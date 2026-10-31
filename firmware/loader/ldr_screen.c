/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The update loader's screen: "UPDATING", "DO NOT UNPLUG" (smaller) and a bar that fills as the app area is written
 * (ldr_progress), in the installer page's colours. The panel is started as the app's lcd.c does (fm1-base
 * device/lcd.c: reset, sleep out, RGB565, IPS inversion), polled, at Felucca's SPI1 rate (BAUD 4).
 * Part of loader.c's single unit; every buffer is in RAM (the SPI DMA reads RAM only). */
#include "../hal/fm1_lcd_hw.h"

#define SCR_BG 0xFFBDu                              /* #faf4e8 cream */
#define SCR_FG 0x49C5u                              /* #48392f brown */
#define SCR_BAR 0xBA43u                             /* #b84a1e coral */
#define SCR_TRACK 0xEEF9u                           /* #e8dcc8, the empty bar */
#define SCR_BAR_X 30u
#define SCR_BAR_Y 150u
#define SCR_BAR_W 180u
#define SCR_BAR_H 12u
#define SCR_ROWS 21u                                /* the tallest line: 5x7 glyphs at 3x */

static uint16_t scr_strip[240u * SCR_ROWS];         /* one line of text, byte-swapped (the panel takes MSB first) */
static uint8_t scr_small[8];
static uint32_t scr_shown;                          /* bar pixels drawn */
static uint8_t scr_on;

/* 5x7 glyphs, a row a byte (bit 4 = left) */
static const struct { char c; uint8_t rows[7]; } SCR_FONT[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
};

static void scr_cmd(uint8_t c)
{
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void scr_data(const void *p, uint32_t n)     /* n > 0 */
{
    fm1_lcd_send_data(p, n);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void scr_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    scr_small[0] = (uint8_t)(x0 >> 8); scr_small[1] = (uint8_t)x0;
    scr_small[2] = (uint8_t)(x1 >> 8); scr_small[3] = (uint8_t)x1;
    scr_cmd(0x2A);
    scr_data(scr_small, 4);
    scr_small[4] = (uint8_t)(y0 >> 8); scr_small[5] = (uint8_t)y0;
    scr_small[6] = (uint8_t)(y1 >> 8); scr_small[7] = (uint8_t)y1;
    scr_cmd(0x2B);
    scr_data(scr_small + 4, 4);
    scr_cmd(0x2C);
}

static uint16_t scr_swap(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

static void scr_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, n = w * h, k = 240u * SCR_ROWS;
    for (i = 0; i < k && i < n; i++)
        scr_strip[i] = scr_swap(c);
    scr_window(x, y, x + w - 1u, y + h - 1u);
    for (; n; n -= k) {
        if (k > n)
            k = n;
        scr_data(scr_strip, k * 2u);
    }
}

static void scr_text(uint32_t y, const char *s, uint32_t scale)   /* one line, centred; scale <= 3 */
{
    uint32_t len = 0, i, r, b, sx, sy, x0, w;
    while (s[len])
        len++;
    w = len * 6u * scale - scale;
    x0 = (240u - w) / 2u;
    for (i = 0; i < w * 7u * scale; i++)
        scr_strip[i] = scr_swap(SCR_BG);
    for (i = 0; i < len; i++) {
        const uint8_t *g = 0;
        uint32_t f;
        for (f = 0; f < sizeof SCR_FONT / sizeof SCR_FONT[0]; f++)
            if (SCR_FONT[f].c == s[i])
                g = SCR_FONT[f].rows;
        if (!g)
            continue;                               /* a space */
        for (r = 0; r < 7u; r++)
            for (b = 0; b < 5u; b++)
                if (g[r] & (0x10u >> b))
                    for (sy = 0; sy < scale; sy++)
                        for (sx = 0; sx < scale; sx++)
                            scr_strip[(r * scale + sy) * w + (i * 6u + b) * scale + sx] = scr_swap(SCR_FG);
    }
    scr_window(x0, y, x0 + w - 1u, y + 7u * scale - 1u);
    scr_data(scr_strip, w * 7u * scale * 2u);
}

static void ldr_screen_init(void)
{
    fm1_lcd_hw_init();                              /* pins, backlight on, SPI1 */
    fm1_delay_ms(20);
    scr_cmd(0x01);                                  /* SWRESET */
    fm1_delay_ms(150);
    scr_cmd(0x11);                                  /* SLPOUT */
    fm1_delay_ms(120);
    scr_cmd(0x3A);                                  /* COLMOD: RGB565 */
    scr_small[0] = 0x55;
    scr_data(scr_small, 1);
    scr_cmd(0x36);                                  /* MADCTL: top-left origin, RGB order */
    scr_small[0] = 0x00;
    scr_data(scr_small, 1);
    scr_cmd(0x21);                                  /* INVON: IPS panel */
    scr_cmd(0x13);                                  /* NORON */
    scr_fill(0, 0, 240, 240, SCR_BG);
    scr_text(76, "UPDATING", 3);
    scr_text(110, "DO NOT UNPLUG", 2);
    scr_fill(SCR_BAR_X, SCR_BAR_Y, SCR_BAR_W, SCR_BAR_H, SCR_TRACK);
    scr_cmd(0x29);                                  /* DISPON */
    scr_on = 1;
}

static void ldr_screen_progress(uint32_t done, uint32_t total)
{
    uint32_t px;
    if (!scr_on || !total)
        return;
    px = done >= total ? SCR_BAR_W : done * SCR_BAR_W / total;     /* < 2^32: total <= 2 MiB */
    if (px <= scr_shown)
        return;
    scr_fill(SCR_BAR_X + scr_shown, SCR_BAR_Y, px - scr_shown, SCR_BAR_H, SCR_BAR);
    scr_shown = px;
}
