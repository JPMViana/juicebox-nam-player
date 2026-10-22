/* SPDX-License-Identifier: GPL-3.0-only
 * Juice-Box by João Viana: NAM player firmware for the M-VAVE ANN BlackBox (JieLi AC791N).
 * (Working name until v0.1.2: armaria-nam.)
 *
 * First own firmware: USB-MIDI with the M-UPGRADE update entry first, then the
 * screen with the boot image. No audio, no inputs yet. Interrupts stay off:
 * USB is polled from the main loop, as in Felucca's update loader.
 *
 * Safety (docs/04-seguranca.md):
 *  - USB and the update entry come up before anything else, so a package
 *    (back to the stock V20 included) can always be installed over USB.
 *  - Rescue: the official JieLi update loader is kept staged in flash at
 *    OTA_AREA. Two failed boots in a row (watchdog resets before 30 s of
 *    stable running) start that loader through the SPL's RAM update record:
 *    it is a plain USB-MIDI updater that tools/bb_install.py talks to.
 *    (v0.1 also counted "bus reset seen, never configured" as a failure: with
 *    no cable the floating D+/D- read as bus resets, so on battery the pedal
 *    went to the rescue loader after 30 s. Removed in v0.1.2.)
 *  - Never UBOOT (the mask-ROM mode needs extra hardware on a Mac).
 *
 * Built from Felucca (github.com/hugelton/Felucca, GPL-3.0): fw/felucca/ holds
 * the HAL, USB and update code we reuse. Single compilation unit. */
#include <stdint.h>

#define JB_NAME "Juice-Box"
#define JB_VERSION "v0.7"
#define FELUCCA_LOADER 1                 /* usb.c: the polled, MIDI-only build of Felucca's loader */
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_OTA_RAMONLY 1            /* RAM update record only: no flash record at a location
                                          * the BlackBox SPL was never seen to scan */
#define FELUCCA_USB_PID 0x0001
#ifndef FELUCCA_ID
#define FELUCCA_ID "BlackBox_914"        /* handshake identity == package marker (fw/build.py) */
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

/* BlackBox flash map (docs/01): app area 0x4000..0xA9000 (flash.bin = 0xA5000), VM 0xA9000..0xC2000,
 * BTIF 0xC2000, USR 0xC3000.., key_mac 0xFF000. The loader is staged near the end of VM, as the stock
 * firmware and Felucca do on the FM-1 (whose VM ends at 0xE8000). */
#define OTA_AREA 0x000BA000u
#define OTA_AREA_LEN 0x5000u
#define OTA_FL_LEN 0x000A5000u
#define OTA_LOADER_ID "ota-BlackBox_914"

#include "fm1_time.h"
#include "fm1_sys.h"
#define JB_LOG 0x000B9000u                       /* boot log sector, just below the staged loader */
#define FL_RANGE_OK(off, n) FL_IN(off, n, JB_LOG, OTA_AREA + OTA_AREA_LEN)
#include "fm1_flash.h"
#include "libc.c"

#define USB_STRINGS_DEFINED
static const uint8_t STR1[] = {22, 3, 'J', 0, 'o', 0, 0xE3, 0, 'o', 0, ' ', 0, 'V', 0, 'i', 0, 'a', 0, 'n', 0,
                               'a', 0};                               /* manufacturer: the author */
static const uint8_t STR2[] = {20, 3, 'J', 0, 'u', 0, 'i', 0, 'c', 0, 'e', 0, '-', 0, 'B', 0, 'o', 0, 'x', 0};
#include "usb.c"

#include "splash.h"
#include "font.h"
#include "loader.h"

/* ------------------------------------------------------------------ time --- */
static uint32_t now_ms(void)                    /* monotonic; survives the TIMER4 wrap */
{
    static uint32_t last, acc, ms;
    uint32_t now = fm1_ticks();
    acc += now - last;
    last = now;
    ms += acc / (1000u * FM1_TICKS_PER_US);
    acc %= 1000u * FM1_TICKS_PER_US;
    return ms;
}

static void audio_service(void);
static void led_pwm_tick(void);
static uint8_t led_on;

static void poll(void)                          /* watchdog, USB at ~2 kHz, audio halves, LED PWM */
{
    audio_service();
    if (led_on)
        led_pwm_tick();
    static uint32_t last;
    uint32_t now = fm1_ticks();
    fm1_wdt_feed();
    if (usb.up && now - last >= 500u * FM1_TICKS_PER_US) {   /* (rescue checks at boot run before usb_start) */
        last = now;
        usb_poll();
    }
}

static void delay_poll(uint32_t ms)
{
    uint32_t t0 = now_ms();
    while (now_ms() - t0 < ms)
        poll();
}

/* --------------------------------------------------------------- display --- */
/* ST7789 240x240 on SPI1 port A (docs/06-analise-firmware.md): SCK PB6, data PB7, D/C PB5, CS PB1,
 * PC7 low (backlight or reset: the stock firmware drives it low first). The visible area is rows 0..239
 * of the 240x320 controller RAM (seen on the pedal with v0.1; the stock init table's RASET 40..279 is
 * only a start-up window). Pin and register sequence copied from the stock init (0x0202A658). */
#define PB_OUT (*(volatile uint32_t *)0x50040u)
#define PB_DIR (*(volatile uint32_t *)0x50048u)
#define PC_OUT (*(volatile uint32_t *)0x50080u)
#define PC_DIR (*(volatile uint32_t *)0x50088u)
#define SPI1_CON (*(volatile uint32_t *)0x11D00u)
#define SPI1_BAUD (*(volatile uint32_t *)0x11D04u)
#define SPI1_BUF (*(volatile uint32_t *)0x11D08u)
#define SPI1_ADR (*(volatile uint32_t *)0x11D0Cu)
#define SPI1_CNT (*(volatile uint32_t *)0x11D10u)
#define LCD_CS (1u << 1)
#define LCD_DC (1u << 5)
#define LCD_ROW0 0u
#define LCD_RAM_ROWS 320u
#define LCD_W 240u
#define LCD_H 240u

static uint32_t lcd_timeouts;
static uint8_t lcd_buf[LCD_W * 2u * 2u] __attribute__((aligned(4)));   /* two rows, RAM: the SPI DMA source */

static void lcd_wait(void)
{
    uint32_t n;
    for (n = 0; n < 2000000u && !(SPI1_CON & 0x8000u); n++)
        ;
    if (n == 2000000u)
        lcd_timeouts++;
    SPI1_CON |= 0x4000u;
}

