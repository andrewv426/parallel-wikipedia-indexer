#pragma once
#include <Windows.h>
#include "MyQueue.h"
#include "MyBuf.h"

struct DiskArgs {
    HANDLE   hFile;
    int      bufSize;
    int      nSearchThreads;   // number of NULL sentinels to enqueue at end
    MyQueue* queue;
};

void  FreeBuf(MyBuf* mb);
DWORD WINAPI DiskThreadProc(LPVOID param);
