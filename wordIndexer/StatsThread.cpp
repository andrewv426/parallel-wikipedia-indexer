#include "StatsThread.h"
#include "Config.h"
#include <stdio.h>
#include <Psapi.h>

static ULONGLONG GetProcessCPUTicks() {
    FILETIME ftCreate, ftExit, ftKernel, ftUser;
    if (!GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser))
        return 0;

    ULONGLONG userTicks   = ((ULONGLONG)ftUser.dwHighDateTime   << 32) | ftUser.dwLowDateTime;
    ULONGLONG kernelTicks = ((ULONGLONG)ftKernel.dwHighDateTime << 32) | ftKernel.dwLowDateTime;
    return userTicks + kernelTicks;
}

DWORD WINAPI StatsThreadProc(LPVOID param) {
    StatsArgs* args = (StatsArgs*)param;

    LARGE_INTEGER freq      = args->freq;
    LARGE_INTEGER startTime = args->startTime;
    size_t        fileSize  = args->fileSize;
    int           nThreads  = args->nThreads;

    LARGE_INTEGER prevTime  = startTime;
    size_t        prevBytes = 0;

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int nCores = (int)si.dwNumberOfProcessors;
    if (nCores < 1) nCores = 1;

    ULONGLONG prevCPUTicks = 0;

    while (WaitForSingleObject(args->hDone, STATS_INTERVAL_MS) == WAIT_TIMEOUT) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double intervalSec = (double)(now.QuadPart - prevTime.QuadPart) / freq.QuadPart;

        // Sum per-thread bytes and words
        size_t totalBytes = 0;
        size_t totalWords = 0;
        for (int i = 0; i < nThreads; i++) {
            totalBytes += args->bytesProcessed[i];
            totalWords += args->totalWords[i];
        }

        double mbps   = intervalSec > 0 ? (double)(totalBytes - prevBytes) / (1024.0*1024.0) / intervalSec : 0.0;
        double pct    = fileSize > 0 ? 100.0 * totalBytes / fileSize : 0.0;
        double wordsMil = totalWords / 1e6;

        size_t lookupCount = 0;
        size_t depthSum    = 0;
        int    maxDepth    = 0;
        for (int i = 0; i < nThreads; i++) {
            lookupCount += args->tables[i]->GetLookupCount();
            depthSum    += args->tables[i]->GetDepthSum();
            int tableMax = args->tables[i]->GetMaxDepth();
            if (tableMax > maxDepth) maxDepth = tableMax;
        }
        double avgDepth = lookupCount ? (double)depthSum / lookupCount : 0.0;

        // CPU usage via GetProcessTimes
        ULONGLONG cpuTicks = GetProcessCPUTicks();
        double cpuPct = 0.0;
        if (cpuTicks > 0) {
            double cpuHns;
            double wallSec;
            if (prevCPUTicks == 0) {
                cpuHns  = (double)cpuTicks;
                wallSec = (double)(now.QuadPart - startTime.QuadPart) / freq.QuadPart;
            } else {
                cpuHns  = (double)(cpuTicks - prevCPUTicks);
                wallSec = intervalSec;
            }
            double wallHns = wallSec * 1e7 * nCores;
            cpuPct = 100.0 * cpuHns / wallHns;
        }
        prevCPUTicks = cpuTicks;

        // RAM usage
        PROCESS_MEMORY_COUNTERS pmc = {};
        pmc.cb = sizeof(pmc);
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        size_t ramMB = pmc.WorkingSetSize / (1024*1024);

        fprintf(stderr, "[%.1f%%] %.1f MB/s, words %.1fM, depth (%.3f, %d), [CPU %.0f%% %zu MB]\n",
                pct, mbps, wordsMil, avgDepth, maxDepth, cpuPct, ramMB);

        prevTime  = now;
        prevBytes = totalBytes;
    }

    return 0;
}