static void lcd_cmd(uint8_t c)
{
    PB_OUT &= ~LCD_DC;
    PB_OUT &= ~LCD_CS;
    SPI1_CON |= 0x4000u;
    SPI1_BUF = c;
    lcd_wait();
    PB_OUT |= LCD_CS;
}

static void lcd_data(const uint8_t *ram, uint32_t n)
{
    if (!n)
        return;
    PB_OUT |= LCD_DC;
    PB_OUT &= ~LCD_CS;
    SPI1_CON |= 0x4000u;
    SPI1_ADR = (uint32_t)ram;
    SPI1_CNT = n;
    lcd_wait();
    PB_OUT |= LCD_CS;
}

static uint8_t lcd_small[8] __attribute__((aligned(4)));

static void lcd_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    y0 += LCD_ROW0;
    y1 += LCD_ROW0;
    lcd_small[0] = (uint8_t)(x0 >> 8);
    lcd_small[1] = (uint8_t)x0;
    lcd_small[2] = (uint8_t)(x1 >> 8);
    lcd_small[3] = (uint8_t)x1;
    lcd_cmd(0x2A);
    lcd_data(lcd_small, 4);
    lcd_small[4] = (uint8_t)(y0 >> 8);
    lcd_small[5] = (uint8_t)y0;
    lcd_small[6] = (uint8_t)(y1 >> 8);
    lcd_small[7] = (uint8_t)y1;
    lcd_cmd(0x2B);
    lcd_data(lcd_small + 4, 4);
    lcd_cmd(0x2C);
}

/* the stock init table (0x0205CE60): command, data length, data; 0xFF = delay of data[0] ms */
static const uint8_t LCD_INIT[] = {
    0x01, 0, 0xFF, 1, 120,
    0x11, 0, 0xFF, 1, 120,
    0x2A, 4, 0x00, 0x00, 0x00, 0xEF,
    0x2B, 4, 0x00, 0x28, 0x01, 0x17,
    0xB2, 5, 0x0C, 0x0C, 0x0C, 0x00, 0x33,
    0x20, 0,
    0xB7, 1, 0x56,
    0xBB, 1, 0x18,
    0xC0, 1, 0x2C,
    0xC2, 1, 0x01,
    0xC3, 1, 0x1F,
    0xC4, 1, 0x20,
    0xC6, 1, 0x0F,
    0xD0, 2, 0xA6, 0xA1,
    0xE0, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,
    0xE1, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,
    0x36, 1, 0x00,
    0x3A, 1, 0x55,
    0xE7, 1, 0x00,
    0x51, 1, 0xFF,
    0x21, 0,
};

static void lcd_init(void)
{
    uint32_t i, n, k;
    PC_OUT &= ~(1u << 7);                         /* gpio_direction_output(PC7, 0) */
    PC_DIR &= ~(1u << 7);
    PB_OUT &= ~LCD_CS;                            /* gpio_direction_output(PB1, 0) */
    PB_DIR &= ~LCD_CS;
    PB_DIR &= ~LCD_DC;                            /* gpio_set_direction(PB5, out) */
    PB_DIR &= ~(1u << 6);                         /* gpio_set_direction(PB6, out) */
    PB_DIR |= 1u << 7;                            /* gpio_set_direction(PB7, in), as stock */
    SPI1_CON = 0x4021u;
    SPI1_BAUD = 1u;
    delay_poll(50);
    for (i = 0; i < sizeof LCD_INIT; i += 2u + n) {
        n = LCD_INIT[i + 1];
        if (LCD_INIT[i] == 0xFFu) {
            delay_poll(LCD_INIT[i + 2]);
            continue;
        }
        lcd_cmd(LCD_INIT[i]);
        for (k = 0; k < n; k++)
            lcd_buf[k] = LCD_INIT[i + 2 + k];
        lcd_data(lcd_buf, n);
    }
}

static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, r;
    for (i = 0; i < w; i++) {
        lcd_buf[2 * i] = (uint8_t)(c >> 8);
        lcd_buf[2 * i + 1] = (uint8_t)c;
    }
    lcd_window(x, y, x + w - 1u, y + h - 1u);
    for (r = 0; r < h; r++) {
        lcd_data(lcd_buf, w * 2u);
        poll();
    }
}

static void lcd_clear_ram(void)                 /* the whole controller RAM: no leftovers of the stock UI */
{
    uint32_t i, r;
    for (i = 0; i < LCD_W * 2u; i++)
        lcd_buf[i] = 0;
    lcd_window(0, 0, LCD_W - 1u, LCD_RAM_ROWS - 1u);
    for (r = 0; r < LCD_RAM_ROWS; r++) {
        lcd_data(lcd_buf, LCD_W * 2u);
        poll();
    }
}

static void lcd_splash(void)
{
    uint32_t r, i;
    lcd_clear_ram();
    lcd_window(0, 0, LCD_W - 1u, LCD_H - 1u);
    for (r = 0; r < LCD_H; r++) {
        const uint8_t *src = SPLASH565 + r * LCD_W * 2u;
        for (i = 0; i < LCD_W * 2u; i++)
            lcd_buf[i] = src[i];                  /* XIP -> RAM: the DMA reads RAM only */
        lcd_data(lcd_buf, LCD_W * 2u);
        poll();
    }
    lcd_cmd(0x29);                                /* display on */
}

#define RGB(r, g, b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3)))
#define C_BG RGB(14, 14, 16)
#define C_TEXT RGB(200, 196, 190)
#define C_OK RGB(110, 210, 120)
#define C_WARN RGB(255, 160, 40)
#define C_ERR RGB(240, 70, 60)

/* one line of 8x16 text, centred in the status band */
static void status(const char *s, uint16_t fg)
{
    uint32_t n = 0, x0, row, i;
    while (s[n] && n < 30u)
        n++;
    lcd_fill(0, SPLASH_STATUS_Y, LCD_W, LCD_H - SPLASH_STATUS_Y, C_BG);
    if (!n)
        return;
    x0 = (LCD_W - n * 8u) / 2u;
    lcd_window(x0, SPLASH_STATUS_Y + 6u, x0 + n * 8u - 1u, SPLASH_STATUS_Y + 21u);
    for (row = 0; row < 16u; row++) {
        for (i = 0; i < n * 8u; i++) {
            uint32_t ch = (uint8_t)s[i / 8u];
            uint32_t bits = ch >= 32u && ch < 127u ? FONT8X16[(ch - 32u) * 16u + row] : 0u;
            uint16_t c = bits & (0x80u >> (i % 8u)) ? fg : C_BG;
            lcd_buf[2 * i] = (uint8_t)(c >> 8);
            lcd_buf[2 * i + 1] = (uint8_t)c;
        }
        lcd_data(lcd_buf, n * 16u);
    }
    poll();
}

