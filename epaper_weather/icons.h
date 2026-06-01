#pragma once
#include "epd_driver.h"
#include <math.h>
#include <string.h>

// Draw a cloud shape approximately centered at (cx, cy), sized relative to s.
static void _cloud(int32_t cx, int32_t cy, int32_t s, uint8_t *fb) {
    int u = s / 14;
    if (u < 1) u = 1;
    epd_fill_circle(cx - u*2, cy + u,   u*3,   0x00, fb);
    epd_fill_circle(cx + u,   cy - u,   u*4,   0x00, fb);
    epd_fill_circle(cx + u*4, cy + u/2, u*3,   0x00, fb);
    epd_fill_rect(cx - u*5, cy + u, u*12, u*3, 0x00, fb);
}

static void _sun(int32_t cx, int32_t cy, int32_t s, int32_t body_pct, int32_t inner_pct, int32_t outer_pct, uint8_t *fb) {
    int r  = s * body_pct  / 100;
    int r1 = s * inner_pct / 100;
    int r2 = s * outer_pct / 100;
    epd_fill_circle(cx, cy, r, 0x00, fb);
    for (int a = 0; a < 360; a += 45) {
        float rad = a * (float)M_PI / 180.0f;
        epd_draw_line(cx + (int)(cosf(rad) * r1), cy + (int)(sinf(rad) * r1),
                      cx + (int)(cosf(rad) * r2), cy + (int)(sinf(rad) * r2),
                      0x00, fb);
    }
}

// Draw a weather icon centered at (cx, cy) within a bounding box of size x size.
void drawWeatherIcon(const char *cond, int32_t cx, int32_t cy, int32_t size, uint8_t *fb) {
    int s = size;

    if (strcmp(cond, "sunny") == 0 || strcmp(cond, "clear-day") == 0) {
        _sun(cx, cy, s, 22, 27, 42, fb);
        return;
    }

    if (strcmp(cond, "clear-night") == 0) {
        int r = s * 28 / 100;
        epd_fill_circle(cx, cy, r, 0x00, fb);
        epd_fill_circle(cx - s*8/100, cy - s*5/100, r*7/8, 0xFF, fb);
        return;
    }

    if (strcmp(cond, "cloudy") == 0) {
        _cloud(cx, cy, s, fb);
        return;
    }

    if (strcmp(cond, "partlycloudy") == 0 || strcmp(cond, "partlycloudy-day") == 0) {
        _sun(cx + s*16/100, cy - s*16/100, s, 12, 15, 22, fb);
        _cloud(cx - s*8/100, cy + s*10/100, s * 70 / 100, fb);
        return;
    }

    if (strcmp(cond, "partlycloudy-night") == 0) {
        int r = s * 15 / 100;
        epd_fill_circle(cx + s*18/100, cy - s*16/100, r, 0x00, fb);
        epd_fill_circle(cx + s*10/100, cy - s*21/100, r*7/8, 0xFF, fb);
        _cloud(cx - s*8/100, cy + s*10/100, s * 70 / 100, fb);
        return;
    }

    if (strcmp(cond, "rainy") == 0) {
        _cloud(cx, cy - s*12/100, s, fb);
        for (int i = -2; i <= 2; i++) {
            int rx = cx + i * s*11/100;
            epd_draw_line(rx, cy + s*12/100, rx - s*5/100, cy + s*44/100, 0x00, fb);
        }
        return;
    }

    if (strcmp(cond, "lightning-rainy") == 0 || strcmp(cond, "lightning") == 0) {
        _cloud(cx, cy - s*15/100, s, fb);
        int bx = cx - s*4/100, by = cy + s*8/100;
        epd_draw_line(bx + s*5/100, by,            bx,            by + s*16/100, 0x00, fb);
        epd_draw_line(bx,           by + s*16/100,  bx + s*6/100, by + s*32/100, 0x00, fb);
        epd_draw_line(cx - s*22/100, by, cx - s*26/100, by + s*28/100, 0x00, fb);
        epd_draw_line(cx + s*18/100, by, cx + s*14/100, by + s*28/100, 0x00, fb);
        return;
    }

    if (strcmp(cond, "snowy") == 0 || strcmp(cond, "snowy-rainy") == 0) {
        _cloud(cx, cy - s*12/100, s, fb);
        int dot_r = s*3/100 + 1;
        int sy = cy + s*16/100;
        for (int col = -2; col <= 2; col++)
            epd_fill_circle(cx + col * s*11/100, sy, dot_r, 0x00, fb);
        for (int col = -1; col <= 1; col++)
            epd_fill_circle(cx + col * s*11/100, sy + s*16/100, dot_r, 0x00, fb);
        return;
    }

    if (strcmp(cond, "fog") == 0) {
        int widths[] = {80, 65, 85, 55, 70};
        for (int i = 0; i < 5; i++) {
            int lw = s * widths[i] / 100;
            epd_draw_hline(cx - lw/2, cy - s*24/100 + i * s*12/100, lw, 0x00, fb);
        }
        return;
    }

    if (strcmp(cond, "windy") == 0 || strcmp(cond, "windy-variant") == 0) {
        int widths[] = {80, 60, 75, 50};
        for (int i = 0; i < 4; i++) {
            int lw = s * widths[i] / 100;
            int ly = cy - s*20/100 + i * s*14/100;
            for (int x = 0; x < lw; x += 2) {
                int xi = cx - lw/2 + x;
                int dy = (int)(sinf(x * (float)M_PI / lw) * s*3/100);
                epd_draw_pixel(xi, ly + dy,   0x00, fb);
                epd_draw_pixel(xi, ly + dy+1, 0x00, fb);
            }
        }
        return;
    }

    if (strcmp(cond, "hail") == 0) {
        _cloud(cx, cy - s*12/100, s, fb);
        int hr = s*5/100 + 1;
        int hy = cy + s*20/100;
        epd_fill_circle(cx - s*20/100, hy,            hr, 0x00, fb);
        epd_fill_circle(cx,            hy,            hr, 0x00, fb);
        epd_fill_circle(cx + s*20/100, hy,            hr, 0x00, fb);
        epd_fill_circle(cx - s*10/100, hy + s*16/100, hr, 0x00, fb);
        epd_fill_circle(cx + s*10/100, hy + s*16/100, hr, 0x00, fb);
        return;
    }

    // exceptional or unknown: exclamation mark
    epd_fill_rect(cx - s*4/100, cy - s*28/100, s*8/100, s*38/100, 0x00, fb);
    epd_fill_circle(cx, cy + s*20/100, s*6/100, 0x00, fb);
}
