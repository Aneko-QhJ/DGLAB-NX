#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t blocked, previous, held, down;
} DglabUiKeys;

// Held buttons at a page/backend boundary must be released before reuse.
void dglabUiKeysReset(DglabUiKeys* keys, uint64_t held);
void dglabUiKeysUpdate(DglabUiKeys* keys, uint64_t raw);

typedef struct {
    uint64_t started, repeated;
    int direction;
} DglabUiRepeat;
void dglabUiRepeatReset(DglabUiRepeat* repeat);
// Returns at most one step. Times are monotonic nanoseconds.
int dglabUiRepeatStep(DglabUiRepeat* repeat, int press, int held, uint64_t now);
