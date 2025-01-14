#pragma once

#include <logger.hpp>
#if __linux__
#include <unistd.h>
#elif _WIN32
#include <windows.h>
#endif

namespace structures{
// cpu side system info
struct sys_info{
    // flags
    bool    initialized : 1;

    size_t  ram_size{};

    void init(){
    initialized = true;

#if __linux__
    size_t pages = sysconf(_SC_PHYS_PAGES);
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    ram_size = pages * page_size;
    ::logger << logging::info_prefix << " Found " << (ram_size >> 30) << " GByte of Ram" << logging::endl;
#elif _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    ram_size = status.ullTotalPhys;
    ::logger << logging::info_prefix << " Found " << (ram_size >> 30) << " GByte of Ram" << logging::endl;
#elif __APPLE__
    int mib [] = { CTL_HW, HW_MEMSIZE };
    int64_t value = 0;
    size_t length = sizeof(value);     
    if(-1 == sysctl(mib, 2, &value, &length, NULL, 0))
        logger << logging::error_prefix << " Ram size could not be loaded, sysclt() returned value -1" << logging::endl;
    else{
        ram_size = value;
        ::logger << logging::info_prefix << " Found " << (ram_size >> 30) << " GByte of Ram" << logging::endl;  
    }
#else
    logger << logging::info_prefix << " Ram size could not be loaded for your system due to missing hardware specific implementation. Expect problems for loading large datasets that might exceed the RAM limit" << logging::endl;
#endif
    }
};
}

namespace globals{
extern structures::sys_info sys_info;
}