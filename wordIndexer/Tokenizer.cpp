#include "Tokenizer.h"
#include "Config.h"
#include "../mt/mt.h"
#include <string.h>
#include <assert.h>
#include <stdint.h>

char     isalphaLUT[256];
char     isdelimiterLUT[256];
char     toLowerLUT[256];
uint64_t sboxLUT[256];

void InitLUTs() {
    memset(isalphaLUT, 0, 256);
    for (int c = 'a'; c <= 'z'; c++) isalphaLUT[c] = 1;
    for (int c = 'A'; c <= 'Z'; c++) isalphaLUT[c] = 1;

    memset(isdelimiterLUT, 0, 256);
    // 15 delimiters: NUL, space, comma, \n, \r, period, apostrophe, double-quote,
    //                ?, dash, colon, semicolon, *, !, \t
    const unsigned char delims[] = { 0, ' ', ',', '\n', '\r', '.', '\'', '"',
                                     '?', '-', ':', ';', '*', '!', '\t' };
    for (int i = 0; i < 15; i++) isdelimiterLUT[delims[i]] = 1;

    for (int c = 0; c < 256; c++) toLowerLUT[c] = (char)c;
    for (int c = 'A'; c <= 'Z'; c++) toLowerLUT[c] = (char)(c - 'A' + 'a');

    MersenneTwister mt;
    mt.init_genrand64(MT_SEED);
    for (int i = 0; i < 256; i++) sboxLUT[i] = mt.genrand64_int64();
    // Case-fold: uppercase maps to same hash value as lowercase
    for (int c = 'A'; c <= 'Z'; c++) sboxLUT[c] = sboxLUT[c - 'A' + 'a'];
}

#ifndef NDEBUG
void TestLUTs() {
    int alphaCount = 0;
    for (int i = 0; i < 256; i++) if (isalphaLUT[i]) alphaCount++;
    assert(alphaCount == 52);

    int delimCount = 0;
    for (int i = 0; i < 256; i++) if (isdelimiterLUT[i]) delimCount++;
    assert(delimCount == 15);

    for (int i = 0; i < 256; i++)
        assert(!(isalphaLUT[i] && isdelimiterLUT[i]));

    for (int c = 'A'; c <= 'Z'; c++)
        assert(sboxLUT[c] == sboxLUT[c - 'A' + 'a']);
}
#endif

// Skip non-alpha bytes; set *wordStart to first alpha byte.
// Returns 0 on success, EOB if no alpha byte found before mb->lastStart.
int FindNextWordStart(MyBuf* mb, int off, int* wordStart) {
    while (off < mb->lastStart) {
        if (isalphaLUT[(uint8_t)mb->buf[off]]) {
            *wordStart = off;
            return 0;
        }
        off++;
    }
    return EOB;
}

// Scan alpha bytes starting at wordStart, hashing in the same pass.
// Sets *wordEnd to the first non-alpha byte offset and *hashKey to the hash.
// Returns EOB if the word runs past mb->size (straddles buffer boundary).
int FindThisWordEnd(MyBuf* mb, int wordStart, int* wordEnd, uint64_t* hashKey) {
    uint64_t h = 0;
    int off = wordStart;
    while (off < mb->size && isalphaLUT[(uint8_t)mb->buf[off]]) {
        h = (h + sboxLUT[(uint8_t)mb->buf[off]]) * 3;
        off++;
    }
    if (off >= mb->size && isalphaLUT[(uint8_t)mb->buf[off - 1]]) {
        // Word may continue into the next buffer
        return EOB;
    }
    *wordEnd  = off;
    *hashKey  = h;
    return 0;
}

// A word [wordStart, wordEnd) is eligible if:
//   - length in [MIN_WORD_LEN, MAX_WORD_LEN]
//   - byte before wordStart is a delimiter
//   - byte at wordEnd is a delimiter
bool WordIsEligible(MyBuf* mb, int wordStart, int wordEnd) {
    int len = wordEnd - wordStart;
    if (len < MIN_WORD_LEN || len > MAX_WORD_LEN) return false;
    if (!isdelimiterLUT[(uint8_t)mb->buf[wordStart - 1]]) return false;
    if (!isdelimiterLUT[(uint8_t)mb->buf[wordEnd]])       return false;
    return true;
}