static void hex(char *d, uint32_t v, uint32_t digits)
{
    while (digits--)
        *d++ = "0123456789ABCDEF"[(v >> (4u * digits)) & 15u];
}

/* 8x16 text at (x, y), cell background bg */
static void text_at(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg)
{
    uint32_t n = 0, row, i;
    while (s[n] && n < 30u)
        n++;
    if (!n)
        return;
    lcd_window(x, y, x + n * 8u - 1u, y + 15u);
    for (row = 0; row < 16u; row++) {
        for (i = 0; i < n * 8u; i++) {
            uint32_t ch = (uint8_t)s[i / 8u];
            uint32_t bits = ch >= 32u && ch < 127u ? FONT8X16[(ch - 32u) * 16u + row] : 0u;
            uint16_t c = bits & (0x80u >> (i % 8u)) ? fg : bg;
            lcd_buf[2 * i] = (uint8_t)(c >> 8);
            lcd_buf[2 * i + 1] = (uint8_t)c;
        }
        lcd_data(lcd_buf, n * 16u);
    }
}

/* ------------------------------------------------------------ input test --- */
/* v0.3: the inputs the stock firmware configures (docs/06), shown live and sent over USB
 * (SysEx F0 7D 'J' 'B' 'I' <digital bits, 2 x 7> <3 ADC values, 2 x 7 bits each> F7, on change)
 * so the panel can be mapped by pressing / turning. Digital: pull-up inputs, scanned by the stock
 * firmware every 20 ms (the 7 first) and 50 ms (PC1, PC0). Analog: SARADC channels scanned every 15 ms. */
#define PORT_REG(p, r) (*(volatile uint32_t *)(0x50000u + (p) * 0x40u + (r)))
enum { P_A = 0, P_B = 1, P_C = 2, P_H = 7 };
enum { R_OUT = 0x00, R_IN = 0x04, R_DIR = 0x08, R_DIE = 0x0C, R_PU = 0x10, R_PD = 0x14 };
static const uint8_t DIN[][2] = {{P_H, 6}, {P_A, 1}, {P_A, 0}, {P_H, 9}, {P_H, 8}, {P_A, 3}, {P_A, 2}, {P_C, 1}, {P_C, 0}};
static const char *const DIN_NAME[] = {"PH6", "PA1", "PA0", "PH9", "PH8", "PA3", "PA2", "PC1", "PC0"};
#define N_DIN (sizeof DIN / sizeof DIN[0])
static const uint8_t AIN[][3] = {{P_A, 10, 2}, {P_C, 10, 9}, {P_C, 9, 8}};   /* port, pin, SARADC channel */
static const char *const AIN_NAME[] = {"PA10", "PC10", "PC9"};
#define N_AIN (sizeof AIN / sizeof AIN[0])
#define ADC_CON (*(volatile uint32_t *)0x13100u)
#define ADC_RES (*(volatile uint32_t *)0x13104u)
#define WLA_CON0 (*(volatile uint32_t *)0x11900u)

static void inputs_init(void)
{
    uint32_t i, m;
    for (i = 0; i < N_DIN; i++) {                 /* as stock: pull-up on, pull-down off, digital in */
        m = 1u << DIN[i][1];
        PORT_REG(DIN[i][0], R_PU) |= m;
        PORT_REG(DIN[i][0], R_PD) &= ~m;
        PORT_REG(DIN[i][0], R_DIE) |= m;
        PORT_REG(DIN[i][0], R_DIR) |= m;
    }
    for (i = 0; i < N_AIN; i++) {                 /* as stock: no pulls, digital input off, input */
        m = 1u << AIN[i][1];
        PORT_REG(AIN[i][0], R_PU) &= ~m;
        PORT_REG(AIN[i][0], R_PD) &= ~m;
        PORT_REG(AIN[i][0], R_DIE) &= ~m;
        PORT_REG(AIN[i][0], R_DIR) |= m;
    }
}

static uint32_t din_read(void)                    /* bit i = 1: pressed (pin low) */
{
    uint32_t i, v = 0;
    for (i = 0; i < N_DIN; i++)
        if (!(PORT_REG(DIN[i][0], R_IN) & (1u << DIN[i][1])))
            v |= 1u << i;
    return v;
}

static int32_t adc_read(uint32_t ch)              /* 0..1023, -1 on timeout (Felucca fm1_adc_read) */
{
    uint32_t t, v;
    ADC_CON = 0;
    if (WLA_CON0 & (1u << 14))
        WLA_CON0 &= ~(1u << 14);
    ADC_CON = 0xF04Eu | ((ch & 0xFu) << 8);
    ADC_CON |= 0x10u;
    ADC_CON |= 0x40u;
    for (t = 0; t < 20000u; t++)
        if (ADC_CON & 0x80u)
            break;
    v = ADC_RES & 0x3FFu;
    ADC_CON = 0x40u;
    ADC_CON = 0;
    return t == 20000u ? -1 : (int32_t)v;
}

#define C_HOT RGB(255, 150, 30)
#define C_DIM RGB(90, 90, 96)

static void dec4(char *d, int32_t v)
{
    uint32_t u = v < 0 ? 9999u : (uint32_t)v, k;
    for (k = 4; k--;) {
        d[k] = (char)('0' + u % 10u);
        u /= 10u;
    }
}

static void test_screen(void)
{
    uint32_t i;
    lcd_fill(0, 0, LCD_W, LCD_H, C_BG);
    text_at(8, 4, "Juice-Box " JB_VERSION "  ENTRADAS", C_HOT, C_BG);
    for (i = 0; i < N_DIN; i++)
        text_at(8u + (i % 5u) * 46u, 30u + (i / 5u) * 22u, DIN_NAME[i], C_DIM, C_BG);
    for (i = 0; i < N_AIN; i++)
        text_at(8, 90u + i * 34u, AIN_NAME[i], C_TEXT, C_BG);
    text_at(8, 180, "IN L", C_TEXT, C_BG);
    text_at(8, 196, "IN R", C_TEXT, C_BG);
}

