#include <stdint.h>

////// vvvvv - windows-only code starts here. pls port to linux and put in an ifdef

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>

uint8_t * copy_as_executable(uint8_t * mem, size_t len)
{
    void * const buffer = VirtualAlloc(NULL, len, MEM_COMMIT, PAGE_READWRITE);
    
    printf("0x%llX\n", (uint64_t)buffer);
    
    memcpy(buffer, mem, len);
    DWORD dummy;
    VirtualProtect(buffer, len, PAGE_EXECUTE_READ, &dummy);
    
    return (uint8_t *)buffer;
}

void free_as_executable(uint8_t * mem)
{
    VirtualFree(mem, 0, MEM_RELEASE);
}

////// ^^^^^ - windows-only code ends
