#pragma once
#include <stdbool.h>
#include <math.h>

// QR layout in drawable pixels. The view converts to/from NanoVG units once.
typedef struct { int x, y, module, side; } DglabQrPixels;
static inline bool dglabQrPixels(float x, float y, float width, float height,
    unsigned modules, int maximum, int minimum, DglabQrPixels* out)
{
    if (!out || !modules || maximum < minimum || minimum <= 0) return false;
    int left = (int)ceilf(x), top = (int)ceilf(y);
    int right = (int)floorf(x + width), bottom = (int)floorf(y + height);
    int total = (int)modules + 8; // Four-module quiet zone on every edge.
    int room = right - left < bottom - top ? right - left : bottom - top;
    int module = room / total;
    if (module > maximum) module = maximum;
    if (module < minimum) return false;
    out->module = module; out->side = total * module;
    out->x = left + (right - left - out->side) / 2;
    out->y = top + (bottom - top - out->side) / 2;
    return true;
}
