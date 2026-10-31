/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The few libc routines the compiler may call implicitly (struct copies,
 * zeroing). Freestanding: no other libc. */
#include <stdint.h>

/* memset and memcpy move 32-bit words where they can. The data cache does
 * not sit in front of SRAM and a taken branch costs about 8 cycles, so a
 * byte loop costs 7 to 10 cycles a byte. Words are used only at aligned
 * addresses; the ends go byte by byte. The word type is may_alias because
 * a unity build can inline these into code that reaches the same memory
 * through other types. */
void *memset(void *d, int c, unsigned n)
{
    typedef unsigned int __attribute__((may_alias)) word_t;
    unsigned char *p = d;
    unsigned char b = (unsigned char)c;

    while (n && ((uintptr_t)p & 3)) {
        *p++ = b;
        n--;
    }
    if (n >= 4) {
        word_t w = b * 0x01010101u;
        word_t *wp = (word_t *)p;
        for (; n >= 16; n -= 16) {
            wp[0] = w;
            wp[1] = w;
            wp[2] = w;
            wp[3] = w;
            wp += 4;
        }
        for (; n >= 4; n -= 4)
            *wp++ = w;
        p = (unsigned char *)wp;
    }
    while (n--)
        *p++ = b;
    return d;
}

void *memcpy(void *d, const void *s, unsigned n)
{
    typedef unsigned int __attribute__((may_alias)) word_t;
    unsigned char *p = d;
    const unsigned char *q = s;

    /* Words only if both sides can be aligned together. */
    if (n >= 4 && !(((uintptr_t)p ^ (uintptr_t)q) & 3)) {
        word_t *wp;
        const word_t *wq;

        while ((uintptr_t)p & 3) {
            *p++ = *q++;
            n--;
        }
        wp = (word_t *)p;
        wq = (const word_t *)q;
        for (; n >= 16; n -= 16) {
            wp[0] = wq[0];
            wp[1] = wq[1];
            wp[2] = wq[2];
            wp[3] = wq[3];
            wp += 4;
            wq += 4;
        }
        for (; n >= 4; n -= 4)
            *wp++ = *wq++;
        p = (unsigned char *)wp;
        q = (const unsigned char *)wq;
    }
    while (n--)
        *p++ = *q++;
    return d;
}

int memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static unsigned str_len(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i + 1 < max) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

/* decimal with an optional fixed point: fmt_fix(buf, -63, 1) -> "-6.3" */
static void fmt_fix(char *b, int32_t v, int decimals)
{
    char t[12];
    int n = 0, neg = v < 0, i;
    uint32_t u = (uint32_t)(neg ? -v : v);
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
        if (n == decimals)
            t[n++] = '.';
    } while (u || n <= decimals);
    if (neg)
        t[n++] = '-';
    for (i = 0; i < n; i++)
        b[i] = t[n - 1 - i];
    b[n] = 0;
}

static void fmt_int(char *b, int32_t v) { fmt_fix(b, v, 0); }

static uint32_t rng_state = 0x1234567u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
