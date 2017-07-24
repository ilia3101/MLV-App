#ifndef SYSTEMMEMORY_H
#define SYSTEMMEMORY_H

#ifdef __APPLE__
#include <unistd.h>

//Get RAM in Bytes
unsigned long long getTotalSystemMemory()
{
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    return pages * page_size;
}
#elif _WIN32
#include <windows.h>

//Get RAM in Bytes
unsigned long long getTotalSystemMemory()
{
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullTotalPhys;
}
#endif

#endif // SYSTEMMEMORY_H