static void test_draw(uint32_t din, uint32_t din_old, const int32_t *a, const int32_t *a_old, uint32_t first)
{
    uint32_t i, w;
    char num[5] = "0000";
    for (i = 0; i < N_DIN; i++)
        if (first || ((din ^ din_old) >> i & 1u))
            text_at(8u + (i % 5u) * 46u, 30u + (i / 5u) * 22u, DIN_NAME[i], din >> i & 1u ? C_BG : C_DIM,
                    din >> i & 1u ? C_HOT : C_BG);
    for (i = 0; i < N_AIN; i++) {
        if (!first && a[i] == a_old[i])
            continue;
        dec4(num, a[i]);
        text_at(56, 90u + i * 34u, num, C_TEXT, C_BG);
        w = a[i] < 0 ? 0u : (uint32_t)a[i] * 136u / 1023u;
        if (w)
            lcd_fill(96, 94u + i * 34u, w, 8, C_HOT);
        if (w < 136u)
            lcd_fill(96u + w, 94u + i * 34u, 136u - w, 8, RGB(40, 40, 46));
    }
}

static void test_send(uint32_t din, const int32_t *a)
{
    static uint8_t m[16];
    uint32_t i, n = 0;
    m[n++] = 0xF0; m[n++] = 0x7D; m[n++] = 'J'; m[n++] = 'B'; m[n++] = 'I';
    m[n++] = (uint8_t)(din & 0x7Fu);
    m[n++] = (uint8_t)(din >> 7 & 0x7Fu);
    for (i = 0; i < N_AIN; i++) {
        uint32_t v = a[i] < 0 ? 0x3FFFu : (uint32_t)a[i];
        m[n++] = (uint8_t)(v & 0x7Fu);
        m[n++] = (uint8_t)(v >> 7 & 0x7Fu);
    }
    m[n++] = 0xF7;
    if (usb.config && !usb.suspended)
        ota_wire_send(m, n);
}

/* ------------------------------------------------------------------ LEDs --- */
/* Button LEDs behind an 8-bit shift register (the stock 0x02005E9C): PC5 clock, PC8 data, PC6 latch,
 * PC7 = output enable, active LOW (v0.5.1 drove it high: all LEDs off; with it low, as the LCD init leaves it,
 * the register's random power-on content showed). 8 bits, first shifted bit first. */
static void led_write(uint8_t v)
{
    uint32_t b;
    PORT_REG(P_C, R_OUT) &= ~((1u << 6) | (1u << 5));
    for (b = 0; b < 8u; b++) {
        if (v & (0x80u >> b))
            PORT_REG(P_C, R_OUT) |= 1u << 8;
        else
            PORT_REG(P_C, R_OUT) &= ~(1u << 8);
        PORT_REG(P_C, R_OUT) |= 1u << 5;          /* (an SFR write takes far longer than the 595's 20 ns) */
        PORT_REG(P_C, R_OUT) &= ~(1u << 5);
    }
    PORT_REG(P_C, R_OUT) |= 1u << 6;              /* latch */
}

/* Software PWM: 16 levels per LED, one level step per led_pwm_tick() (from poll(), every 200 us:
 * a 3.2 ms frame, ~310 Hz). The LEDs are active low: a 0 bit lights. */
static volatile uint8_t led_bri[8];
static void led_pwm_tick(void)
{
    static uint32_t last, sub;
    uint32_t now = fm1_ticks(), k;
    uint8_t v = 0;
    if (now - last < 200u * FM1_TICKS_PER_US)
        return;
    last = now;
    sub = (sub + 1u) & 15u;
    for (k = 0; k < 8u; k++)
        if (led_bri[k] > sub)
            v |= (uint8_t)(1u << k);
    led_write((uint8_t)~v);
}

static void led_init(void)
{
    uint32_t m = (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8);
    PORT_REG(P_C, R_OUT) &= ~((1u << 5) | (1u << 6) | (1u << 7) | (1u << 8));
    PORT_REG(P_C, R_DIR) &= ~m;
    led_write(0xFFu);                             /* all off */
}

/* ----------------------------------------------------------------- audio --- */
/* External codec on ALNK0 (I2S), set up as the stock firmware does (docs/06, 0x02007490):
 * MCLK PA4, SCLK PA5, LRCK PA6, channel 0 = TX (DAC) on PA7, channel 1 = RX (ADC) on PA8, PA9 = codec
 * enable (low, then high once the link runs). ~44118 Hz. RX (ADC): 24-bit RIGHT-justified in int32 (bits
 * 23..0, measured with tools/jb_audio.py; v0.4 read it left-justified = loud hiss). TX (DAC): LEFT-justified
 * (v0.5.1 wrote right-justified = silence; v0.4's << 8 sounded). L/R interleaved; the guitar is on L.
 * Double-buffered DMA; polled from poll(): CON2 bit 4 + ch = half done, write bit ch to clear;
 * CON0 bit 15 = the half the DMA is in. v0.4: passthrough, MASTER pot = output level. */
#define ALNK 0x12E00u
#define ALNK_CON0 (*(volatile uint16_t *)(ALNK + 0x00u))
#define ALNK_CON1 (*(volatile uint16_t *)(ALNK + 0x04u))
#define ALNK_CON2 (*(volatile uint8_t *)(ALNK + 0x08u))
#define ALNK_CON3 (*(volatile uint8_t *)(ALNK + 0x0Cu))
#define ALNK_ADR0 (*(volatile uint32_t *)(ALNK + 0x10u))
#define ALNK_ADR1 (*(volatile uint32_t *)(ALNK + 0x14u))
#define ALNK_LEN (*(volatile uint16_t *)(ALNK + 0x20u))
#define CLK_CON2 (*(volatile uint32_t *)0x10014u)
#define IOMAP_CON5 (*(volatile uint32_t *)0x51030u)
#define AU_HALF 256u                              /* int32 words per half: 128 stereo frames, 2.9 ms */
static int32_t au_tx[2 * AU_HALF] __attribute__((aligned(4)));
static int32_t au_rx[2 * AU_HALF] __attribute__((aligned(4)));
static uint8_t au_on;
static volatile uint32_t au_gain = 0;             /* Q16 output gain from the MASTER pot */
static uint32_t au_peak[2];                       /* |input| peak per channel since the last read */
static uint32_t au_halves, au_late;
static volatile uint8_t au_route = 1;             /* 0 stereo, 1 L to both (default: the guitar is on L), 2 R */
static volatile uint8_t snap_req, snap_ready, snap_trig;
static uint32_t au_in_max, au_clip_in, au_clip_out, au_us_max;
/* v0.7 output diagnostics: MOD = guitar / 440 Hz test tone; AMP = the DAC word format */
static volatile uint8_t au_tone, au_fmt;
static uint32_t au_phase;
static const int16_t SINQ[65] = {0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757, 32767};
#define N_FMT 5u
static const char *const FMT_NAME[N_FMT] = {"DAC: <<8 (LJ24)", "DAC: <<7", "DAC: <<9", "DAC: <<16 (16b)", "DAC: RJ24"};

