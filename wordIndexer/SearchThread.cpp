#include "SearchThread.h"
#include "DiskThread.h"   // FreeBuf
#include "Tokenizer.h"
#include "Config.h"
#include <stdlib.h>
#include <string.h>

DWORD WINAPI SearchThreadProc(LPVOID param) {
    SearchArgs* args = (SearchArgs*)param;
    MyQueue*    queue = args->queue;
    HashTable*  ht    = args->ht;

    size_t totalWords   = 0;
    size_t invalidWords = 0;

    while (true) {
        MyBuf* mb = queue->Dequeue();
        if (!mb) break;   // NULL sentinel = end of stream

        // For non-first buffers, start at -(MAX_WORD_LEN) to cover the gap region
        // [B-MAX_WORD_LEN, B-1] that the previous buffer's lastStart cutoff excluded.
        // For first buffers, start at 0 (shadow is all '\0' delimiters from calloc).
        int off = mb->isFirst ? 0 : -MAX_WORD_LEN;

        while (off < mb->lastStart) {
            int wordStart;
            if (FindNextWordStart(mb, off, &wordStart) == EOB) break;

            // A word found in the shadow or at offset 0 whose preceding byte is alpha
            // is a continuation of a word counted by the previous buffer — skip it.
            bool isContinuation = isalphaLUT[(uint8_t)mb->buf[wordStart - 1]];

            int wordEnd;
            uint64_t hashKey;
            if (FindThisWordEnd(mb, wordStart, &wordEnd, &hashKey) == EOB) {
                if (!isContinuation) {
                    invalidWords++;
                    totalWords++;
                }
                break;
            }

            if (isContinuation) {
                off = wordEnd + 1;
                continue;
            }

            int wordLen = wordEnd - wordStart;
            if (WordIsEligible(mb, wordStart, wordEnd)) {
                bool found;
                int valueSize = (int)sizeof(DWORD) + wordLen + 1;
                HashValue* hv = ht->FindInsertKey(hashKey, valueSize, &found);
                if (found) {
                    hv->counter++;
                } else {
                    hv->counter = 1;
                    char* dst = hv->GetWordPtr();
                    const char* src = mb->buf + wordStart;
                    for (int i = 0; i < wordLen; i++)
                        dst[i] = toLowerLUT[(uint8_t)src[i]];
                    dst[wordLen] = '\0';
                }
            } else {
                invalidWords++;
            }
            totalWords++;
            off = wordEnd + 1;
        }

        *args->pBytesProcessed += mb->bytesRead;
        *args->pTotalWords      = totalWords;
        FreeBuf(mb);
    }

    args->totalWords   = totalWords;
    args->invalidWords = invalidWords;
    return 0;
}
