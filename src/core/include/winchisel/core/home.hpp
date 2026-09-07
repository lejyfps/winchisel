#pragma once

#include <string>

namespace winchisel::core {

struct HomeInfo {
    std::string cpu_brand;
    std::string cpu_cores;
    std::string cpu_speed;
    std::string cpu_usage;
    float cpu_usage_percent{};

    std::string gpu_name;
    std::string gpu_vram;
    std::string gpu_driver;

    std::string memory_total;
    std::string memory_used;
    std::string ram_details;
    float memory_fraction{};

    std::string storage_total;
    std::string storage_used;
    float storage_fraction{};

    std::string os_version;
    std::string windows_build;
    std::string computer_name;
    std::string kernel_version;

    std::string motherboard;
    std::string bios;

    std::string display;
    std::string uptime;
};

}  // namespace winchisel::core
