/*
 * app_worldclock.cpp — Landscape dot-matrix world clock
 *
 * Locked to landscape (448×368). Every character — digits and letters — is
 * drawn as a 5×7 LED dot matrix: dim "off" pixels for the empty grid, lit
 * pixels for the glyph (airport departure-board look).
 *
 * NTP sync at startup via WiFi (reads SSID/PASSWORD from setup.txt).
 * HOME_TIMEZONE selects which city's time is shown on boot.
 *
 * Controls:
 *   BOOT short — cycle to the next city's local time
 *   PWR  short — cycle brightness
 */

#include "app_worldclock.h"
#include "app_common.h"
#include <Arduino.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include <FS.h>
#include <time.h>
#include <strings.h>
#include <pgmspace.h>
#include "canvas/Arduino_Canvas.h"
#include "pin_config.h"
#include "HWCDC.h"
#include <Adafruit_XCA9554.h>
#include "font/glcdfont.h"     // 5×7 bitmap font (PROGMEM `font[]`)

extern USBCDC USBSerial;
extern Arduino_Canvas *g_canvas;

// ── Timezone table ──────────────────────────────────────────────────────────
// City names are uppercase to match the dot-matrix glyph set.
struct TzEntry {
    const char *city;
    const char *tz;
};

// One city per major UTC offset, ordered west-to-east. POSIX TZ strings
// include DST rules where the location observes DST (US, EU, Australia,
// New Zealand, Chile); offsets without DST use plain forms.
static const TzEntry TZS[] = {
    { "HONOLULU",    "HST10" },
    { "ANCHORAGE",   "AKST9AKDT,M3.2.0,M11.1.0" },
    { "L.A.",        "PST8PDT,M3.2.0,M11.1.0" },
    { "DENVER",      "MST7MDT,M3.2.0,M11.1.0" },
    { "CHICAGO",     "CST6CDT,M3.2.0,M11.1.0" },
    { "NEW YORK",    "EST5EDT,M3.2.0,M11.1.0" },
    { "SANTIAGO",    "<-04>4<-03>,M9.1.6/24,M4.1.6/24" },
    { "SAO PAULO",   "<-03>3" },
    { "LONDON",      "GMT0BST,M3.5.0/1,M10.5.0" },
    { "BERLIN",      "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "CAIRO",       "EET-2" },
    { "ATHENS",      "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "MOSCOW",      "MSK-3" },
    { "DUBAI",       "GST-4" },
    { "MUMBAI",      "IST-5:30" },
    { "DHAKA",       "BST-6" },
    { "BANGKOK",     "ICT-7" },
    { "SHANGHAI",    "CST-8" },
    { "TOKYO",       "JST-9" },
    { "SYDNEY",      "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "AUCKLAND",    "NZST-12NZDT,M9.5.0,M4.1.0/3" },
};
static const int NUM_TZS = (int)(sizeof(TZS) / sizeof(TZS[0]));

// ── Config ───────────────────────────────────────────────────────────────────
static char     cfg_ssid[3][64] = {};
static char     cfg_pass[3][64] = {};
static char     cfg_home[32]    = "BERLIN";    // HOME_TIMEZONE → matched against TZS[].city
static bool     cfg_color_set   = false;
static uint16_t cfg_color       = 0xFFFF;

// ── State ────────────────────────────────────────────────────────────────────
static Arduino_Canvas *canvas = nullptr;
static Arduino_SH8601 *s_gfx = nullptr;
static int  s_prevMin  = -1;
static int  s_prevHour = -1;
static int  s_prevDay  = -1;
static int  s_prevTzIdx = -2;
static uint32_t s_lastCheck = 0;
static bool     s_bootWas   = false;
static int      s_tzIdx     = 9;   // default to BERLIN (TZS[9]; resolved properly in setup)
static const int DEFAULT_TZ_IDX = 9;
static uint8_t  s_brightIdx = 0;
static const uint8_t BRIGHT_LEVELS[] = { 255, 180, 100, 40, 10 };

// Landscape only.
static const uint8_t S_ROT = 1;

// ── Departure-board palette ──────────────────────────────────────────────────
#define COL_BG       0x0000
#define DOT_OFF      0x18C3
#define DOT_YELLOW   0xFE60
#define DOT_WHITE    0xFFFF
#define COL_WHITE    0xFFFF
#define COL_GREY     0x7BEF

// ── Weekday / month names ────────────────────────────────────────────────────
static const char *WDAY[] = {
    "SUNDAY","MONDAY","TUESDAY","WEDNESDAY","THURSDAY","FRIDAY","SATURDAY"
};
static const char *MON[] = {
    "JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE",
    "JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"
};

// ── Hex color parser → RGB565 ────────────────────────────────────────────────
static uint16_t parseHexColor(const char *s) {
    if (!s || !*s) return 0xFFFF;
    if (*s == '#') s++;
    uint32_t v = 0;
    int digits = 0;
    while (*s && digits < 6) {
        char c = *s++;
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * 16 + d;
        digits++;
    }
    if (digits < 6) return 0xFFFF;
    uint8_t r = (v >> 16) & 0xFF;
    uint8_t g = (v >> 8) & 0xFF;
    uint8_t b = v & 0xFF;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// ── Config parser ────────────────────────────────────────────────────────────
static bool extractVal(const char *line, const char *key, char *out, size_t cap) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    size_t kl = strlen(key);
    if (strncmp(p, key, kl) != 0) return false;
    char after = p[kl];
    if (isalnum((unsigned char)after) || after == '_') return false;
    p += kl;
    while (*p == ' ' || *p == '=') p++;
    if (*p == '"') p++;
    size_t n = 0;
    while (*p && *p != '"' && *p != '\n' && *p != '\r' && n < cap - 1)
        out[n++] = *p++;
    out[n] = '\0';
    return n > 0;
}

static bool readConfig() {
    File f = SD_MMC.open("/setup/setup.txt");
    if (!f) return false;
    char line[160];
    while (f.available()) {
        int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        extractVal(line, "SSID",          cfg_ssid[0], 64);
        extractVal(line, "PASSWORD",      cfg_pass[0], 64);
        extractVal(line, "SSID2",         cfg_ssid[1], 64);
        extractVal(line, "PASSWORD2",     cfg_pass[1], 64);
        extractVal(line, "SSID3",         cfg_ssid[2], 64);
        extractVal(line, "PASSWORD3",     cfg_pass[2], 64);
        extractVal(line, "HOME_TIMEZONE", cfg_home, sizeof(cfg_home));
        char colorBuf[16] = {};
        if (extractVal(line, "CLOCK_COLOR", colorBuf, 16)) {
            cfg_color = parseHexColor(colorBuf);
            cfg_color_set = true;
        }
    }
    f.close();
    return cfg_ssid[0][0] != '\0';
}

// Resolve cfg_home → s_tzIdx. Case-insensitive city match; fall back to
// DEFAULT_TZ_IDX if no match.
static void resolveHomeIdx() {
    for (int i = 0; i < NUM_TZS; i++) {
        if (strcasecmp(cfg_home, TZS[i].city) == 0) {
            s_tzIdx = i;
            return;
        }
    }
    USBSerial.printf("[worldclock] HOME_TIMEZONE '%s' unknown, defaulting to %s\n",
                     cfg_home, TZS[DEFAULT_TZ_IDX].city);
    s_tzIdx = DEFAULT_TZ_IDX;
}

static void applyTz() {
    setenv("TZ", TZS[s_tzIdx].tz, 1);
    tzset();
}

// ── Status splash ────────────────────────────────────────────────────────────
static void showStatus(const char *l1, const char *l2 = nullptr) {
    canvas->setFont();
    canvas->fillScreen(COL_BG);
    canvas->setTextColor(COL_WHITE); canvas->setTextSize(2);
    canvas->setCursor(16, LCD_HEIGHT / 2 - 16); canvas->print(l1);
    if (l2) {
        canvas->setTextColor(COL_GREY);
        canvas->setCursor(16, LCD_HEIGHT / 2 + 12); canvas->print(l2);
    }
    canvas->flush();
}

// ── WiFi + NTP sync ──────────────────────────────────────────────────────────
static bool syncTime() {
    showStatus("Connecting WiFi...", cfg_ssid[0][0] ? cfg_ssid[0] : "");
    WifiCred list[3] = {
        { cfg_ssid[0], cfg_pass[0] },
        { cfg_ssid[1], cfg_pass[1] },
        { cfg_ssid[2], cfg_pass[2] },
    };
    if (wifi_try_connect(list, 3) < 0) return false;

    showStatus("Syncing NTP...");
    configTzTime(TZS[s_tzIdx].tz, "pool.ntp.org", "time.nist.gov");
    applyTz();
    delay(2000);

    struct tm ti = {};
    bool ok = false;
    for (int t = 0; t < 15; t++) {
        if (getLocalTime(&ti, 1000) && ti.tm_year > 100) { ok = true; break; }
        delay(500);
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    USBSerial.printf("[worldclock] NTP sync %s: %d-%02d-%02d %02d:%02d (%s)\n",
        ok ? "OK" : "FAIL", ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
        ti.tm_hour, ti.tm_min, TZS[s_tzIdx].city);
    return ok;
}

// ── Dot-matrix row plumbing (cribbed from clock app) ─────────────────────────
#define CELL_COLS  5
#define CELL_ROWS  7
#define CELL_GAP   1

static int16_t pitchForCells(int n, int16_t maxW, int16_t maxPitch, int16_t minPitch) {
    int16_t cellSpan = n * (CELL_COLS + CELL_GAP) - CELL_GAP;
    if (cellSpan <= 0) cellSpan = 1;
    for (int16_t p = maxPitch; p >= minPitch; p--) {
        if (cellSpan * p <= maxW) return p;
    }
    return minPitch;
}

// Cap month names at MONTH_MAX_LEN; longer names render as <first 4 letters>.
// Today that only catches SEPTEMBER → SEPT.; FEBRUARY/NOVEMBER/DECEMBER are 8
// chars and stay full.
#define MONTH_MAX_LEN 8

static const char *shortMonth(const char *m, char *buf, size_t bufLen) {
    if ((int)strlen(m) <= MONTH_MAX_LEN) return m;
    snprintf(buf, bufLen, "%.4s.", m);
    return buf;
}

static int16_t drawDotRow(int16_t y, int16_t pitch,
                          int16_t rOn, int16_t rOff,
                          uint16_t onCol, uint16_t offCol,
                          const char *text, int16_t W) {
    int16_t halfP = pitch / 2;
    int16_t dotCols    = W / pitch;
    int16_t totalCells = (dotCols + CELL_GAP) / (CELL_COLS + CELL_GAP);
    if (totalCells < 1) totalCells = 1;
    int16_t cellSpan   = totalCells * (CELL_COLS + CELL_GAP) - CELL_GAP;
    int16_t marginPx   = (W - cellSpan * pitch) / 2;
    int16_t baseX      = marginPx + halfP;

    if (rOff >= 1) {
        for (int cell = 0; cell < totalCells; cell++) {
            int16_t cellX = baseX + cell * (CELL_COLS + CELL_GAP) * pitch;
            for (int row = 0; row < CELL_ROWS; row++) {
                int16_t dy = y + row * pitch + halfP;
                for (int col = 0; col < CELL_COLS; col++) {
                    int16_t dx = cellX + col * pitch;
                    canvas->fillCircle(dx, dy, rOff, offCol);
                }
            }
        }
    }

    int n = (int)strlen(text);
    if (n > totalCells) n = totalCells;
    for (int i = 0; i < n; i++) {
        unsigned char uc = (unsigned char)text[i];
        if (uc < 32 || uc > 126) uc = ' ';
        const uint8_t *g = font + uc * 5;
        int16_t cellX = baseX + i * (CELL_COLS + CELL_GAP) * pitch;
        for (int col = 0; col < 5; col++) {
            uint8_t b = pgm_read_byte(g + col);
            for (int row = 0; row < 7; row++) {
                if ((b >> row) & 1) {
                    int16_t dx = cellX + col * pitch;
                    int16_t dy = y + row * pitch + halfP;
                    canvas->fillCircle(dx, dy, rOn, onCol);
                }
            }
        }
    }
    return CELL_ROWS * pitch;
}

static inline int16_t dotR(int16_t pitch) {
    int16_t r = (pitch * 3) / 10;
    return r < 1 ? 1 : r;
}

// ── Full clock face — 4 rows: time / weekday / date / city ───────────────────
static void drawClock(struct tm &ti) {
    canvas->setRotation(S_ROT);
    int16_t W = 448;
    int16_t H = 368;

    canvas->fillScreen(COL_BG);

    char tStr[8];
    snprintf(tStr, sizeof(tStr), "%02d:%02d", ti.tm_hour, ti.tm_min);
    const char *wkStr = WDAY[ti.tm_wday];
    char monBuf[16];
    const char *monStr = shortMonth(MON[ti.tm_mon], monBuf, sizeof(monBuf));
    char dStr[20];
    snprintf(dStr, sizeof(dStr), "%02d %s", ti.tm_mday, monStr);
    const char *cityStr = TZS[s_tzIdx].city;

    int longest = (int)strlen(tStr);
    if ((int)strlen(wkStr)   > longest) longest = (int)strlen(wkStr);
    if ((int)strlen(dStr)    > longest) longest = (int)strlen(dStr);
    if ((int)strlen(cityStr) > longest) longest = (int)strlen(cityStr);
    int16_t pitch = pitchForCells(longest, W, 8, 3);
    int16_t r     = dotR(pitch);
    int16_t rowH  = CELL_ROWS * pitch;
    int16_t rowGap = pitch;

    uint16_t colTime    = cfg_color_set ? cfg_color : DOT_WHITE;
    uint16_t colWeekday = cfg_color_set ? cfg_color : DOT_YELLOW;
    uint16_t colDate    = cfg_color_set ? cfg_color : DOT_YELLOW;
    uint16_t colCity    = cfg_color_set ? cfg_color : DOT_WHITE;

    int16_t totalH    = 4 * rowH + 3 * rowGap;
    int16_t topMargin = 24;
    int16_t y         = topMargin;
    int16_t spare = H - topMargin - totalH - 36;
    if (spare > 0) y += spare / 2;

    drawDotRow(y, pitch, r, r, colTime,    DOT_OFF, tStr,    W); y += rowH + rowGap;
    drawDotRow(y, pitch, r, r, colWeekday, DOT_OFF, wkStr,   W); y += rowH + rowGap;
    drawDotRow(y, pitch, r, r, colDate,    DOT_OFF, dStr,    W); y += rowH + rowGap;
    drawDotRow(y, pitch, r, r, colCity,    DOT_OFF, cityStr, W);

    canvas->setFont();
    draw_battery_g(canvas, W, H);
    draw_watermark_g(canvas, W, H);
    draw_pill_label(canvas, S_ROT, 0, "tz");
    draw_pill_label(canvas, S_ROT, 1, "dim");
    canvas->flush();
}

// ── App entry points ─────────────────────────────────────────────────────────
void app_worldclock_setup(Arduino_SH8601 *gfx) {
    s_gfx   = gfx;
    canvas  = g_canvas;
    s_prevMin   = -1;
    s_prevHour  = -1;
    s_prevDay   = -1;
    s_prevTzIdx = -2;
    s_lastCheck = 0;
    s_bootWas   = false;
    s_brightIdx = 0;
    memset(cfg_ssid, 0, sizeof(cfg_ssid));
    memset(cfg_pass, 0, sizeof(cfg_pass));
    strncpy(cfg_home, "BERLIN", sizeof(cfg_home));
    pinMode(0, INPUT_PULLUP);

    Adafruit_XCA9554 expander;
    if (!expander.begin(0x20)) USBSerial.println("XCA9554 init failed");
    expander.pinMode(1, OUTPUT); expander.digitalWrite(1, LOW);
    expander.pinMode(2, OUTPUT); expander.digitalWrite(2, LOW);
    delay(20);
    expander.digitalWrite(1, HIGH);
    expander.digitalWrite(2, HIGH);

    showStatus("World Clock", "Reading config...");

    SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
    if (SD_MMC.begin("/sdcard", true)) {
        readConfig();
        SD_MMC.end();
    }

    resolveHomeIdx();
    applyTz();

    if (cfg_ssid[0][0]) {
        if (!syncTime())
            showStatus("NTP sync failed", "Using system time");
    }

    struct tm ti;
    if (getLocalTime(&ti, 1000)) {
        s_prevMin  = ti.tm_min;
        s_prevHour = ti.tm_hour;
        s_prevDay  = ti.tm_mday;
        s_prevTzIdx = s_tzIdx;
        drawClock(ti);
    } else {
        showStatus("No time available", "Check WiFi config");
    }
}

void app_worldclock_loop() {
    common_tick();
    uint32_t now = millis();

    if (now - s_lastCheck >= 500) {
        s_lastCheck = now;
        struct tm ti;
        if (getLocalTime(&ti, 50)) {
            if (ti.tm_min != s_prevMin || ti.tm_hour != s_prevHour ||
                ti.tm_mday != s_prevDay || s_tzIdx != s_prevTzIdx) {
                s_prevMin   = ti.tm_min;
                s_prevHour  = ti.tm_hour;
                s_prevDay   = ti.tm_mday;
                s_prevTzIdx = s_tzIdx;
                drawClock(ti);
            }
        }
    }

    bool boot = (digitalRead(0) == LOW);
    if (boot && !s_bootWas) {
        common_activity();
        s_tzIdx = (s_tzIdx + 1) % NUM_TZS;
        applyTz();
        // Force redraw with the new TZ.
        s_prevMin = -1;
        struct tm ti;
        if (getLocalTime(&ti, 50)) {
            s_prevMin   = ti.tm_min;
            s_prevHour  = ti.tm_hour;
            s_prevDay   = ti.tm_mday;
            s_prevTzIdx = s_tzIdx;
            drawClock(ti);
        }
    }
    s_bootWas = boot;

    if (common_consume_pwr_short()) {
        common_activity();
        s_brightIdx = (s_brightIdx + 1) % (sizeof(BRIGHT_LEVELS));
        s_gfx->setBrightness(BRIGHT_LEVELS[s_brightIdx]);
    }

    delay(50);
}
