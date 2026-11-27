#pragma once
#include <cstdint>

inline const char *tdeckFormatUptime(char (&text)[21], uint64_t seconds)
{
    // ESP's nano printf does not support %llu; keep the 64-bit counter intact.
    char *digit = text + 20;
    *digit = '\0';
    do {
        *--digit = '0' + seconds % 10;
        seconds /= 10;
    } while (seconds);
    return digit;
}
