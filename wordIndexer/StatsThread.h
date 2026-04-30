#pragma once
#include <Windows.h>
#include "HashTable.h"

struct StatsArgs {
    volatile const size_t* bytesProcessed; // per-thread array
    volatile const size_t* totalWords;     // per-thread array
    int                    nThreads;
    size_t                 fileSize;
    HANDLE                 hDone;          // manual-reset event: set when search threads done
    LARGE_INTEGER          startTime;
    LARGE_INTEGER          freq;
    HashTable* const*      tables;         // per-thread hash tables (for depth stats)
};

DWORD WINAPI StatsThreadProc(LPVOID param);
