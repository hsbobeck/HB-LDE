#include "util/platform.h"
#include "util/dstr.h"
#include "obs.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include <Carbon/Carbon.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDManager.h>

#include <string>
#import <AppKit/AppKit.h>


void gen_processor_info(std::string& info_out)
{
	char *name = NULL;
	size_t size;
	int ret;

	ret = sysctlbyname("machdep.cpu.brand_string", NULL, &size, NULL, 0);
	if (ret != 0)
	{
		info_out += "\"cpu-name\": ";
		info_out += "null, ";
	}
	else
	{
		name = (char*)malloc(size);

		ret = sysctlbyname("machdep.cpu.brand_string", name, &size, NULL, 0);

		info_out += "\"cpu-name\": ";
		if (ret == 0)
			info_out += std::string("\"") + name + "\", ";
		else
			info_out += "null, ";

		free(name);
	}

	long long freq;

	size = sizeof(freq);

	info_out += "\"cpu-speed\": ";
	ret = sysctlbyname("hw.cpufrequency", &freq, &size, NULL, 0);
	if (ret == 0)
		info_out += std::string("\"") + std::to_string(freq / 1000000) + "MHz\", ";
	else
		info_out += "null, ";
}

void gen_available_memory(std::string& info_out)
{
	size_t size;
	long long memory_total;
	long long memory_user;
	int ret;

    memory_total = 0;
	size = sizeof(memory_total);
	ret = sysctlbyname("hw.memsize", &memory_total, &size, NULL, 0);

	info_out += "\"physical-memory\": \"";
	if (ret == 0)
		info_out += std::to_string(memory_total / 1048576) + "MB Total\", ";
	else
		info_out += "null, ";

    memory_user = 0;
    size = sizeof(memory_user);
	ret = sysctlbyname("hw.usermem", &memory_user, &size, NULL, 0);

	info_out += "\"physical-memory-available\": \"";
	if (ret == 0)
		info_out += std::to_string((memory_total - memory_user) / 1048576) + "MB\", ";
	else
		info_out += "null, ";
}