static int32_t tone_next(void)                   /* 440 Hz, -12 dBFS, 24-bit */
{
    uint32_t ph = au_phase, idx = ph >> 24, q = idx & 63u;
    int32_t v;
    au_phase += 42834620u;                        /* 440 / 44118 * 2^32 */
    switch (idx >> 6) {
    case 0: v = SINQ[q]; break;
    case 1: v = SINQ[64u - q]; break;
    case 2: v = -SINQ[q]; break;
    default: v = -SINQ[64u - q]; break;
    }
    return v * 64;                                /* 32767 * 64 = 2^21: -12 dBFS of 2^23 */
}
static int32_t snap_buf[AU_HALF] __attribute__((aligned(4)));

static void pa_out(uint32_t pin, uint32_t v)      /* gpio_direction_output */
{
    if (v)
        PORT_REG(P_A, R_OUT) |= 1u << pin;
    else
        PORT_REG(P_A, R_OUT) &= ~(1u << pin);
    PORT_REG(P_A, R_DIR) &= ~(1u << pin);
}

static void audio_start(void)
{
    uint32_t i;
    for (i = 0; i < 2u * AU_HALF; i++)
        au_tx[i] = au_rx[i] = 0;
    pa_out(9, 0);                                 /* codec off */
    fm1_delay_ms(5);
    ALNK_CON0 = 0;
    ALNK_CON1 = 0;
    ALNK_CON2 = 0;
    ALNK_CON3 = 0;
    IOMAP_CON5 = (IOMAP_CON5 & ~0xC0u) | 0x40u;   /* as stock (the FM-1 uses 0) */
    ALNK_CON3 |= 3u;
    pa_out(4, 1);                                 /* MCLK */
    ALNK_CON0 |= 0x100u;
    pa_out(6, 1);                                 /* LRCK */
    pa_out(5, 1);                                 /* SCLK */
    ALNK_CON0 |= 0x80u;
    ALNK_CON0 &= ~0x40u;
    ALNK_LEN = AU_HALF;
    ALNK_CON0 &= ~0x400u;
    ALNK_CON0 &= ~0x200u;
    CLK_CON2 &= 0xFFFFF0FFu;
    ALNK_CON3 &= ~0x1Cu;
    ALNK_CON3 = (uint8_t)((ALNK_CON3 & 0x1Fu) | 0x80u);
    /* channel 0: TX on PA7 */
    ALNK_CON1 |= 1u << 0;
    ALNK_CON1 |= 1u << 2;
    ALNK_ADR0 = (uint32_t)au_tx;
    pa_out(7, 1);
    ALNK_CON1 &= ~(1u << 3);
    /* channel 1: RX on PA8 (input, pull-up, digital) */
    ALNK_CON1 |= 1u << 4;
    ALNK_CON1 |= 1u << 6;
    ALNK_ADR1 = (uint32_t)au_rx;
    PORT_REG(P_A, R_DIR) |= 1u << 8;
    PORT_REG(P_A, R_PD) &= ~(1u << 8);
    PORT_REG(P_A, R_DIE) |= 1u << 8;
    PORT_REG(P_A, R_PU) |= 1u << 8;
    ALNK_CON1 |= 1u << 7;
    ALNK_CON2 = 0x0Fu;                            /* clear pendings */
    ALNK_CON0 |= 0x800u;                          /* run */
    fm1_delay_ms(5);
    pa_out(9, 1);                                 /* codec on */
    au_on = 1;
}

static void audio_stop(void)
{
    if (!au_on)
        return;
    ALNK_CON0 &= ~0x800u;
    pa_out(9, 0);
    au_on = 0;
}

static void audio_service(void)
{
    uint8_t p;
    uint32_t h, i, g, t0, us;
    int32_t *in, *out;
    if (!au_on)
        return;
    p = ALNK_CON2;
    if ((p & 0x30u) != 0x30u) {                   /* both the TX and the RX half must be done */
        if (p & 0x30u)
            au_late++;                            /* (counts polls that saw only one of them) */
        return;
    }
    au_halves++;
    t0 = fm1_ticks();
    h = ((ALNK_CON0 >> 15) & 1u) ^ 1u;            /* the half both DMAs just left */
    in = au_rx + h * AU_HALF;
    out = au_tx + h * AU_HALF;
    g = au_gain;
    if (snap_trig && !snap_ready) {               /* armed: capture the first loud half */
        uint32_t pk = 0;
        for (i = 0; i < AU_HALF; i += 2u) {
            int32_t x = (int32_t)((uint32_t)in[i] << 8) >> 8;
            uint32_t ax = (uint32_t)(x < 0 ? -x : x);
            if (ax > pk)
                pk = ax;
        }
        if (pk > (1u << 20)) {                    /* > -18 dBFS */
            snap_trig = 0;
            snap_req = 1;
        }
    }
    if (snap_req && !snap_ready) {                /* raw input words for tools/jb_audio.py */
        for (i = 0; i < AU_HALF; i++)
            snap_buf[i] = in[i];
        snap_req = 0;
        snap_ready = 1;
    }
    for (i = 0; i < AU_HALF; i++) {
        uint32_t src = au_route == 0u ? i : au_route == 1u ? (i & ~1u) : (i | 1u);
        int32_t x = (int32_t)((uint32_t)in[src] << 8) >> 8;   /* sign-extend the 24-bit sample */
        if (au_tone) {
            static int32_t t;
            if (!(i & 1u))
                t = tone_next();
            x = t;
        }
        int64_t y = (int64_t)x * (int64_t)g >> 16;
        uint32_t ax = (uint32_t)(x < 0 ? -x : x);
        if (ax > au_peak[i & 1u])
            au_peak[i & 1u] = ax;
        if (ax > au_in_max)
            au_in_max = ax;
        if (ax >= 0x7FF000u)
            au_clip_in++;
        if (y > 0x7FFFFF) {
            y = 0x7FFFFF;
            au_clip_out++;
        }
        if (y < -0x800000) {
            y = -0x800000;
            au_clip_out++;
        }
        switch (au_fmt) {                         /* 24-bit sample y -> the DAC word */
        case 0: out[i] = (int32_t)((uint32_t)y << 8); break;
        case 1: out[i] = (int32_t)((uint32_t)y << 7); break;
        case 2: out[i] = (int32_t)((uint32_t)y << 9); break;
        case 3: out[i] = (int32_t)((uint32_t)(y >> 8) << 16); break;
        default: out[i] = (int32_t)y; break;
        }
    }
    ALNK_CON2 |= 0x03u;                           /* clear channel 0 and 1 pendings */
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    if (us > au_us_max)
        au_us_max = us;
}

