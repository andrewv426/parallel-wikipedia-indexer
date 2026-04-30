#pragma once
#include <Windows.h>

struct MyBuf {
    char*  buf;       // points to shadow+1, so buf[-1] is the last shadow byte
    int    size;      // usable bytes in buf[0..size-1]
    int    lastStart; // cutoff: word starts must be < lastStart
    DWORD  bytesRead;
    bool   isFirst;
    bool   isLast;
};
