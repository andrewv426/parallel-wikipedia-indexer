#pragma once
#include <stdint.h>
#include "MyBuf.h"

extern char     isalphaLUT[256];
extern char     isdelimiterLUT[256];
extern char     toLowerLUT[256];
extern uint64_t sboxLUT[256];

void InitLUTs();

#ifndef NDEBUG
void TestLUTs();
#endif

int  FindNextWordStart(MyBuf* mb, int off, int* wordStart);
int  FindThisWordEnd(MyBuf* mb, int wordStart, int* wordEnd, uint64_t* hashKey);
bool WordIsEligible(MyBuf* mb, int wordStart, int wordEnd);