/* ----------------------------------------------------------------- flash --- */
/* interrupts are never enabled: call the RAM driver directly (no irq_restore / sti) */
static uint8_t fl_page[256] __attribute__((aligned(4)));
static uint32_t flash_id;
static uint8_t flash_ok;

static int fl_read(uint32_t off, void *p, uint32_t n)
{
    return FL_FAR(fl_read_ram)(off, (uint8_t *)p, n);
}

static int fl_erase_sector(uint32_t off)
{
    uint32_t took;
    return FL_FAR(fl_erase4k_ram)(off, &took);
}

static int fl_prog(uint32_t off, const uint8_t *src, uint32_t n)     /* src: RAM or XIP (copied) */
{
    uint32_t took, i;
    while (n) {
        uint32_t chunk = 256u - (off & 0xFFu);
        int rc;
        if (chunk > n)
            chunk = n;
        for (i = 0; i < chunk; i++)
            fl_page[i] = src[i];
        rc = FL_FAR(fl_prog_ram)(off, fl_page, chunk, &took);
        if (rc)
            return rc;
        poll();
        off += chunk;
        src += chunk;
        n -= chunk;
    }
    return 0;
}

/* --------------------------------------------------------- update entry --- */
static uint32_t ota_now_ms(void) { return now_ms(); }
static void ota_idle(void) { poll(); }
static int ota_erase(uint32_t off)
{
    if (!flash_ok || (off & 0xFFFu))
        return -8;
    return fl_erase_sector(off);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n) { return flash_ok ? fl_prog(off, p, n) : -8; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { return fl_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code);
static void ota_commit(const uint8_t *parm);
#include "ota.c"

#define BOOTGUARD_MAGIC 0x41524D41u              /* "ARMA" */
static struct {
    uint32_t magic, pending, failed, rescues, boots, check;
} bootguard __attribute__((section(".noinit")));

/* where a boot got to (RAM that survives warm resets): logged by the next boot */
static struct {
    uint32_t magic, stage, ms, loops, check;
} crumb __attribute__((section(".noinit")));

/* The SRAM keeps (decaying) contents across a short power-off on the switch: v0.1.2 read a stale
 * bootguard as "two failed boots" and went to the rescue loader on battery. Both blocks carry a check
 * word, rewritten with every change; a mismatch is a cold start. */
#define SEAL_K 0x5A17C3E9u
static uint32_t bg_sum(void)
{
    return bootguard.magic ^ bootguard.pending * 0x9E3779B1u ^ bootguard.failed * 0x85EBCA77u ^
           bootguard.rescues * 0xC2B2AE3Du ^ bootguard.boots * 0x27D4EB2Fu ^ SEAL_K;
}
static uint32_t crumb_sum(void)
{
    return crumb.magic ^ crumb.stage * 0x9E3779B1u ^ crumb.ms * 0x85EBCA77u ^ crumb.loops * 0xC2B2AE3Du ^ SEAL_K;
}
#define BG_SEAL() (bootguard.check = bg_sum())
#define CRUMB_SEAL() (crumb.check = crumb_sum())
#define STAGE(n) (crumb.stage = (n), CRUMB_SEAL())

static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "UPDATE: PACKAGE", "UPDATE: HEAD", "UPDATE: LOADER", "UPDATE: CONFIRM",
                                       "UPDATE: RESTART"};
    char b[24] = "UPDATE FAILED -00";
    uint32_t v;
    if (step < 6u) {
        status(STEP[step], C_WARN);
        return;
    }
    v = code < 0 ? (uint32_t)-code : (uint32_t)code;
    b[15] = (char)('0' + v / 10u % 10u);
    b[16] = (char)('0' + v % 10u);
    status(b, C_ERR);
    delay_poll(1500);
}

static void ota_commit(const uint8_t *parm)
{
    audio_stop();
    bootguard.pending = 0;                        /* intentional reset */
    BG_SEAL();
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);                       /* RAM record + core reset: the SPL runs the staged loader */
}

/* -------------------------------------------------------------- boot log --- */
/* 32-byte records in the JB_LOG sector (erased when full): what each boot found. Read over USB
 * with tools/jb_log.py (SysEx F0 7D 'J' 'B' 'L' F7). */
#define LOG_MAGIC 0x474C424Au                     /* "JBLG" */
static struct {
    uint32_t magic, boots, prev_stage, prev_ms, prev_loops;
    uint8_t p3_rst, wdt_con, failed, pending, rescues, warm, vers, pad;
    uint32_t rst_src;
} boot_rec;

static void log_write(void)
{
    uint32_t off, w[8], i, blank;
    for (off = 0; off < 0x1000u; off += 32u) {    /* records, then a fully blank slot; else (the stock VM's
                                                   * leftovers in this sector, or full): erase */
        if (fl_read(JB_LOG + off, w, 32))
            return;
        for (i = 0, blank = 1; i < 8u; i++)
            blank &= w[i] == 0xFFFFFFFFu;
        if (blank)
            break;
        if (w[0] != LOG_MAGIC) {
            off = 0x1000u;
            break;
        }
    }
    if (off >= 0x1000u) {
        if (fl_erase_sector(JB_LOG))
            return;
        off = 0;
    }
    fl_prog(JB_LOG + off, (const uint8_t *)&boot_rec, 32);
}

static void log_dump(void)                        /* every record as F0 7D 'J' 'B' 'L' idx pack7(32) F7 */
{
    static uint8_t rec[32], msg[64];
    uint32_t off, n;
    for (off = 0; off < 0x1000u; off += 32u) {
        if (fl_read(JB_LOG + off, rec, 32) || (rec[0] == 0xFFu && rec[1] == 0xFFu))
            break;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'L';
        msg[5] = (uint8_t)(off / 32u);
        n = 6u + ota_pack7(rec, 32, msg + 6);
        msg[n++] = 0xF7;
        ota_wire_send(msg, n);
    }
    msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'L'; msg[5] = 0x7F; msg[6] = 0xF7;
    ota_wire_send(msg, 7);
}

