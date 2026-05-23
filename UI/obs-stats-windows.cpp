#include "util/windows/win-registry.h"
#include "util/windows/win-version.h"
#include "util/platform.h"
#include "util/dstr.h"
#include "obs.h"
//#include "obs-internal.h"
#include "../UI/obs-frontend-api/obs-frontend-api.h"
#include <string>
#include <vector>

void gen_processor_info(std::string& info_out)
{
	HKEY key;
	wchar_t data[1024];
	DWORD size;
	LSTATUS status;

	char* CPU_Name;
	DWORD CPU_Speed;

	memset(data, 0, sizeof(data));

	status = RegOpenKeyW(
		HKEY_LOCAL_MACHINE,
		L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", &key);

	size = sizeof(data);
	status = RegQueryValueExW(key, L"ProcessorNameString", NULL, NULL,
				  (LPBYTE)data, &size);

	info_out += "\"cpu-name\": ";

	if (status == ERROR_SUCCESS) {
		os_wcs_to_utf8_ptr(data, 256, &CPU_Name);
		info_out += std::string("\"") + CPU_Name + "\", ";
		bfree(CPU_Name);
	} 
	else
	{
		info_out += "null, ";
	}
	size = 128;

	info_out += "\"cpu-speed\": ";
	status = RegQueryValueExW(key, L"~MHz", NULL, NULL, (LPBYTE)&CPU_Speed, &size);
	if (status == ERROR_SUCCESS) {
		info_out += std::string("\"") + std::to_string(CPU_Speed) + "MHz\", ";
	}
	else
	{
		info_out += "null, ";
	}


	RegCloseKey(key);
}

void gen_available_memory(std::string& info_out)
{
	MEMORYSTATUSEX ms;
	ms.dwLength = sizeof(ms);

	GlobalMemoryStatusEx(&ms);
	
	info_out += "\"physical-memory\": \"";
	info_out += std::to_string(ms.ullTotalPhys / 1048576) + "MB Total\", ";
	info_out += "\"physical-memory-available\": \"";
	info_out += std::to_string(ms.ullAvailPhys / 1048576) + "MB\", ";
}

