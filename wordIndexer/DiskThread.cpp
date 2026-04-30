#include "DiskThread.h"
#include "Config.h"
#include <stdlib.h>
#include <string.h>

static const int SHADOW_LEN = MAX_WORD_LEN + 1;

static MyBuf* AllocBuf(int bufSize) {
    char* raw = (char*)calloc(SHADOW_LEN + bufSize + 1, 1);
    if (!raw) return NULL;
    MyBuf* mb = (MyBuf*)calloc(1, sizeof(MyBuf));
    mb->buf   = raw + SHADOW_LEN;
    mb->size  = bufSize;
    return mb;
}

void FreeBuf(MyBuf* mb) {
    if (!mb) return;
    free(mb->buf - SHADOW_LEN);
    free(mb);
}

DWORD WINAPI DiskThreadProc(LPVOID param) {
    DiskArgs* args    = (DiskArgs*)param;
    HANDLE    hFile   = args->hFile;
    int       bufSize = args->bufSize;
    MyQueue*  queue   = args->queue;
    int       nSearch = args->nSearchThreads;

    // Holds last SHADOW_LEN bytes of the previously enqueued buffer
    // to fill the shadow region of the next buffer.
    char savedShadow[MAX_WORD_LEN + 1];
    bool hasShadow = false;

    bool first = true;

    while (true) {
        MyBuf* cur = AllocBuf(bufSize);
        if (!cur) break;

        DWORD bytesRead = 0;
        ReadFile(hFile, cur->buf, bufSize, &bytesRead, NULL);
        if (bytesRead == 0) { FreeBuf(cur); break; }

        cur->bytesRead = bytesRead;
        cur->isFirst   = first;
        cur->isLast    = (bytesRead < (DWORD)bufSize);

        if (cur->isLast) {
            cur->buf[bytesRead] = '\0';
            cur->size           = (int)bytesRead + 1;
            cur->lastStart      = (int)bytesRead;
        } else {
            cur->size      = bufSize;
            // lastStart = bufSize - MAX_WORD_LEN ensures valid words (len<=31)
            // never straddle a buffer boundary: max start = bufSize-32, max end =
            // bufSize-32+30 = bufSize-2, terminator at bufSize-1 (within buffer).
            cur->lastStart = bufSize - MAX_WORD_LEN;
        }

        // Install shadow from the previous buffer's saved tail bytes
        if (hasShadow)
            memcpy(cur->buf - SHADOW_LEN, savedShadow, SHADOW_LEN);
        // else: first buffer shadow is '\0' from calloc

        // Save the tail bytes of cur BEFORE enqueueing (safe: no concurrent reader yet)
        int saveFrom = (cur->isLast ? (int)cur->bytesRead : cur->size) - SHADOW_LEN;
        if (saveFrom < 0) saveFrom = 0;
        memcpy(savedShadow, cur->buf + saveFrom, SHADOW_LEN);
        hasShadow = true;

        queue->Enqueue(cur);

        if (cur->isLast) break;
        first = false;
    }

    // Signal end-of-stream to each search thread
    for (int i = 0; i < nSearch; i++)
        queue->Enqueue(NULL);

    return 0;
}
