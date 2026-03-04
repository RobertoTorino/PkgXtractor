// Minimal hwinfo stub for Windows builds
#pragma once

#include <string>
#include <vector>

namespace hwinfo {

class CPU {
public:
    std::string vendor() const { return "Unknown"; }
    std::string modelName() const { return "Unknown CPU"; }
    int numPhysicalCores() const { return 4; }
    int numLogicalCores() const { return 8; }
    int64_t regularClockSpeed_Hz() const { return 3000000000; }
};

class Memory {
public:
    int64_t total_Bytes() const { return 16ULL * 1024 * 1024 * 1024; }
    int64_t available_Bytes() const { return 8ULL * 1024 * 1024 * 1024; }
};

class OS {
public:
    std::string name() const { return "Windows"; }
    std::string version() const { return "10/11"; }
    std::string fullName() const { return "Windows 10/11"; }
};

inline std::vector<CPU> getAllCPUs() {
    return {CPU()};
}

} // namespace hwinfo