static void debug_service(void)                   /* our SysEx; anything else stays for ota_service */
{
    const uint8_t *p;
    uint32_t n;
    if (!ota_frame_get(&p, &n))
        return;
    if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'L') {
        ota_frame_done();
        log_dump();
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'A') {
        ota_frame_done();
        snap_ready = 0;
        snap_req = 1;                             /* the next input half; sent from the main loop */
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'T') {
        ota_frame_done();
        snap_ready = 0;
        snap_trig = 1;                            /* the first half above -18 dBFS */
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'S') {
        static uint8_t msg[64];
        uint32_t st[8], k;
        ota_frame_done();
        st[0] = au_halves; st[1] = au_late; st[2] = au_in_max; st[3] = au_clip_in;
        st[4] = au_clip_out; st[5] = au_us_max; st[6] = au_gain; st[7] = au_route | (uint32_t)au_fmt << 8 | (uint32_t)au_tone << 16;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'S';
        k = 5u + ota_pack7((const uint8_t *)st, 32, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    }
}

static void snap_send(void)                       /* F0 7D 'J' 'B' 'A' idx pack7(32 B) F7, idx 0..31, then 7F */
{
    static uint8_t msg[64];
    uint32_t k, n;
    const uint8_t *b = (const uint8_t *)snap_buf;
    for (k = 0; k < AU_HALF * 4u / 32u; k++) {
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'A';
        msg[5] = (uint8_t)k;
        n = 6u + ota_pack7(b + k * 32u, 32, msg + 6);
        msg[n++] = 0xF7;
        ota_wire_send(msg, n);
    }
    msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'A'; msg[5] = 0x7F; msg[6] = 0xF7;
    ota_wire_send(msg, 7);
}

/* ---------------------------------------------------------------- rescue --- */
static int rescue_staged(void)                    /* the official loader is at OTA_AREA, head and body intact */
{
    uint8_t h[32];
    uint32_t i, n, crc = 0;
    if (fl_read(OTA_AREA, h, 32) || ota_crc16(h + 2, 30, 0) != ota_rd16(h) || ota_rd32(h + 4) != OTA_AREA ||
        ota_rd32(h + 8) != RESCUE_LDR_LEN + 32u || h[12] != 0x41u)
        return 0;
    for (i = 0; i < RESCUE_LDR_LEN; i += n) {
        n = RESCUE_LDR_LEN - i > 256u ? 256u : RESCUE_LDR_LEN - i;
        if (fl_read(OTA_AREA + 32u + i, fl_page, n))
            return 0;
        crc = ota_crc16(fl_page, n, crc);
        poll();
    }
    return crc == RESCUE_LDR_DCRC;
}

static int rescue_stage(void)                     /* erase, body, verify, head last (as ota.c ota_stage) */
{
    uint32_t i;
    for (i = 0; i < OTA_AREA_LEN; i += 0x1000u) {
        if (fl_erase_sector(OTA_AREA + i))
            return -1;
        poll();
    }
    if (fl_prog(OTA_AREA + 32u, RESCUE_LDR, RESCUE_LDR_LEN))
        return -2;
    if (ota_stage_head(RESCUE_LDR_LEN))
        return -3;
    return rescue_staged() ? 0 : -4;
}

static void rescue_enter(void)                    /* never returns */
{
    static uint8_t parm[112];
    uint32_t i;
    for (i = 0; i < sizeof parm; i++)
        parm[i] = 0;
    ota_wr16(parm + 2, 0x5A0Du);
    ota_wr16(parm + 4, 0x5A01u);
    ota_wr16(parm + 6, 0x5441u);
    for (i = 0; i < sizeof OTA_LOADER_ID - 1u; i++)
        parm[8 + i] = (uint8_t)OTA_LOADER_ID[i];
    ota_wr32(parm + 72, OTA_AREA);
    ota_wr16(parm, ota_crc16(parm + 2, 78, 0));
    bootguard.pending = 0;
    bootguard.rescues++;
    BG_SEAL();
    if (usb.up)
        usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}

/* ------------------------------------------------------------------ main --- */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[], _pool_start[], _pool_end[];

