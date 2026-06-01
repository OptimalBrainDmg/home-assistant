#pragma once
#include "epd_driver.h"
#include "mdi-icons/mdi_weather_large.h"
#include "mdi-icons/mdi_weather_small.h"

// MDI glyph UTF-8 literals (Supplementary Private Use Area-B, 4-byte UTF-8)
#define MDI_WEATHER_NIGHT                        "\xF3\xB0\x96\x94"
#define MDI_WEATHER_CLOUDY                       "\xF3\xB0\x96\x90"
#define MDI_WEATHER_CLOUDY_ALERT                 "\xF3\xB0\xBC\xAF"
#define MDI_WEATHER_FOG                          "\xF3\xB0\x96\x91"
#define MDI_WEATHER_HAIL                         "\xF3\xB0\x96\x92"
#define MDI_WEATHER_LIGHTNING                    "\xF3\xB0\x96\x93"
#define MDI_WEATHER_LIGHTNING_RAINY              "\xF3\xB0\x99\xBE"
#define MDI_WEATHER_PARTLY_CLOUDY                "\xF3\xB0\x96\x95"
#define MDI_WEATHER_POURING                      "\xF3\xB0\x96\x96"
#define MDI_WEATHER_RAINY                        "\xF3\xB0\x96\x97"
#define MDI_WEATHER_SNOWY                        "\xF3\xB0\x96\x98"
#define MDI_WEATHER_SNOWY_RAINY                  "\xF3\xB0\x99\xBF"
#define MDI_WEATHER_SUNNY                        "\xF3\xB0\x96\x99"
#define MDI_WEATHER_WINDY                        "\xF3\xB0\x96\x9D"
#define MDI_WEATHER_WINDY_VARIANT                "\xF3\xB0\x96\x9E"

static const char *condToMdi(const char *cond) {
    if (strcmp(cond, "sunny")               == 0) return MDI_WEATHER_SUNNY;
    if (strcmp(cond, "clear-day")           == 0) return MDI_WEATHER_SUNNY;
    if (strcmp(cond, "clear-night")         == 0) return MDI_WEATHER_NIGHT;
    if (strcmp(cond, "cloudy")              == 0) return MDI_WEATHER_CLOUDY;
    if (strcmp(cond, "partlycloudy")        == 0) return MDI_WEATHER_PARTLY_CLOUDY;
    if (strcmp(cond, "partlycloudy-day")    == 0) return MDI_WEATHER_PARTLY_CLOUDY;
    if (strcmp(cond, "partlycloudy-night")  == 0) return MDI_WEATHER_PARTLY_CLOUDY;
    if (strcmp(cond, "rainy")               == 0) return MDI_WEATHER_RAINY;
    if (strcmp(cond, "pouring")             == 0) return MDI_WEATHER_POURING;
    if (strcmp(cond, "lightning-rainy")     == 0) return MDI_WEATHER_LIGHTNING_RAINY;
    if (strcmp(cond, "lightning")           == 0) return MDI_WEATHER_LIGHTNING;
    if (strcmp(cond, "snowy")               == 0) return MDI_WEATHER_SNOWY;
    if (strcmp(cond, "snowy-rainy")         == 0) return MDI_WEATHER_SNOWY_RAINY;
    if (strcmp(cond, "fog")                 == 0) return MDI_WEATHER_FOG;
    if (strcmp(cond, "windy")               == 0) return MDI_WEATHER_WINDY;
    if (strcmp(cond, "windy-variant")       == 0) return MDI_WEATHER_WINDY_VARIANT;
    if (strcmp(cond, "hail")                == 0) return MDI_WEATHER_HAIL;
    if (strcmp(cond, "exceptional")         == 0) return MDI_WEATHER_CLOUDY_ALERT;
    return MDI_WEATHER_CLOUDY_ALERT;
}

// Draw an MDI weather icon centered at (cx, cy). Selects font by size.
static void drawMdiIcon(const char *cond, int32_t cx, int32_t cy, int32_t size, uint8_t *fb) {
    const GFXfont *font = (size >= 80)
        ? (const GFXfont *)&MdiWeather
        : (const GFXfont *)&MdiWeatherSmall;
    const char *glyph = condToMdi(cond);
    int32_t x = 0, y = 0, x1, y1, w, h;
    get_text_bounds(font, glyph, &x, &y, &x1, &y1, &w, &h, nullptr);
    int32_t cursor_x = cx - x1 - w / 2;
    int32_t cursor_y = cy + y1 + h / 2;
    write_mode(font, glyph, &cursor_x, &cursor_y, fb, BLACK_ON_WHITE, nullptr);
}
