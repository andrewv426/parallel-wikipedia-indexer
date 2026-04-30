#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>  // std::sort (only allowed STL)
#include "Config.h"
#include "Tokenizer.h"
#include "MyBuf.h"
#include "HashTable.h"
#include "MyQueue.h"
#include "DiskThread.h"
#include "SearchThread.h"
#include "StatsThread.h"

struct WordRank {
    const char* word;
    DWORD       count;
    int         len;
};

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

struct CollectCtx {
    WordRank* rows;
    size_t    index;
    size_t    validWords;
    size_t    lengthCounts[MAX_WORD_LEN + 1];
};

static void CollectCb(uint64_t, DWORD count, const char* word, int wordLen, void* ctx) {
    CollectCtx* cc = (CollectCtx*)ctx;
    WordRank* wr = &cc->rows[cc->index++];
    wr->word  = word;
    wr->count = count;
    wr->len   = wordLen;

    cc->validWords += count;
    if (wordLen >= 0 && wordLen <= MAX_WORD_LEN)
        cc->lengthCounts[wordLen] += count;
}

static bool WordRankLess(const WordRank& a, const WordRank& b) {
    if (a.count != b.count) return a.count > b.count;
    return strcmp(a.word, b.word) < 0;
}

static void MakeDatasetName(const char* path, char* out, size_t outCap) {
    const char* base = path;
    for (const char* p = path; *p; p++) {
        if (*p == '\\' || *p == '/') base = p + 1;
    }

    size_t n = 0;
    for (const char* p = base; *p && n + 1 < outCap; p++) {
        if (*p == '.') break;
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            c = '_';
        }
        out[n++] = c;
    }
    if (n == 0 && outCap > 1) {
        const char* fallback = "input";
        while (*fallback && n + 1 < outCap)
            out[n++] = *fallback++;
    }
    out[n] = '\0';
}

static void MakeReportPath(char* out, size_t outCap, const char* dataset, const char* suffix) {
    _snprintf_s(out, outCap, _TRUNCATE, "report_data\\%s_%s", dataset, suffix);
}

static void ExportReportData(HashTable* master,
                             const char* inputFile,
                             size_t fileSize,
                             int log2BufSize,
                             int bufSize,
                             int nThreads,
                             int log2HashBins,
                             size_t invalidWords,
                             size_t totalWords,
                             double elapsedSec,
                             double mbps,
                             double wps) {
    CreateDirectoryA("report_data", NULL);

    size_t uniqueCount = master->GetUniqueCount();
    WordRank* rows = new WordRank[uniqueCount];

    CollectCtx cc = {};
    cc.rows = rows;
    master->ForEach(CollectCb, &cc);

    std::sort(rows, rows + uniqueCount, WordRankLess);

    char dataset[128];
    MakeDatasetName(inputFile, dataset, sizeof(dataset));
    char runName[192];
    _snprintf_s(runName, sizeof(runName), _TRUNCATE,
                "%s_b%d_t%d_h%d", dataset, log2BufSize, nThreads, log2HashBins);

    double avgLen = cc.validWords ? (double)0.0 : 0.0;
    if (cc.validWords) {
        size_t weightedLen = 0;
        for (int len = MIN_WORD_LEN; len <= MAX_WORD_LEN; len++)
            weightedLen += cc.lengthCounts[len] * (size_t)len;
        avgLen = (double)weightedLen / (double)cc.validWords;
    }

    char path[MAX_PATH];
    MakeReportPath(path, sizeof(path), runName, "summary.csv");
    FILE* f = NULL;
    fopen_s(&f, path, "w");
    if (f) {
        fprintf(f, "dataset,file_size_bytes,log2_buf_size,buf_size,n_threads,log2_hash_bins,unique,valid,invalid,total,elapsed_sec,mbps,wps,avg_word_len\n");
        fprintf(f, "%s,%zu,%d,%d,%d,%d,%zu,%zu,%zu,%zu,%.6f,%.3f,%.3f,%.6f\n",
                dataset, fileSize, log2BufSize, bufSize, nThreads, log2HashBins,
                uniqueCount, cc.validWords, invalidWords, totalWords,
                elapsedSec, mbps, wps, avgLen);
        fclose(f);
    }

    MakeReportPath(path, sizeof(path), runName, "ranks.txt");
    f = NULL;
    fopen_s(&f, path, "w");
    if (f) {
        fprintf(f, "Top 20\n");
        size_t topN = uniqueCount < 20 ? uniqueCount : 20;
        for (size_t i = 0; i < topN; i++)
            fprintf(f, "[%zu] %s = %lu\n", i, rows[i].word, (unsigned long)rows[i].count);

        fprintf(f, "\nRanks 5000-5020\n");
        for (size_t i = 5000; i <= 5020 && i < uniqueCount; i++)
            fprintf(f, "[%zu] %s = %lu\n", i, rows[i].word, (unsigned long)rows[i].count);
        fclose(f);
    }

    MakeReportPath(path, sizeof(path), runName, "zipf_top10000.csv");
    f = NULL;
    fopen_s(&f, path, "w");
    if (f) {
        fprintf(f, "rank,word,count\n");
        size_t zipfN = uniqueCount < 10000 ? uniqueCount : 10000;
        for (size_t i = 0; i < zipfN; i++)
            fprintf(f, "%zu,%s,%lu\n", i + 1, rows[i].word, (unsigned long)rows[i].count);
        fclose(f);
    }

    MakeReportPath(path, sizeof(path), runName, "length_dist.csv");
    f = NULL;
    fopen_s(&f, path, "w");
    if (f) {
        fprintf(f, "length,count,percent\n");
        for (int len = MIN_WORD_LEN; len <= MAX_WORD_LEN; len++) {
            double pct = cc.validWords ? 100.0 * (double)cc.lengthCounts[len] / (double)cc.validWords : 0.0;
            fprintf(f, "%d,%zu,%.6f\n", len, cc.lengthCounts[len], pct);
        }
        fclose(f);
    }

    delete[] rows;
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

    ExportReportData(master, inputFile, fileSize, log2BufSize, bufSize, nThreads,
                     log2HashBins, invalidWords, totalWords, elapsedSec, mbps, wps);

    delete master;
    delete[] tables;
    delete[] sargs;
    delete[] sHandles;
    delete[] (size_t*)bytesProcessed;
    delete[] (size_t*)wordsProcessed;

    return 0;
}