static void jb_main(void)
{
    char line[32];
    uint32_t t_boot, shown = 0xFFu, rescue_ok;

    usb_start();                                  /* 1. USB + update entry first */
    STAGE(3);
    t_boot = now_ms();
    delay_poll(20);
    flash_id = FL_FAR(fl_jedec_ram)();
    flash_ok = (flash_id & 0xFFu) == 0x14u && (flash_id >> 16) != 0u && (flash_id >> 16) != 0xFFu;   /* a 1 MiB part */
    STAGE(4);
    if (flash_ok)
        log_write();
    STAGE(5);

    lcd_init();                                   /* 2. the screen */
    STAGE(6);
    lcd_splash();
    STAGE(7);
    str_cpy(line, "RST ?? BOOT ???? RESC ??", sizeof line);   /* diagnostics, 2 s */
    hex(line + 4, fm1_boot.p3_rst, 2);
    hex(line + 12, bootguard.boots, 4);
    hex(line + 22, bootguard.rescues, 2);
    status(line, C_TEXT);
    delay_poll(2000);
    status(JB_NAME " " JB_VERSION, C_TEXT);

    rescue_ok = flash_ok && (rescue_staged() || rescue_stage() == 0);   /* 3. the rescue loader */
    STAGE(8);
    if (!rescue_ok) {
        str_cpy(line, "FLASH ?????? NO RESCUE", sizeof line);
        hex(line + 6, flash_id, 6);
        status(line, C_ERR);
        delay_poll(3000);
    }

    inputs_init();
    delay_poll(1500);                             /* the splash, a moment */
    test_screen();
    led_init();
    led_on = 1;
    audio_start();
    STAGE(9);
    {
    uint32_t din = 0, din_old = 0, first = 1, t_scan = 0, t_adc = 0, t_meter = 0, t_led = 0, i;
    int32_t led_pos = 0, led_dir = 1;
    int32_t a[N_AIN] = {0}, a_old[N_AIN] = {0};
    for (;;) {
        uint32_t now = now_ms(), st;
        crumb.ms = now;
        crumb.loops++;
        CRUMB_SEAL();
        poll();
        debug_service();
        usb_retry(now);
        if (usb.uboot_req)                        /* the stock soft key: no UBOOT here (no Mac recovery) */
            usb.uboot_req = 0;
        ota_service();                            /* M-UPGRADE handshake */
        if (usb.ota_req) {
            usb.ota_req = 0;
            audio_stop();                         /* no buzz while the flash is busy */
            if (flash_ok)
                ota_session();                    /* returns only if nothing was committed */
            audio_start();
            shown = 0xFFu;
        }
        if (bootguard.pending && now - t_boot > 30000u) {
            bootguard.pending = 0;                /* 30 s up: a good boot */
            bootguard.failed = 0;
            BG_SEAL();
        }
        st = usb.up && usb.config && !usb.suspended ? 2u : 1u;   /* suspended: bus idle, the cable is out */
        if (st != shown) {
            shown = st;
            status(st == 2u ? "USB OK" : "sem USB", st == 2u ? C_OK : C_DIM);
        }
        if (snap_ready) {
            snap_send();
            snap_ready = 0;
        }
        if (now - t_led >= 120u) {                /* LED ping-pong: a lit ball with a fading trail */
            uint32_t k;
            t_led = now;
            for (k = 0; k < 8u; k++)
                led_bri[k] = (uint8_t)(led_bri[k] * 10u / 16u);
            led_bri[led_pos] = 16;
            if (led_pos + led_dir < 0 || led_pos + led_dir > 7)
                led_dir = -led_dir;
            led_pos += led_dir;
        }
        if (now - t_meter >= 60u) {               /* input meters */
            uint32_t ch, w;
            t_meter = now;
            for (ch = 0; ch < 2u; ch++) {
                uint32_t pk = au_peak[ch], lv = 0;
                au_peak[ch] = 0;
                while (pk > 64u && lv < 17u) {    /* ~6 dB steps from -102 dBFS */
                    pk >>= 1;
                    lv++;
                }
                w = lv * 8u;
                if (w)
                    lcd_fill(56, 184u + ch * 16u, w, 8, lv >= 16u ? C_ERR : lv >= 13u ? C_WARN : C_OK);
                if (w < 136u)
                    lcd_fill(56u + w, 184u + ch * 16u, 136u - w, 8, RGB(40, 40, 46));
            }
        }
        if (now - t_scan >= 5u) {                 /* inputs every 5 ms, ADC every 15 ms */
            uint32_t changed = 0;
            t_scan = now;
            din = din_read();
            if (now - t_adc >= 15u) {
                t_adc = now;
                for (i = 0; i < N_AIN; i++) {
                    a[i] = adc_read(AIN[i][2]);
                    if (first || a[i] - a_old[i] > 6 || a_old[i] - a[i] > 6)
                        changed = 1;
                }
            }
            if (a[0] >= 0) {                      /* MASTER (PA10) -> output gain, squared curve, 0..1.0 */
                uint32_t v = (uint32_t)a[0];
                au_gain = v * v * 65536u / (1023u * 1023u);
            }
            if ((din & ~din_old) & (1u << 3)) {   /* AMP: next DAC format */
                au_fmt = (uint8_t)((au_fmt + 1u) % N_FMT);
                text_at(8, 4, "                             ", C_HOT, C_BG);
                text_at(8, 4, FMT_NAME[au_fmt], C_HOT, C_BG);
            }
            if ((din & ~din_old) & (1u << 4)) {   /* MOD: guitar / test tone */
                au_tone ^= 1u;
                text_at(8, 4, "                             ", C_HOT, C_BG);
                text_at(8, 4, au_tone ? "FONTE: TOM 440 Hz" : "FONTE: GUITARRA", C_HOT, C_BG);
            }
            if ((din & ~din_old) & (1u << 2)) {   /* FX pressed: next routing */
                static const char *const RT[] = {"SAIDA: ESTEREO", "SAIDA: L -> L+R", "SAIDA: R -> L+R"};
                au_route = (uint8_t)((au_route + 1u) % 3u);
                text_at(8, 4, "                             ", C_HOT, C_BG);
                text_at(8, 4, RT[au_route], C_HOT, C_BG);
            }
            if (first || changed || din != din_old) {
                test_draw(din, din_old, a, a_old, first);
                test_send(din, a);
                din_old = din;
                for (i = 0; i < N_AIN; i++)
                    a_old[i] = a[i];
                first = 0;
            }
        }
    }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_reset_reason();                           /* before the watchdog is re-armed (shown at boot) */
    fm1_wdt_arm(0x0D);                            /* ~8 s; fed by poll() */
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                  /* flash driver: runs from RAM */
    if (bootguard.magic != BOOTGUARD_MAGIC || bootguard.check != bg_sum() || bootguard.pending > 1u) {
        /* cold start, decayed SRAM, or the update loaders used this RAM */
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.pending = 0;
        bootguard.failed = 0;
        bootguard.rescues = 0;
        bootguard.boots = 0;
    }
    bootguard.boots++;
    BG_SEAL();
    boot_rec.magic = LOG_MAGIC;
    boot_rec.boots = bootguard.boots;
    if (crumb.magic == BOOTGUARD_MAGIC && crumb.check == crumb_sum())
        boot_rec.warm = 1;
    else
        boot_rec.warm = 0;
    boot_rec.prev_stage = boot_rec.warm ? crumb.stage : 0;
    boot_rec.prev_ms = boot_rec.warm ? crumb.ms : 0;
    boot_rec.prev_loops = boot_rec.warm ? crumb.loops : 0;
    boot_rec.p3_rst = fm1_boot.p3_rst;
    boot_rec.wdt_con = fm1_boot.wdt_con;
    boot_rec.rst_src = fm1_boot.rst_src;
    boot_rec.pending = (uint8_t)bootguard.pending;
    boot_rec.rescues = (uint8_t)bootguard.rescues;
    boot_rec.vers = 0x20;
    crumb.magic = BOOTGUARD_MAGIC;
    crumb.stage = 2;
    crumb.ms = 0;
    crumb.loops = 0;
    CRUMB_SEAL();
    if (bootguard.pending)
        bootguard.failed++;
    boot_rec.failed = (uint8_t)bootguard.failed;
    bootguard.pending = 1;
    BG_SEAL();
    fm1_updata_parm_clear();                      /* no stale update record for the next warm reset */
    if (bootguard.failed >= 2u) {
        bootguard.failed = 0;
        BG_SEAL();
        if (rescue_staged())
            rescue_enter();
    }
    jb_main();
}
