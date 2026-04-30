#pragma once
#include <Windows.h>

constexpr int     MIN_WORD_LEN          = 3;
constexpr int     MAX_WORD_LEN          = 31;
constexpr int     N_BUFFERS             = 24;
constexpr int     LOG2_HASH_BINS        = 20;
constexpr size_t  HASH_RESERVE_BYTES    = 1ULL << 31;
constexpr size_t  HASH_COMMIT_INCREMENT = 1ULL << 20;
constexpr int     STATS_INTERVAL_MS     = 2000;
constexpr DWORD   MT_SEED               = 19650218UL;
constexpr int     EOB                   = -1;
