#pragma once
#include <Windows.h>
#include <stdint.h>

class HashValue {
public:
    DWORD counter;
    char* GetWordPtr() { return (char*)(this + 1); }
};

class HashTable {
    int*   hashBins;
    char*  mainBuf;
    size_t committed;
    size_t used;
    size_t H;
    size_t hashMask;

    size_t lookupCount;
    size_t depthSum;
    size_t uniqueCount;
    int    depthMax;

    void EnsureCommitted(size_t needed);

public:
    HashTable(int log2H);
    ~HashTable();

    HashValue* FindInsertKey(uint64_t key, int valueSize, bool* found);

    size_t GetUniqueCount() const { return uniqueCount; }
    double GetAvgDepth()    const { return lookupCount ? (double)depthSum / lookupCount : 0.0; }
    int    GetMaxDepth()    const { return depthMax; }
    size_t GetLookupCount() const { return lookupCount; }
    size_t GetDepthSum()    const { return depthSum; }

    // Iterate all inserted records: cb(key, count, word, wordLen, ctx)
    void ForEach(void(*cb)(uint64_t, DWORD, const char*, int, void*), void* ctx) const;
};
