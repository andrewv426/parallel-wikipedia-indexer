#include "HashTable.h"
#include "Config.h"
#include <assert.h>
#include <string.h>

HashTable::HashTable(int log2H) {
    H        = (size_t)1 << log2H;
    hashMask = H - 1;

    hashBins = new int[H];
    for (size_t i = 0; i < H; i++) hashBins[i] = -1;

    mainBuf = (char*)VirtualAlloc(NULL, HASH_RESERVE_BYTES, MEM_RESERVE, PAGE_READWRITE);
    assert(mainBuf != NULL);

    committed   = 0;
    used        = 0;
    lookupCount = 0;
    depthSum    = 0;
    uniqueCount = 0;
    depthMax    = 0;
}

HashTable::~HashTable() {
    delete[] hashBins;
    VirtualFree(mainBuf, 0, MEM_RELEASE);
}

void HashTable::EnsureCommitted(size_t needed) {
    while (needed > committed) {
        VirtualAlloc(mainBuf + committed, HASH_COMMIT_INCREMENT, MEM_COMMIT, PAGE_READWRITE);
        committed += HASH_COMMIT_INCREMENT;
    }
}

// Record layout: [0..7] uint64_t key | [8..11] int nextOffset | [12+] HashValue
HashValue* HashTable::FindInsertKey(uint64_t key, int valueSize, bool* found) {
    size_t bin    = key & hashMask;
    int    off    = hashBins[bin];
    int*   prevLink = &hashBins[bin];
    int    depth  = 1;

    while (off != -1) {
        char* rec = mainBuf + off;
        if (*(uint64_t*)rec == key) {
            *found = true;
            lookupCount++;
            depthSum += depth;
            if (depth > depthMax) depthMax = depth;
            return (HashValue*)(rec + 12);
        }
        prevLink = (int*)(rec + 8);
        off      = *prevLink;
        depth++;
    }

    // Not found — append new record at end of chain
    int recSize = 8 + 4 + valueSize;
    EnsureCommitted(used + recSize);

    int   newOff = (int)used;
    char* rec    = mainBuf + newOff;
    *(uint64_t*)rec      = key;
    *(int*)(rec + 8)     = -1;   // nextOffset sentinel
    *prevLink            = newOff;
    used                += recSize;

    *found = false;
    uniqueCount++;
    lookupCount++;
    depthSum += depth;
    if (depth > depthMax) depthMax = depth;
    return (HashValue*)(rec + 12);
}

void HashTable::ForEach(void(*cb)(uint64_t, DWORD, const char*, int, void*), void* ctx) const {
    for (size_t bin = 0; bin < H; bin++) {
        int off = hashBins[bin];
        while (off != -1) {
            char*      rec  = mainBuf + off;
            uint64_t   key  = *(uint64_t*)rec;
            int        next = *(int*)(rec + 8);
            HashValue* hv   = (HashValue*)(rec + 12);
            const char* word = hv->GetWordPtr();
            int wordLen = (int)strlen(word);
            cb(key, hv->counter, word, wordLen, ctx);
            off = next;
        }
    }
}
