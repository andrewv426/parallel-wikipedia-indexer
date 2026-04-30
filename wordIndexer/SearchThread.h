#pragma once
#include <Windows.h>
#include "MyQueue.h"
#include "HashTable.h"

struct SearchArgs {
    MyQueue*          queue;
    HashTable*        ht;
    volatile size_t*  pBytesProcessed; // cumulative bytes (read by stats thread)
    volatile size_t*  pTotalWords;     // cumulative words (approximate, for stats)
    size_t            totalWords;      // final count, filled at thread exit
    size_t            invalidWords;
};

DWORD WINAPI SearchThreadProc(LPVOID param);
