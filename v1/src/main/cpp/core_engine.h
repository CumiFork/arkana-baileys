#pragma once

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((visibility("default"))) const char* core_get_system_info(void);
__attribute__((visibility("default"))) const char* core_get_cpu_load(void);
__attribute__((visibility("default"))) const char* core_get_mem_info(void);
__attribute__((visibility("default"))) const char* core_get_thermal_info(void);
__attribute__((visibility("default"))) const char* core_get_selinux_status(void);
__attribute__((visibility("default"))) const char* core_format_command_output(const char* raw);
__attribute__((visibility("default"))) void core_free_string(const char* str);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace core {
std::string getSystemInfo();
std::string getCpuLoad();
std::string getMemInfo();
std::string getThermalInfo();
std::string getSelinuxStatus();
std::string formatCommandOutput(const std::string& raw);
}
#endif
