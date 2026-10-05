#!/usr/bin/env python3
"""Copy absolute non-system dylibs into an app bundle and point them at @rpath.

Homebrew dylibs record absolute install names. A later brew upgrade can
remove or rename those files and dyld then aborts the app at launch. This
walks the bundle, copies every such dependency into Contents/Frameworks, and
rewrites load commands to @rpath/<name>, which the executable already searches.
"""

import os
import shutil
import subprocess
import sys

SYSTEM_PREFIXES = ("/usr/lib/", "/System/", "/usr/local/lib/libSystem")
MACHO_MAGICS = (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def run(cmd):
    result = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)}\n{result.stderr.strip()}")
    return result.stdout


def is_macho(path):
    try:
        with open(path, "rb") as handle:
            return handle.read(4) in MACHO_MAGICS
    except OSError:
        return False


def otool_lines(path):
    lines = []
    for line in run(["otool", "-L", path]).splitlines()[1:]:
        entry = line.strip()
        if not entry or entry.endswith(":") or "(architecture " in entry:
            continue
        dep = entry.split(" (compatibility version", 1)[0].strip()
        if dep:
            lines.append(dep)
    return lines


def dylib_id(path):
    ids = []
    for line in run(["otool", "-D", path]).splitlines():
        entry = line.strip()
        if not entry or entry.endswith(":") or entry.startswith("Archive"):
            continue
        ids.append(entry)
    return ids[-1] if ids else None


def is_system(path):
    return path.startswith(SYSTEM_PREFIXES) or path.startswith("@")


def app_root(binary):
    contents = os.path.dirname(os.path.dirname(os.path.realpath(binary)))
    if not contents.endswith("/Contents"):
        raise RuntimeError(f"{binary} is not inside an application bundle")
    return os.path.dirname(contents)


def iter_machos(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [name for name in dirnames if name != "Headers"]
        for name in filenames:
            path = os.path.join(dirpath, name)
            if not os.path.islink(path) and is_macho(path):
                yield path


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <app-binary> <entitlements.plist>", file=sys.stderr)
        return 2

    binary = os.path.realpath(sys.argv[1])
    entitlements = sys.argv[2]
    root = app_root(binary)
    frameworks = os.path.join(root, "Contents", "Frameworks")
    os.makedirs(frameworks, exist_ok=True)

    images = list(iter_machos(root))
    if binary not in images:
        images.append(binary)

    # install id -> real source file outside the bundle
    sources = {}
    # every load-command string that should be rewritten
    aliases = {}
    pending = list(images)
    seen = set()

    while pending:
        path = pending.pop()
        real = os.path.realpath(path)
        if real in seen or not os.path.exists(real):
            continue
        seen.add(real)
        try:
            deps = otool_lines(real)
        except RuntimeError as error:
            print(f"warning: {error}", file=sys.stderr)
            continue

        outside = not real.startswith(root + os.sep)
        ident = dylib_id(real) if outside else None
        if outside and ident and not is_system(ident):
            sources.setdefault(ident, real)
            aliases[ident] = ident

        for dep in deps:
            if is_system(dep):
                continue
            if os.path.islink(dep) or os.path.exists(dep):
                dep_real = os.path.realpath(dep)
            else:
                print(f"warning: missing dependency {dep} from {os.path.basename(real)}", file=sys.stderr)
                continue
            if dep_real.startswith(root + os.sep):
                continue
            ident = dylib_id(dep_real)
            if not ident or is_system(ident):
                continue
            sources.setdefault(ident, dep_real)
            aliases[dep] = ident
            aliases[ident] = ident
            if dep_real not in seen:
                pending.append(dep_real)

    if not sources:
        print("No absolute non-system dylibs to bundle")
        return 0

    bundled = {}
    for ident, source in sorted(sources.items()):
        name = os.path.basename(ident)
        dest = os.path.join(frameworks, name)
        if os.path.lexists(dest):
            existing = os.path.realpath(dest)
            if existing != os.path.realpath(source) and dylib_id(dest) not in (ident, f"@rpath/{name}"):
                raise RuntimeError(f"{dest} already exists and is a different library")
        if not os.path.exists(dest) or os.path.realpath(dest) != os.path.realpath(source):
            if os.path.lexists(dest):
                os.remove(dest)
            shutil.copy2(source, dest)
        os.chmod(dest, os.stat(dest).st_mode | 0o200)
        bundled[ident] = f"@rpath/{name}"
        print(f"bundled {name}")

    for ident, dest_name in bundled.items():
        dest = os.path.join(frameworks, os.path.basename(ident))
        if dylib_id(dest) != dest_name:
            run(["install_name_tool", "-id", dest_name, dest])

    for image in iter_machos(root):
        present = set(otool_lines(image))
        for old, ident in aliases.items():
            if old in present and ident in bundled and old != bundled[ident]:
                run(["install_name_tool", "-change", old, bundled[ident], image])

    for ident in bundled:
        dest = os.path.join(frameworks, os.path.basename(ident))
        run(["codesign", "--force", "--sign", "-", dest])

    run(
        [
            "codesign",
            "--force",
            "--sign",
            "-",
            "--options",
            "runtime",
            "--entitlements",
            entitlements,
            root,
        ]
    )
    print(f"Bundled {len(bundled)} libraries into {frameworks}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
