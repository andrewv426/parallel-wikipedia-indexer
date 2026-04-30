#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Config.h"
#include "Tokenizer.h"
#include "MyBuf.h"
#include "HashTable.h"
#include "MyQueue.h"
#include "DiskThread.h"
#include "SearchThread.h"
#include "StatsThread.h"

// -----------------------------------------------------------------------
// Merge helper: called by ForEach to fold a thread table into the master
// -----------------------------------------------------------------------
struct MergeCtx { HashTable* master; };

static void MergeCb(uint64_t key, DWORD count,
                    const char* word, int wordLen, void* ctx) {
    MergeCtx* mc = (MergeCtx*)ctx;
    bool found;
    int  valueSize = (int)sizeof(DWORD) + wordLen + 1;
    HashValue* hv = mc->master->FindInsertKey(key, valueSize, &found);
    if (found) {
        hv->counter += count;
    } else {
        hv->counter = count;
        memcpy(hv->GetWordPtr(), word, wordLen + 1);
    }
}

// -----------------------------------------------------------------------
// main
// -----------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 5) {
        fprintf(stderr, "Usage: wordIndexer <log2_buf_size> <input_file> [n_search_threads] [log2_hash_bins]\n");
        return 1;
    }

    int log2BufSize   = atoi(argv[1]);
    const char* inputFile = argv[2];

    InitLUTs();
#ifndef NDEBUG
    TestLUTs();
    fprintf(stderr, "[debug] LUT assertions passed\n");
#endif

    // Detect logical core count for thread count
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int nThreads = (int)si.dwNumberOfProcessors;
    if (nThreads < 1) nThreads = 1;
    if (argc >= 4) {
        nThreads = atoi(argv[3]);
        if (nThreads < 1) nThreads = 1;
    }
    if (nThreads > MAXIMUM_WAIT_OBJECTS) nThreads = MAXIMUM_WAIT_OBJECTS;

    int log2HashBins = LOG2_HASH_BINS;
    if (argc >= 5) {
        log2HashBins = atoi(argv[4]);
        if (log2HashBins < 1) log2HashBins = 1;
        if (log2HashBins > 30) log2HashBins = 30;
    }

    int bufSize = 1 << log2BufSize;

    HANDLE hFile = CreateFileA(inputFile, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open file: %s\n", inputFile);
        return 1;
    }

    LARGE_INTEGER fileSizeEx;
    GetFileSizeEx(hFile, &fileSizeEx);
    size_t fileSize = (size_t)fileSizeEx.QuadPart;

    // Use idle priority so the kernel cache thread can run.
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);

    // Allocate per-thread state
    HashTable**      tables         = new HashTable*[nThreads];
    SearchArgs*      sargs          = new SearchArgs[nThreads];
    HANDLE*          sHandles       = new HANDLE[nThreads];
    volatile size_t* bytesProcessed = new volatile size_t[nThreads];
    volatile size_t* wordsProcessed = new volatile size_t[nThreads];

    for (int i = 0; i < nThreads; i++) {
        tables[i]              = new HashTable(log2HashBins);
        bytesProcessed[i]      = 0;
        wordsProcessed[i]      = 0;
        sargs[i].queue         = NULL;  // set below
        sargs[i].ht            = tables[i];
        sargs[i].pBytesProcessed = &bytesProcessed[i];
        sargs[i].pTotalWords   = &wordsProcessed[i];
        sargs[i].totalWords    = 0;
        sargs[i].invalidWords  = 0;
    }

    MyQueue queue;
    for (int i = 0; i < nThreads; i++)
        sargs[i].queue = &queue;

    // Start timer before threads launch
    LARGE_INTEGER freq, startTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&startTime);

    // Done event for StatsThread (signaled when search threads finish)
    HANDLE hDone = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Launch search threads (idle priority, pinned to distinct cores)
    for (int i = 0; i < nThreads; i++) {
        sHandles[i] = CreateThread(NULL, 0, SearchThreadProc, &sargs[i], CREATE_SUSPENDED, NULL);
        SetThreadPriority(sHandles[i], THREAD_PRIORITY_IDLE);
        if (i < 64) SetThreadAffinityMask(sHandles[i], (DWORD_PTR)1 << i);
        ResumeThread(sHandles[i]);
    }

    // Launch disk thread (time critical, free to migrate)
    DiskArgs dargs;
    dargs.hFile          = hFile;
    dargs.bufSize        = bufSize;
    dargs.nSearchThreads = nThreads;
    dargs.queue          = &queue;

    HANDLE hDisk = CreateThread(NULL, 0, DiskThreadProc, &dargs, CREATE_SUSPENDED, NULL);
    SetThreadPriority(hDisk, THREAD_PRIORITY_TIME_CRITICAL);
    ResumeThread(hDisk);

    // Launch stats thread (normal priority, runs until hDone is set)
    StatsArgs statsArgs;
    statsArgs.bytesProcessed = bytesProcessed;
    statsArgs.totalWords     = wordsProcessed;
    statsArgs.nThreads       = nThreads;
    statsArgs.fileSize       = fileSize;
    statsArgs.hDone          = hDone;
    statsArgs.startTime      = startTime;
    statsArgs.freq           = freq;
    statsArgs.tables         = tables;

    HANDLE hStats = CreateThread(NULL, 0, StatsThreadProc, &statsArgs, 0, NULL);

    // Wait for all search threads to finish
    WaitForMultipleObjects(nThreads, sHandles, TRUE, INFINITE);
    WaitForSingleObject(hDisk, INFINITE);

    // Signal stats thread to stop and wait
    SetEvent(hDone);
    WaitForSingleObject(hStats, INFINITE);

    CloseHandle(hFile);
    CloseHandle(hDisk);
    CloseHandle(hDone);
    CloseHandle(hStats);
    for (int i = 0; i < nThreads; i++) CloseHandle(sHandles[i]);

    // Merge thread tables 1..N-1 into master (table[0])
    MergeCtx mc;
    mc.master = tables[0];
    for (int i = 1; i < nThreads; i++) {
        tables[i]->ForEach(MergeCb, &mc);
        delete tables[i];
        tables[i] = NULL;
    }

    HashTable* master = tables[0];

    // Aggregate counts
    size_t totalWords   = 0;
    size_t invalidWords = 0;
    for (int i = 0; i < nThreads; i++) {
        totalWords   += sargs[i].totalWords;
        invalidWords += sargs[i].invalidWords;
    }

    // Stop timer once the final merged distribution is ready.
    LARGE_INTEGER endTime;
    QueryPerformanceCounter(&endTime);
    double elapsedSec = (double)(endTime.QuadPart - startTime.QuadPart) / freq.QuadPart;
    double mbps = elapsedSec > 0 ? (fileSize / (1024.0*1024.0)) / elapsedSec : 0.0;

    // Final summary (timer stopped before sort/output)
    double wps = elapsedSec > 0 ? totalWords / elapsedSec : 0.0;
    fprintf(stderr, "Execution time: %.2f sec, %.1f MB/s, %.1fM wps\n",
            elapsedSec, mbps, wps / 1e6);

    printf("Unique:  %zu\n", master->GetUniqueCount());
    printf("Invalid: %zu\n", invalidWords);
    printf("Total:   %zu\n", totalWords);


    delete master;
    delete[] tables;
    delete[] sargs;
    delete[] sHandles;
    delete[] (size_t*)bytesProcessed;
    delete[] (size_t*)wordsProcessed;

    return 0;
}
