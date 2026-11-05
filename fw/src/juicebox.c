/* SPDX-License-Identifier: GPL-3.0-only
 * Juice-Box by João Viana: NAM player firmware for the M-VAVE ANN BlackBox (JieLi AC791N).
 * (Working name until v0.1.2: armaria-nam.)
 *
 * First own firmware: USB-MIDI with the M-UPGRADE update entry first, then the
 * screen with the boot image. No audio, no inputs yet. Interrupts stay off:
 * USB is polled from the main loop, as in Felucca's update loader.
 *
 * Safety (docs/04-safety.md):
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
#define JB_VERSION "v0.18.0"
#define FELUCCA_LOADER 1                 /* usb.c: the polled, MIDI-only build of Felucca's loader */
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_OTA_RAMONLY 1            /* RAM update record only: no flash record at a location
                                          * the BlackBox SPL was never seen to scan */
#define FELUCCA_USB_PID 0x0001
#ifndef FELUCCA_ID
#define FELUCCA_ID "BlackBox_980"        /* handshake identity == package marker (fw/build.py) */
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

/* BlackBox flash map (docs/01): app area 0x4000..0xA9000 (flash.bin = 0xA5000), VM 0xA9000..0xC2000,
 * BTIF 0xC2000, USR 0xC3000.., key_mac 0xFF000. The loader is staged near the end of VM, as the stock
 * firmware and Felucca do on the FM-1 (whose VM ends at 0xE8000). */
#define OTA_AREA 0x000BA000u
#define OTA_AREA_LEN 0x5000u
#define OTA_FL_LEN 0x000A5000u
#define OTA_LOADER_ID "ota-BlackBox_980"

#define FM1_CPU1_USED 1                  /* fm1_sys.h: every reset path holds core 1 first */
#include "fm1_time.h"
#include "fm1_sys.h"
#define JB_LOG 0x000B9000u                       /* boot log sector, just below the staged loader */
/* v0.14: the capture store (8 slots of 8 KB) in the VM's free start, and the settings sector above the staged
 * loader. Not in USR (0xC3000): that is the stock firmware's file system (its users' AMP/CAB files), kept for a
 * return to the stock firmware. The stock firmware already lived with our data in the VM (the loader, 0xBA000). */
#define MS_BASE 0x000A9000u
#define MS_SLOTS 8u
#define MS_SLOT_LEN 0x2000u
#define MS_SET 0x000BF000u                       /* settings: the active slot (append-only records) */
#define FL_RANGE_OK(off, n) FL_IN(off, n, MS_BASE, 0x000C2000u)   /* the VM region only (BTIF at 0xC2000) */
#include "fm1_flash.h"
#include "fm1_irq.h"                     /* exception vectors + crash record (Felucca) */
#define C1_MAIN_SECTION ".text.c1main"
#define FM1_C1_EXCEPTIONS 1               /* v0.12: core 1 faults are caught (fm1_cpu1.h) */
#include "fm1_cpu1.h"                    /* the second core (Felucca / fm1-x0x / Melodee / Jangada, GPL-3.0) */
#include "libc.c"

#define USB_STRINGS_DEFINED
static const uint8_t STR1[] = {22, 3, 'J', 0, 'o', 0, 0xE3, 0, 'o', 0, ' ', 0, 'V', 0, 'i', 0, 'a', 0, 'n', 0,
                               'a', 0};                               /* manufacturer: the author */
static const uint8_t STR2[] = {20, 3, 'J', 0, 'u', 0, 'i', 0, 'c', 0, 'e', 0, '-', 0, 'B', 0, 'o', 0, 'x', 0};
#include "usb.c"

#include "splash.h"
#include "font.h"
#include "loader.h"
#include "a2lite.h"
#include "a2lx.h"                       /* v0.13: core 1 runs the back half in fixed point */
#include "fonts_aa.h"                    /* v0.15: the UI fonts (fw/tools/gen_assets.py) */

/* A core-0 fault: the record (fm1_crash, .noinit) is logged by the next boot */
static void fm1_fault(const fm1_crash_t *c)
{
    (void)c;
    fm1_reboot();
}

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
static void jb_stage(uint32_t n);
#define VDC14_LEVEL 7u                            /* 1.25 V + 0.05 V x level: v0.18 1.60 V (the top), for the accelerator + CPU traffic of
                                                   * the Onyx engine (at 1.50 V its results were sporadically wrong, docs/07); v0.13.16 used 1.50 V */
static uint8_t nam_vlevel = 13u;                  /* SYSVDD level for two cores (JBV sets it) */
static uint8_t nam_dbg_marks;                     /* v0.11.3: stage markers through the main loop while NAM runs */            /* the crumb's stage (defined with it, below) */
static void led_pwm_tick(void);
static uint8_t led_on;
/* v0.13.21 (JBE m): noise hunt, what the main loop leaves out: 1 = LEDs off (no PWM), 2 = no screen updates
 * (meters, CPU line), 4 = LEDs all on, static (current without PWM) */
static volatile uint8_t quiet;

/* v0.13.18: the longest time between two audio_service calls (gap_*), and per main-loop section (loop_at) the
 * longest stretch of its own code between two polls (sec_max; 20 = usb_poll, 21 = the LED PWM tick) */
static volatile uint8_t loop_at, gap_at;
static uint32_t gap_last, gap_max, gap_long, poll_out;
static uint16_t sec_max[24];
static void sec_note(uint32_t k, uint32_t t0)
{
    uint32_t us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    if (us > 0xFFFFu)
        us = 0xFFFFu;
    if (us > sec_max[k])
        sec_max[k] = (uint16_t)us;
}
static void poll(void)                          /* watchdog, USB at ~2 kHz, audio halves, LED PWM */
{
    uint32_t g0 = fm1_ticks(), gu;
    if (poll_out)
        sec_note(loop_at % 24u, poll_out);
    if (gap_last) {
        gu = (g0 - gap_last) / FM1_TICKS_PER_US;
        if (gu > 400u)
            gap_long++;
        if (gu > gap_max) {
            gap_max = gu;
            gap_at = loop_at;
        }
    }
    audio_service();
    gap_last = fm1_ticks();
    if (nam_dbg_marks)
        jb_stage(98);
    if (led_on) {
        uint32_t t = fm1_ticks();
        led_pwm_tick();
        sec_note(21, t);
    }
    if (nam_dbg_marks)
        jb_stage(99);
    static uint32_t last;
    uint32_t now = fm1_ticks();
    fm1_wdt_feed();
    if (usb.up && now - last >= 500u * FM1_TICKS_PER_US) {   /* (rescue checks at boot run before usb_start) */
        last = now;
        if (nam_dbg_marks)
            jb_stage(97);
        usb_poll();
        sec_note(20, now);
        if (nam_dbg_marks)
            jb_stage(96);
    }
    poll_out = fm1_ticks();
}

static void delay_poll(uint32_t ms)
{
    uint32_t t0 = now_ms();
    while (now_ms() - t0 < ms)
        poll();
}

/* --------------------------------------------------------------- display --- */
/* ST7789 240x240 on SPI1 port A (docs/06-stock-firmware-analysis.md): SCK PB6, data PB7, D/C PB5, CS PB1,
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
        poll();                                   /* v0.13.18: per row, as lcd_fill */
    }
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
        poll();                                   /* v0.13.18: a whole line between two polls made outputs late */
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

/* v0.13.18: adc_read split in two for the main loop: a conversion is started on one 5 ms scan and read on the
 * next (the three blocking reads every 15 ms held the audio for ~670 us: late NAM outputs) */
static void adc_start(uint32_t ch)
{
    ADC_CON = 0;
    if (WLA_CON0 & (1u << 14))
        WLA_CON0 &= ~(1u << 14);
    ADC_CON = 0xF04Eu | ((ch & 0xFu) << 8);
    ADC_CON |= 0x10u;
    ADC_CON |= 0x40u;
}
static int32_t adc_take(void)                     /* the started conversion, -1 if it is not done */
{
    uint32_t done = ADC_CON & 0x80u, v = ADC_RES & 0x3FFu;
    ADC_CON = 0x40u;
    ADC_CON = 0;
    return done ? (int32_t)v : -1;
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
    text_at(8, 4, "Juice-Box " JB_VERSION "  INPUTS", C_HOT, C_BG);
    for (i = 0; i < N_DIN; i++)
        text_at(8u + (i % 5u) * 46u, 30u + (i / 5u) * 22u, DIN_NAME[i], C_DIM, C_BG);
    for (i = 0; i < N_AIN; i++)
        text_at(8, 90u + i * 34u, AIN_NAME[i], C_TEXT, C_BG);
    text_at(8, 180, "IN", C_TEXT, C_BG);
    text_at(8, 196, "OUT", C_TEXT, C_BG);
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
 * Double-buffered DMA; polled from poll(): CON2 bit 4 + ch = half done, write bit ch to clear.
 * CON0 bit 15 does NOT toggle here (always 0, measured in v0.7.2): the half index is kept in software.
 * TX is RJ24 as well (v0.7.1 measurement through an audio interface: the only format giving 440 Hz). v0.4: passthrough, MASTER pot = output level. */
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
/* v0.17.2: the bypass (left footswitch, dry signal) runs at a fixed gain, not MASTER's: the user compared it with the guitar
 * straight into the amp and it was far quieter (MASTER's squared curve is -12 dB at half way). Q16; 65536 = the input word
 * to the output word. Set over USB for calibration (JBK): tools/jb_bypass.py */
static volatile uint32_t au_bypass_gain = 32768u;    /* v0.18: -6 dB (the user's ear test: unity distorted, -6 dB excellent) */
static uint8_t ui_bypass;                         /* (defined with the UI below; the audio path reads it) */
static volatile uint8_t au_mute;                  /* v0.15: output silent (a capture is being loaded) */
static volatile uint8_t au_umute;                 /* v0.16: the player's MUTE (left footswitch held) */
/* v0.13.22: the model's loudness normalized to -18 dB as the NAM plugin does (tools/nam2c.py, per model), then
 * +8 dB (at MASTER full the A2 example peaked at ~0.5 of full scale: too quiet); a soft limiter above 0.7 */
#define NAM_OUT_BOOST 2.5f
static float nam_trim = NAM_OUT_BOOST;           /* v0.14: per capture (its loudness trim x the boost) */
static float nam_in = 1.0f, nam_out = 1.0f;     /* v0.16: the bank's IN / OUT (linear) */
#define NAM_OUT_TRIM (nam_trim * nam_out)
#define LIM_T (0.7f * 8388607.0f)
#define LIM_R (8388607.0f - LIM_T)
static uint32_t au_peak[2];                       /* [0] |input L| peak, [1] |output| peak, since the last read */
static volatile uint32_t au_clip_in_ev, au_clip_out_ev;   /* clip events, for the screen */
/* v0.13.20: which half is done is software only (CON0 bit 15 never toggles here): a half lost once (a long
 * stretch without polls: boot, a model reset, before v0.13.18 every 7th) left au_h on the half the DMA was
 * filling / playing, for good: each block then began with ~10 samples from elsewhere (heard as hiss and
 * crackle, NAM on or off; tools/jb_capture.py). The RX words carry 24 bits under a zero byte, so the last
 * word of each half is set to AU_SENT once read: the half in progress still has it, the finished one not */
#define AU_SENT 0x5A5A5A5Au
static uint32_t au_resync;
static uint32_t au_halves, au_late, au_out_late;   /* au_out_late: halves whose output was written too late */
static volatile uint8_t au_route = 1;             /* 0 stereo, 1 L to both (default: the guitar is on L), 2 R */
static volatile uint8_t snap_req, snap_ready, snap_trig;
static uint32_t au_in_max, au_clip_in, au_clip_out, au_us_max;
/* v0.7 output diagnostics: MOD = guitar / 440 Hz test tone; AMP = the DAC word format */
static volatile uint8_t au_tone, au_fmt = 4;      /* RJ24: measured right (tools/jb_measure.py, v0.7.1) */
/* NAM A2-Lite (nam/a2lite.c) on the guitar (L) input; MOD toggles it */
static struct a2l nam __attribute__((section(".c1_bss.nam")));   /* v0.12.4: what core 1 reads, high RAM */
static struct a2lq namq __attribute__((section(".c1_bss.nam")));  /* v0.13: the back half, fixed point (core 1) */
static a2l_t nam_ring[A2L_RING_FRAMES * A2L_C] __attribute__((section(".pool")));
/* v0.18: the NAM engine. 1 = Onyx's A2-Lite block engine on the accelerator (fw/onyx, fw/src/jb_onyx.c), in nam_ring's memory,
 * run on core 1; 0 = the v0.13 two-core engine (a2lite.c front on core 0, a2lq.c back on core 1) */
#define NAM_ENG_ONYX 1u
static uint8_t nam_eng = NAM_ENG_ONYX;
extern void onyx_prepare(void *mem, uint32_t size, const float *w, uint32_t n, uint32_t *r);
extern float onyx_au_in[2][128], onyx_au_out[2][128];
extern float onyx_head_scale(void);
extern void onyx_reset(void), onyx_au_job(uint32_t slot);
extern uint32_t onyx_weights_addr(void);
#define CAP_INFO ((volatile uint32_t *)0x01C7D300u)   /* v0.18 capture: the scaling it ran with (tools/jb_capture_onyx.py) */
static volatile uint8_t nam_ok, nam_on, c1_ok;
static uint32_t nam_us, nam_us_max;
/* Two-core pipeline: core 0 runs a2l_front on block n while core 1 runs a2l_back on block n - 1 (one
 * block of extra latency, 2.9 ms). Slot n & 1 holds block n's input and the front's lin / hsum. */
static struct {
    a2l_t in[2][AU_HALF / 2u], lin[2][AU_HALF / 2u * A2L_C], hsum[2][AU_HALF / 2u * A2L_C], out[2][AU_HALF / 2u];
    a2lq_t qin[2][AU_HALF / 2u], qlin[2][AU_HALF / 2u * A2L_C], qhs[2][AU_HALF / 2u * A2L_C], qout[2][AU_HALF / 2u];
} pipe __attribute__((section(".c1_bss.pipe")));
static uint32_t pipe_blk, pipe_job_blk;          /* the block number; the block core 1's job is for */
static uint8_t pipe_primed;
static uint8_t pipe_busy;                         /* core 1 still owns a job (never held mid-job: on this chip a
                                                   * hold during work stalls the bus: USB and the audio DMA stop) */
/* v0.13.19 (JBW): 48 consecutive NAM blocks as the pedal saw them, from the first loud one (the model reset
 * there, as a2lx_reset after boot, so the host can run the same blocks from the same state): the raw input
 * (24-bit, sign-extended) and core 1's raw output (Q8.23, before the gain; call k holds block k - 1's output,
 * 0x7FFFFFFF where the block had none), for a host comparison against the float model */
#define CAP_BLK 48u
static int32_t cap_in[CAP_BLK][AU_HALF / 2u] __attribute__((section(".c1_bss.cap")));
static int32_t cap_out[CAP_BLK][AU_HALF / 2u] __attribute__((section(".c1_bss.cap")));
static volatile uint32_t cap_state, cap_n;        /* 0 idle, 1 armed, 2 capturing, 3 done */
static uint32_t pipe_late, pipe_late_max;         /* blocks core 1 was not done in time; the longest wait (us) */

static volatile uint32_t c1_test_out;
__attribute__((section(".ram_text"), noinline, used)) static void c1_int_job(uint32_t arg)
{
    c1_test_out = arg * 3u + 1u;
}
__attribute__((section(".ram_text"), noinline, used)) static void c1_float_job(uint32_t arg)
{
    volatile float a = (float)arg, b = 0.5f;
    union { float f; uint32_t u; } r;
    r.f = a * b + 1.25f;
    c1_test_out = r.u;
}

/* JB3 loads: register-only work for about n iterations (no shared memory writes but the result) */
__attribute__((section(".ram_text"), noinline, used)) static void c1_fload(uint32_t n)
{
    float a = 1.0f, b = 0.999f, c = 0.001f, d = 0.5f;
    while (n--) {
        a = a * b + c;
        d = d * b + a;
    }
    { union { float f; uint32_t u; } r; r.f = a + d; c1_test_out = r.u; }
}
__attribute__((section(".ram_text"), noinline, used)) static void c1_iload(uint32_t n)
{
    uint32_t a = 1, b = 7;
    while (n--) {
        a = a * 1664525u + 1013904223u;
        b ^= a >> 3;
    }
    c1_test_out = a ^ b;
}

/* float register load in XIP flash */
__attribute__((noinline, used)) static void x_fload(uint32_t n)
{
    float a = 1.0f, b = 0.999f, c = 0.001f, d = 0.5f;
    while (n--) {
        a = a * b + c;
        d = d * b + a;
    }
    { union { float f; uint32_t u; } r; r.f = a + d; c1_test_out = r.u; }
}

/* the same integer load in XIP flash (normal .text) */
__attribute__((noinline, used)) static void x_iload(uint32_t n)
{
    uint32_t a = 1, b = 7;
    while (n--) {
        a = a * 1664525u + 1013904223u;
        b ^= a >> 3;
    }
    c1_test_out = a ^ b;
}

/* XIP, not RAM: both cores executing RAM code at the same time lock the chip up (JB2 / JB3 / JB4, v0.9.x);
 * XIP on both is fine (core 1's idle loop stays in RAM, .c1_text, as in Felucca) */
__attribute__((noinline, used)) static void nam_back_job(uint32_t slot)   /* core 1: integer only (v0.13) */
{
    a2lq_back(&namq, pipe.qin[slot], pipe.qlin[slot], pipe.qhs[slot], pipe.qout[slot], AU_HALF / 2u);
}

/* v0.13.4 (JB9): core 1 runs the real fixed-point back half over and over for the given ticks */
static volatile uint32_t qb_runs __attribute__((section(".c1_bss.qb")));
__attribute__((noinline, used)) static void qback_loop_job(uint32_t ticks)
{
    uint32_t t0 = FM1_C1_T4, n = 0;
    while (FM1_C1_T4 - t0 < ticks) {
        a2lq_back(&namq, pipe.qin[0], pipe.qlin[0], pipe.qhs[0], pipe.qout[0], AU_HALF / 2u);
        n++;
    }
    qb_runs = n;
}

/* v0.13.7 (JBX): which arithmetic core 1 may use while core 0 runs float. Dense loops over a 256-word table,
 * checked against the first pass: m = 0 plain 32-bit multiply-adds, 1 the 64-bit multiply-accumulate */
static volatile uint32_t ar_res[4] __attribute__((section(".c1_bss.ar")));   /* runs, bad, ref, got */
static int32_t ar_tab[256] __attribute__((section(".c1_bss.ar")));
__attribute__((noinline)) static uint32_t ar_mul32(void)
{
    uint32_t i, r;
    int32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    for (r = 0; r < 8u; r++)
        for (i = 0; i < 256u; i += 4u) {
            a0 += ar_tab[i] * (int32_t)(r + 3u);
            a1 += ar_tab[i + 1u] * ar_tab[i];
            a2 += ar_tab[i + 2u] * 0x3B9AC9;
            a3 += ar_tab[i + 3u] * ar_tab[i + 2u];
        }
    return (uint32_t)(a0 ^ a1 ^ a2 ^ a3);
}
#define AR_MAC(acc, a, b) __asm__("%0 += %1 * %2 (s)" : "+r"(acc) : "r"(a), "r"(b))
__attribute__((noinline)) static uint32_t ar_mac64(void)
{
    uint32_t i, r;
    int64_t a0 = 0, a1 = 0, a2 = 0;
    for (r = 0; r < 8u; r++)
        for (i = 0; i < 255u; i += 3u) {
            AR_MAC(a0, ar_tab[i], ar_tab[i + 1u]);
            AR_MAC(a1, ar_tab[i + 1u], 0x1234567);
            AR_MAC(a2, ar_tab[i + 2u], ar_tab[i]);
        }
    return (uint32_t)(a0 ^ a1 ^ a2 ^ (a0 >> 32) ^ (a2 >> 29));
}
__attribute__((noinline, used)) static void ar_job(uint32_t arg)   /* arg: mode << 28 | ticks >> 4 */
{
    const uint32_t m = arg >> 28, ticks = (arg & 0x0FFFFFFFu) << 4;
    uint32_t t0 = FM1_C1_T4, n = 0, bad = 0, v, ref = m ? ar_mac64() : ar_mul32();
    while (FM1_C1_T4 - t0 < ticks) {
        v = m ? ar_mac64() : ar_mul32();
        if (v != ref) {
            if (!bad)
                ar_res[3] = v;
            bad++;
        }
        n++;
    }
    ar_res[0] = n; ar_res[1] = bad; ar_res[2] = ref;
}

/* core 0: the float front of the block in slot, then its hand-over to core 1 in Q8.23 */
static void nam_front_slot(uint32_t slot)
{
    void (*volatile front)(struct a2l *, const a2l_t *, a2l_t *, a2l_t *, uint32_t) = a2l_front;
    front(&nam, pipe.in[slot], pipe.lin[slot], pipe.hsum[slot], AU_HALF / 2u);
    a2lx_to_q(pipe.in[slot], pipe.qin[slot], AU_HALF / 2u);
    a2lx_to_q(pipe.lin[slot], pipe.qlin[slot], AU_HALF / 2u * A2L_C);
    a2lx_to_q(pipe.hsum[slot], pipe.qhs[slot], AU_HALF / 2u * A2L_C);
}

/* v0.12.1 (JB6): core 1 reads a known pattern for 2 s while core 0 runs the NAM front; misreads counted per
 * RAM region (Jangada's finding: core 1 reads wrong words under load; where?) */
#define MR_WORDS 1024u
static uint32_t mr_test_buf[MR_WORDS];
static uint32_t mr_low_buf[MR_WORDS] __attribute__((section(".lowtest")));   /* 0x01C08000: core 1's stack before v0.12.4 */
static volatile uint32_t *mr_base;
static volatile uint32_t mr_res[6] __attribute__((section(".c1_bss.mr")));               /* reads (x 1024), misreads, first: address, value, expected; done */
#define MR_PAT(a) ((uint32_t)(a) * 0x9E3779B1u ^ 0x5A5AA5A5u)
__attribute__((noinline, used)) static void mr_job(uint32_t ticks)
{
    volatile uint32_t *b = mr_base;
    uint32_t t0 = FM1_C1_T4, rounds = 0, bad = 0, i;
    while (FM1_C1_T4 - t0 < ticks) {
        for (i = 0; i < MR_WORDS; i++) {
            uint32_t v = b[i], e = MR_PAT((uint32_t)(uintptr_t)(b + i));
            if (v != e) {
                if (!bad) {
                    mr_res[2] = (uint32_t)(uintptr_t)(b + i); mr_res[3] = v; mr_res[4] = e;
                }
                bad++;
            }
        }
        rounds++;
    }
    mr_res[0] = rounds;
    mr_res[1] = bad;
}

/* v0.13.13 (JBY mode 8): core 1 writes the pattern (xor the round) and reads it back: write traffic, which the NAM's
 * back half has and the read census did not */
__attribute__((noinline, used)) static void mw_job(uint32_t ticks)
{
    volatile uint32_t *b = mr_base;
    uint32_t t0 = FM1_C1_T4, rounds = 0, bad = 0, i;
    while (FM1_C1_T4 - t0 < ticks) {
        for (i = 0; i < MR_WORDS; i++)
            b[i] = MR_PAT((uint32_t)(uintptr_t)(b + i)) ^ rounds;
        for (i = 0; i < MR_WORDS; i++) {
            uint32_t v = b[i], e = MR_PAT((uint32_t)(uintptr_t)(b + i)) ^ rounds;
            if (v != e) {
                if (!bad) {
                    mr_res[2] = (uint32_t)(uintptr_t)(b + i); mr_res[3] = v; mr_res[4] = e;
                }
                bad++;
            }
        }
        rounds++;
    }
    mr_res[0] = rounds;
    mr_res[1] = bad;
}

/* v0.12.3 (JB7): float on both cores at once. A deterministic float chain with an integer check word
 * folded in, run back to back and compared with the core's own result from before the other core started */
__attribute__((noinline)) static uint32_t fp_chain(float a, float b, uint32_t n, uint32_t *ichk)
{
    float x = 1.0f, y = 0.5f, z = 0.25f;
    uint32_t i, k = 0x12345678u;
    for (i = 0; i < n; i++) {
        x = x * a + b;
        y = y * b + x * 0.5f;
        z = z * a - y * 0.25f + 1.0f;
        k = (k ^ i) * 0x01000193u + (k >> 7);
    }
    *ichk = k;
    { union { float f; uint32_t u; } c; c.f = x + y * 3.0f + z * 7.0f; return c.u; }
}
static volatile uint32_t fp_go, fp_ready, fp_res1[5];   /* core 1: runs, bad, first value, its reference, ichk bad */
__attribute__((noinline, used)) static void fp_job(uint32_t ticks)
{
    uint32_t ref, rk, v, kk, runs = 0, bad = 0, kbad = 0, t0;
    ref = fp_chain(0.999f, 0.001f, 500u, &rk);
    fp_ready = 1;
    FM1_C1_SYNC();
    while (!fp_go)
        FM1_C1_SYNC();
    t0 = FM1_C1_T4;
    while (FM1_C1_T4 - t0 < ticks) {
        v = fp_chain(0.999f, 0.001f, 500u, &kk);
        if (v != ref) {
            if (!bad)
                fp_res1[2] = v;
            bad++;
        }
        if (kk != rk)
            kbad++;
        runs++;
    }
    fp_res1[0] = runs; fp_res1[1] = bad; fp_res1[3] = ref; fp_res1[4] = kbad;
}

/* v0.12.5 (JB8): integer work with deep calls on core 1 (return addresses and spills through its stack, where
 * v0.12.3 saw a RETS read back with bit 21 set), checked against core 0's own result */
__attribute__((noinline)) static uint32_t int_rec(uint32_t d, uint32_t x)
{
    uint32_t a, b;
    if (!d)
        return x * 0x01000193u ^ (x >> 11);
    a = int_rec(d - 1u, x + 1u);
    b = int_rec(d - 1u, a ^ x);
    return (a + (b << 3)) ^ (b >> 5) ^ d;
}
static volatile uint32_t ir_res[4];               /* runs, bad, first value, reference */
__attribute__((noinline, used)) static void ir_job(uint32_t ticks)
{
    uint32_t t0 = FM1_C1_T4, runs = 0, bad = 0, v;
    while (FM1_C1_T4 - t0 < ticks) {
        v = int_rec(10u, 0x1234u);
        if (v != ir_res[3]) {
            if (!bad)
                ir_res[2] = v;
            bad++;
        }
        runs++;
    }
    ir_res[0] = runs;
    ir_res[1] = bad;
}

/* v0.12: the debug unit (corex2, 0x1EEE000; the AC79 SDK's cpu/wl82/debug.c) guards core 1 while the NAM runs:
 * every address it has no business writing is a write-limit window for it alone (C1_WR_LIMIT_EN), and bad bus
 * accesses raise an exception (DBG_EN, as the SDK's debug_init) instead of reading junk */
#define CX2(o) (*(volatile uint32_t *)(0x1EEE000u + (o)))
static uint32_t c1_win[8][2], c1_nwin;
extern uint32_t _c1b_start[], _c1b_end[];
static void c1_guard_setup(void)
{
    uint32_t a[10][2], g[12][2], n = 0, ng = 0, i, j, lo = 0x01C00000u;
#define C1_ALLOW(p, sz) (a[n][0] = (uint32_t)(uintptr_t)(p), a[n][1] = a[n][0] + (uint32_t)(sz), n++)
    C1_ALLOW(_c1b_start, (uint32_t)(uintptr_t)_c1b_end - (uint32_t)(uintptr_t)_c1b_start);   /* its stacks, mailbox,
                                                   * the models, the pipe, its scratch and fault record (.c1_bss) */
    C1_ALLOW(nam_ring, sizeof nam_ring);
    C1_ALLOW(ir_res, sizeof ir_res);
    for (i = 1; i < n; i++)                       /* by address */
        for (j = i; j && a[j - 1][0] > a[j][0]; j--) {
            uint32_t t0 = a[j][0], t1 = a[j][1];
            a[j][0] = a[j - 1][0]; a[j][1] = a[j - 1][1]; a[j - 1][0] = t0; a[j - 1][1] = t1;
        }
    for (i = 0; i < n; i++) {                     /* the gaps between them (under 256 bytes: left open) */
        if (a[i][0] > lo + 256u) {
            g[ng][0] = lo; g[ng][1] = a[i][0] - 1u; ng++;
        }
        if (a[i][1] > lo)
            lo = (a[i][1] + 3u) & ~3u;
    }
    g[ng][0] = lo; g[ng][1] = 0x01EEEFFFu; ng++;  /* above them: RAM, vectors, peripherals (not its own EMU / ICFG) */
    while (ng > 7u) {                             /* 8 windows: the smallest gaps stay open */
        uint32_t m = 0;
        for (i = 1; i < ng; i++)
            if (g[i][1] - g[i][0] < g[m][1] - g[m][0])
                m = i;
        for (i = m; i + 1u < ng; i++) {
            g[i][0] = g[i + 1][0]; g[i][1] = g[i + 1][1];
        }
        ng--;
    }
    c1_win[0][0] = 0; c1_win[0][1] = 0x01BFFFFFu; /* below RAM: system registers (0x10000: the reset) */
    for (i = 0; i < ng; i++) {
        c1_win[i + 1u][0] = g[i][0]; c1_win[i + 1u][1] = g[i][1];
    }
    c1_nwin = ng + 1u;
    CX2(0x34Cu) = 0;                              /* C1_WR_LIMIT_EN off while they change */
    for (i = 0; i < c1_nwin; i++) {
        CX2(0x2C0u + 4u * i) = c1_win[i][0];      /* WR_LIMITn_L */
        CX2(0x280u + 4u * i) = c1_win[i][1];      /* WR_LIMITn_H (inclusive) */
    }
    CX2(0x248u) = 0xFFFFFFFFu;                    /* DBG_MSG_CLR */
    CX2(0x34Cu) = (1u << c1_nwin) - 1u;           /* core 1 only */
    CX2(0x340u) |= (0x3Fu << 16) | (0x3u << 4) | (1u << 11);   /* DBG_EN: bus invalid (both cores), AXI, c1 write limit */
}
static uint32_t au_h, au_b15[2];                  /* software half index; CON0 bit 15 histogram (diagnostic) */
static volatile int32_t au_gain_force = -1;      /* >= 0: USB-set Q16 gain overrides MASTER (JBC) */
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
    au_rx[AU_HALF - 1u] = au_rx[2u * AU_HALF - 1u] = (int32_t)AU_SENT;
    au_h = 0;
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
    ALNK_CON2 |= 0x03u;                           /* v0.13.18: clear the pendings now, not after the work: a half
                                                   * that ends while this one is processed is then seen next poll
                                                   * (cleared at the end, it was lost and au_h flipped: 14 % of the
                                                   * halves at 2.7 ms of NAM per 2.9 ms block) */
    au_halves++;
    t0 = fm1_ticks();
    au_b15[(ALNK_CON0 >> 15) & 1u]++;
    if ((uint32_t)au_rx[au_h * AU_HALF + AU_HALF - 1u] == AU_SENT &&
        (uint32_t)au_rx[(au_h ^ 1u) * AU_HALF + AU_HALF - 1u] != AU_SENT) {
        au_h ^= 1u;                               /* au_h was on the half in progress */
        au_resync++;
    }
    h = au_h;                                     /* v0.7.1 used CON0 bit 15 here: one half never got written */
    au_h ^= 1u;
    in = au_rx + h * AU_HALF;
    out = au_tx + h * AU_HALF;
    g = au_mute || au_umute ? 0u : ui_bypass ? au_bypass_gain : au_gain;
    if (nam_on && nam_ok && c1_ok && nam_eng == NAM_ENG_ONYX) {   /* v0.18: guitar (L) -> Onyx engine on core 1 -> both outputs */
        const float gf = (float)g * (1.0f / 65536.0f) * 8388608.0f * NAM_OUT_TRIM * onyx_head_scale();
        const uint32_t slot = pipe_blk & 1u, prev = slot ^ 1u;
        uint32_t t1 = fm1_ticks(), f, ran = 0;
        float *nin = onyx_au_in[slot];
        for (f = 0; f < AU_HALF / 2u; f++) {
            int32_t x = (int32_t)((uint32_t)in[2u * f] << 8) >> 8;
            uint32_t ax = (uint32_t)(x < 0 ? -x : x);
            if (ax > au_peak[0])
                au_peak[0] = ax;
            if (ax > au_in_max)
                au_in_max = ax;
            if (ax >= 0x7F0000u) {
                au_clip_in++;
                au_clip_in_ev++;
            }
            nin[f] = (float)x * (nam_in * (1.0f / 8388608.0f));
            if (cap_state == 1u && ax > (1u << 20))
                cap_state = 4;                    /* > -18 dBFS: start here */
        }
        if (cap_state == 4u) {                    /* v0.18 capture (JBW): from a known state, core 1 idle first */
            uint32_t w0 = fm1_ticks();
            while (pipe_busy && fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - w0 < 5000u * FM1_TICKS_PER_US)
                FM1_C1_SYNC();
            pipe_busy = 0;
            onyx_reset();
            { float hs = onyx_head_scale(), ni = nam_in, gg = gf, tr = NAM_OUT_TRIM;
              CAP_INFO[0] = 0x4943424Au; memcpy((void *)&CAP_INFO[1], &hs, 4); memcpy((void *)&CAP_INFO[2], &ni, 4);
              memcpy((void *)&CAP_INFO[3], &gg, 4); CAP_INFO[4] = onyx_weights_addr(); CAP_INFO[5] = g; memcpy((void *)&CAP_INFO[6], &tr, 4); }
            cap_n = 0;
            cap_state = 2;
        }
        if (pipe_busy) {                          /* core 1's block n - 1: it has had a whole half */
            uint32_t w0 = fm1_ticks(), w;
            while (fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - w0 < 1500u * FM1_TICKS_PER_US)
                FM1_C1_SYNC();
            w = (fm1_ticks() - w0) / FM1_TICKS_PER_US;
            if (w > pipe_late_max)
                pipe_late_max = w;
            if (fm1_c1_mb.done == fm1_c1_mb.job) {
                pipe_busy = 0;
                ran = pipe_job_blk + 1u == pipe_blk;
            } else
                pipe_late++;                      /* this half: silence */
        }
        if ((ALNK_CON2 & 0x30u) == 0x30u)
            au_out_late++;
        for (f = 0; f < AU_HALF / 2u; f++) {
            float yf = ran ? onyx_au_out[prev][f] * gf : 0.0f;
            float ya = yf < 0.0f ? -yf : yf;
            int32_t y;
            uint32_t ay;
            if (ya > LIM_T) {                     /* soft knee, as the v0.13 path */
                float o = ya - LIM_T;
                if (o > LIM_R) {
                    au_clip_out++;
                    au_clip_out_ev++;
                }
                ya = LIM_T + LIM_R * o / (o + LIM_R);
                yf = yf < 0.0f ? -ya : ya;
            }
            y = (int32_t)yf;
            ay = (uint32_t)(y < 0 ? -y : y);
            if (ay > au_peak[1])
                au_peak[1] = ay;
            out[2u * f] = out[2u * f + 1u] = y;
        }
        if (cap_state == 2u || cap_state == 5u) {  /* the output of the half before (raw head output, float bits), this half's input */
            if (cap_n > 0u)
                for (f = 0; f < AU_HALF / 2u; f++)
                    cap_out[cap_n - 1u][f] = ran ? *(const int32_t *)&onyx_au_out[prev][f] : 0x7FFFFFFF;
            if (cap_state == 5u)
                cap_state = 3;
            else {
                for (f = 0; f < AU_HALF / 2u; f++)
                    cap_in[cap_n][f] = *(const int32_t *)&nin[f];
                if (++cap_n == CAP_BLK)
                    cap_state = 5;
            }
        }
        if (!pipe_busy && fm1_cpu1_run(onyx_au_job, slot)) {
            pipe_busy = 1;
            pipe_job_blk = pipe_blk;
        }
        pipe_primed = 1;
        pipe_blk++;
        nam_us = (fm1_ticks() - t1) / FM1_TICKS_PER_US;
        if (nam_us > nam_us_max)
            nam_us_max = nam_us;
        goto done;
    }
    if (nam_on && nam_ok && c1_ok && nam_eng != NAM_ENG_ONYX) {   /* guitar (L) -> A2-Lite (two cores) -> both outputs */
        const float gf = (float)g * (1.0f / 65536.0f) * 8388608.0f * NAM_OUT_TRIM;
        const uint32_t slot = pipe_blk & 1u, prev = slot ^ 1u;
        uint32_t t1 = fm1_ticks(), f, ran = 0;
        a2l_t *nin = pipe.in[slot];
        for (f = 0; f < AU_HALF / 2u; f++) {
            int32_t x = (int32_t)((uint32_t)in[2u * f] << 8) >> 8;
            uint32_t ax = (uint32_t)(x < 0 ? -x : x);
            if (ax > au_peak[0])
                au_peak[0] = ax;
            if (ax > au_in_max)
                au_in_max = ax;
            if (ax >= 0x7F0000u) {
                au_clip_in++;
                au_clip_in_ev++;
            }
            nin[f] = (float)x * (nam_in * (1.0f / 8388608.0f));
            if (cap_state == 1u && ax > (1u << 20))
                cap_state = 4;                    /* > -18 dBFS: start here */
        }
        if (cap_state == 4u) {                    /* from a known state, as the host starts: reset (~0.1 s of
                                                   * no audio), core 1 idle first */
            uint32_t w0 = fm1_ticks();
            while (pipe_busy && fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - w0 < 5000u * FM1_TICKS_PER_US)
                FM1_C1_SYNC();
            pipe_busy = 0;
            a2lx_reset(&nam, &namq);
            cap_state = 2;
        }
        if (cap_state == 2u)
            for (f = 0; f < AU_HALF / 2u; f++)
                cap_in[cap_n][f] = (int32_t)((uint32_t)in[2u * f] << 8) >> 8;
        jb_stage(81);
        nam_front_slot(slot);                     /* core 0: front of block n */
        jb_stage(82);
        if (pipe_busy) {                          /* core 1's back of block n - 1, started at the end of the last
                                                   * call: it has had a whole block, so this rarely waits */
            uint32_t w0 = fm1_ticks(), w;
            while (fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - w0 < 1500u * FM1_TICKS_PER_US)
                FM1_C1_SYNC();
            w = (fm1_ticks() - w0) / FM1_TICKS_PER_US;
            if (w > pipe_late_max)
                pipe_late_max = w;
            if (fm1_c1_mb.done == fm1_c1_mb.job) {
                pipe_busy = 0;
                ran = pipe_job_blk + 1u == pipe_blk;
            } else
                pipe_late++;                      /* this block: silence (its back half is not there) */
        }
        if ((ALNK_CON2 & 0x30u) == 0x30u)
            au_out_late++;                        /* the DMA already finished the next half: this output is late */
        for (f = 0; f < AU_HALF / 2u; f++) {
            float yf = ran ? (float)pipe.qout[prev][f] * (gf * (1.0f / 8388608.0f)) : 0.0f;
            float ya = yf < 0.0f ? -yf : yf;
            int32_t y;
            uint32_t ay;
            if (ya > LIM_T) {                     /* soft knee: approaches full scale, never reaches it */
                float o = ya - LIM_T;
                if (o > LIM_R) {                  /* (would have been past full scale: shown as CLIP) */
                    au_clip_out++;
                    au_clip_out_ev++;
                }
                ya = LIM_T + LIM_R * o / (o + LIM_R);
                yf = yf < 0.0f ? -ya : ya;
            }
            y = (int32_t)yf;
            ay = (uint32_t)(y < 0 ? -y : y);
            if (ay > au_peak[1])
                au_peak[1] = ay;
            out[2u * f] = out[2u * f + 1u] = y;   /* RJ24 */
        }
        if (cap_state == 2u) {
            for (f = 0; f < AU_HALF / 2u; f++)
                cap_out[cap_n][f] = ran ? pipe.qout[prev][f] : 0x7FFFFFFF;
            if (++cap_n == CAP_BLK)
                cap_state = 3;
        }
        if (!pipe_busy && fm1_cpu1_run(nam_back_job, slot)) {   /* v0.13.18: core 1 starts block n's back now,
                                                   * not at the start of the next call: core 0 no longer waits
                                                   * for it, and the output is written ~250 us earlier */
            pipe_busy = 1;
            pipe_job_blk = pipe_blk;
        }
        jb_stage(83);
        pipe_primed = 1;
        pipe_blk++;
        nam_us = (fm1_ticks() - t1) / FM1_TICKS_PER_US;
        if (nam_us > nam_us_max)
            nam_us_max = nam_us;
        goto done;
    }
    pipe_primed = 0;
    if (nam_on && nam_ok && !c1_ok && 0) {        /* one core: too slow for real time (kept for reference) */
        static a2l_t nin[AU_HALF / 2u], nout[AU_HALF / 2u];
        const float gf = (float)g * (1.0f / 65536.0f) * 8388608.0f * NAM_OUT_TRIM;
        uint32_t t1 = fm1_ticks(), f;
        void (*volatile run)(struct a2l *, const a2l_t *, a2l_t *, uint32_t) = a2l_process;
        for (f = 0; f < AU_HALF / 2u; f++)
            nin[f] = (float)((int32_t)((uint32_t)in[2u * f] << 8) >> 8) * (1.0f / 8388608.0f);
        run(&nam, nin, nout, AU_HALF / 2u);
        for (f = 0; f < AU_HALF / 2u; f++) {
            float yf = nout[f] * gf;
            yf = yf > 8388607.0f ? 8388607.0f : yf < -8388608.0f ? -8388608.0f : yf;
            out[2u * f] = out[2u * f + 1u] = (int32_t)yf;
        }
        nam_us = (fm1_ticks() - t1) / FM1_TICKS_PER_US;
        if (nam_us > nam_us_max)
            nam_us_max = nam_us;
        goto done;
    }
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
        if (!(i & 1u) && ax > au_peak[0])
            au_peak[0] = ax;
        if (ax > au_in_max)
            au_in_max = ax;
        if (ax >= 0x7F0000u) {                    /* within 0.07 dB of full scale: the ADC is at its limit */
            au_clip_in++;
            au_clip_in_ev++;
        }
        if (y > 0x7FFFFF) {
            y = 0x7FFFFF;
            au_clip_out++;
            au_clip_out_ev++;
        }
        if (y < -0x800000) {
            y = -0x800000;
            au_clip_out++;
            au_clip_out_ev++;
        }
        {
            uint32_t ay = (uint32_t)(y < 0 ? -y : y);
            if (ay > au_peak[1])
                au_peak[1] = ay;
        }
        switch (au_fmt) {                         /* 24-bit sample y -> the DAC word */
        case 0: out[i] = (int32_t)((uint32_t)y << 8); break;
        case 1: out[i] = (int32_t)((uint32_t)y << 7); break;
        case 2: out[i] = (int32_t)((uint32_t)y << 9); break;
        case 3: out[i] = (int32_t)((uint32_t)(y >> 8) << 16); break;
        default: out[i] = (int32_t)y; break;
        }
    }
done:
    au_rx[h * AU_HALF + AU_HALF - 1u] = (int32_t)AU_SENT;   /* read: marked until the DMA fills it again */
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    if (us > au_us_max)
        au_us_max = us;
}

/* ----------------------------------------------------------------- flash --- */
/* interrupts are never enabled: call the RAM driver directly (no irq_restore / sti) */
static uint8_t fl_page[256] __attribute__((aligned(4)));
static uint32_t flash_id;
static uint8_t flash_ok;

/* v0.13.2: interrupts are on (the CPU exception; the profiler's TIMER5 once the NAM runs), and their entries are
 * in XIP: off around every flash operation (the flash is off inside them) */
static int fl_read(uint32_t off, void *p, uint32_t n)
{
    int rc;
    fm1_irq_off();
    rc = FL_FAR(fl_read_ram)(off, (uint8_t *)p, n);
    fm1_irq_on();
    return rc;
}

static int fl_erase_sector(uint32_t off)
{
    uint32_t took;
    int rc;
    fm1_irq_off();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    fm1_irq_on();
    return rc;
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
        fm1_irq_off();
        rc = FL_FAR(fl_prog_ram)(off, fl_page, chunk, &took);
        fm1_irq_on();
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
static void jb_stage(uint32_t n) { STAGE(n); }

/* ------------------------------------------------------- the profiler --- */
/* v0.13.2: TIMER5 at 1 kHz samples where each core is (core 0: the interrupted PC; core 1: its PCRS) into
 * .noinit; after a watchdog reset the next boot logs the last samples (a hang shows where; samples that stop
 * mean even the interrupt stopped: the bus) */
#define PROF_MAGIC 0x464F5250u                    /* "PROF" */
static struct {
    uint32_t magic, n, pc0[8], pc1[8], stage[8];
} prof __attribute__((section(".noinit")));
void jb_prof_irq(uint32_t pc)
{
    uint32_t i = prof.n & 7u;
    *(volatile uint32_t *)0x10900u |= 0x4000u;    /* TIMER5 pending clear */
    prof.pc0[i] = pc;
    prof.pc1[i] = *(volatile uint32_t *)0x1EEF27Cu;   /* core 1's PCRS */
    prof.stage[i] = crumb.stage;
    prof.n++;
}
extern void isr_jbprof(void);
static void prof_start(void)
{
    uint32_t i;
    prof.magic = PROF_MAGIC;
    prof.n = 0;
    for (i = 0; i < 8u; i++)
        prof.pc0[i] = prof.pc1[i] = prof.stage[i] = 0;
    fm1_irq_off();
    *(volatile uint32_t *)0x10900u = 0x4000u;     /* TIMER5: OSC / 4 = 6 MHz, PRD 6000 -> 1 kHz, IRQ 63 */
    *(volatile uint32_t *)0x10904u = 0;
    *(volatile uint32_t *)0x10908u = 6000u;
    fm1_irq_attach(63u, isr_jbprof, 6u);
    *(volatile uint32_t *)0x10900u = 0x4019u;
    fm1_irq_on();
}


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

/* v0.13.14 (JBH): the HSB divider (SYS_DIV bits 0..7, SDK: hsb_clk = sys_pll / div), set from RAM: the flash clock is
 * derived from it. Not kept across a reset. Test: does a slower bus stop the two-core faults? */
__attribute__((section(".ram_text"), noinline, used)) static void hsb_div_ram(uint32_t v)
{
    uint32_t i;
    __asm__ volatile("csync" ::: "memory");
    *(volatile uint32_t *)0x10008u = (*(volatile uint32_t *)0x10008u & ~0xFFu) | (v & 0xFFu);
    __asm__ volatile("csync" ::: "memory");
    for (i = 0; i < 1000u; i++)
        __asm__ volatile("" ::: "memory");
}

/* CPU clock probe: 200000 iterations of a fixed loop timed with TIMER4 (24 MHz); the loop's
 * instruction count is read from the disassembly (build/fw/juicebox.dis, clk_loop) */
static __attribute__((noinline)) uint32_t clk_loop(uint32_t n)
{
    uint32_t t0 = fm1_ticks();
    while (n--)
        __asm__ volatile("nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop" ::: "memory");
    return fm1_ticks() - t0;
}

/* v0.13.17: the two-core NAM on (boot and JBN): core supply, core 1's write limits, core 1 started (once), the
 * pipeline on, the screen updated. VDC14 is already 1.50 V (jb_main) */
static uint8_t ui_bypass;                         /* v0.15: MOD, the NAM bypassed (dry signal) */
static void nam_start(void)
{
    fm1_p33_write(0x09, (uint8_t)((fm1_p33_read(0x09) & 0xF0u) | nam_vlevel));
    fm1_delay_ms(2);
    c1_guard_setup();
    if (!c1_ok)
        c1_ok = fm1_cpu1_start() == 0;
    if (nam_eng == NAM_ENG_ONYX)
        CX2(0x34Cu) = 0;                          /* v0.18: core 1 writes the accelerator, the low SRAM and the driver's state */
    prof_start();
    pipe_primed = 0;
    nam_on = nam_ok && c1_ok && !ui_bypass;
}

/* --------------------------------------------------------- capture store --- */
/* v0.14: captures in flash, written over USB (tools/jb_models.py; later the installer), no rebuild. A slot is a
 * 64-byte header, then the A2-Lite weights as little-endian float32 in .nam order (tools/nam2c.py --slot). The host
 * writes the weights first and the header last, so a slot reads valid only once complete; "commit" checks both
 * CRCs. v0.15: no built-in capture (none stored: the dry signal). Flash work needs core 1 held (its code runs from XIP and the
 * flash is off during a write): the NAM pauses (no audio) for store commands. Core 1 started a second time faults
 * (v0.14.0 on the pedal: write-limit error inside fm1_cpu1_start, DBG_MSG 0x800), so the way back is a reboot, which
 * loads the saved capture: on select / resume, or on its own MS_IDLE_MS after the last command.
 *
 * SysEx (host -> pedal), replies F0 7D 'J' 'B' cmd ... F7, rc 0 = ok, else the error code:
 *   d               directory: per slot  d slot valid pack7(header 64 B), then  d 7F active MS_PROTO
 *   e slot          erase the slot (pauses the NAM)                   -> e slot rc
 *   w slot o3 data  program pack7(data <= 256 B) at slot + o (o: 3 x 7 bits, LSB first), erased slots only
 *                                                                     -> w slot rc
 *   c slot          check the header and the weights' CRC: the slot becomes valid     -> c slot rc
 *   s slot          the slot on the running bank position, saved, then reboot        -> s slot rc
 *   t               v0.16: the bank record                             -> t pack7(64 B)
 *   u pack7(64 B)   v0.16: a new bank record (CRC checked), saved, then reboot         -> u 0 rc
 *   p a1 .. an      v0.17: the P33 registers a1..an (7-bit addresses), then PC9 and the A B C H inputs (3 x 7 bits
 *                   each)                                              -> p v1 .. vn (2 x 7 bits each) pc9 ports
 *   l m1 m2 s       v0.16: LEDs := mask (m1 | m2 << 7, a 1 lights) for s seconds          -> l 0 0
 *   z m             (v0.15 test) core 1 held and started again, no flash work: variant m (0 as nam_start,
 *                   1 its write limits off while it starts, 2 = 1 + the fault flags cleared first)
 *                                                                     -> z m start job ok  (NAM stays off)
 *   x               wipe the store and the settings (before a return to the stock firmware)  -> x 0 rc
 *   r               reboot (back to playing after put / wipe)                        -> r 0 0 */
#define MS_MAGIC 0x314D424Au                     /* "JBM1" */
#define MS_PROTO 1u
#define MS_HDR 64u
#define MS_DATA (A2L_NWEIGHTS * 4u)
#define MS_SET_MAGIC 0x5453424Au                 /* "JBST" */
#define MS_NONE 0x7Fu                            /* no capture */
#define MS_IDLE_MS 20000u
struct ms_hdr {
    uint32_t magic;
    uint16_t hlen, ver;                          /* 64, 1 */
    uint32_t nw, dcrc, rate;                     /* A2L_NWEIGHTS, CRC-32 of the weights, sample rate */
    float norm;                                  /* linear loudness trim to -18 dB (nam2c.py) */
    char title[24];                              /* ASCII, 0-padded */
    uint32_t rsvd[3];
    uint32_t hcrc;                               /* CRC-32 of the 60 bytes above */
};
typedef char ms_hdr_size_check[sizeof(struct ms_hdr) == MS_HDR ? 1 : -1];
static struct ms_hdr ms_dir[MS_SLOTS];           /* the headers as read (boot, commit) */
static uint8_t ms_valid, ms_erased;              /* masks: a valid capture; erased in this session (writable) */
static uint8_t ms_active = MS_NONE;              /* the capture running */
static uint8_t ms_list[MS_SLOTS], ms_n;          /* the valid slots in order: the pedal's list */
static uint8_t ms_xip_ok;                        /* v0.15: the store reads right through XIP (live switching) */
static uint32_t bat_raw[4];                      /* v0.16: [3] = PC9 (raw 10 bits): half the battery voltage */
/* v0.17: the UI in English (default) and Brazilian Portuguese, chosen in the bank menu, kept in the bank record */
enum { S_USB_WRITE, S_REBOOT, S_EMPTY, S_BANK, S_EMPTY_POS, S_SEND_USB, S_MUTE, S_FX_OFF, S_UNSAVED, S_CAPTURES,
       S_OF, S_MAX, S_NAM_OFF, S_BATTERY, S_INFO, S_HOME_BACK, S_SAVING, S_SAVE_ERR, S_LOADING, S_LOAD_ERR,
       S_MENU, S_M_BANK, S_M_CAPA, S_M_CAPB, S_M_IN, S_M_OUT, S_M_LANG, S_M_SAVE, S_M_BACK, S_LANG, S_N };
static const char *const STR[2][S_N] = {
    {"USB: writing captures…", "Restarting…", "(empty)", "BANK ", "Empty slot · click PARA", "Send captures over USB",
     "MUTED · press left switch", "EFFECT OFF · dry signal", "Not saved · hold PARA", "Captures: ", " of ", "max",
     "NAM: off", "Battery: ", "INFO", "HOME to go back", "Saving…", "Could not save", "Loading…",
     "Could not load the capture", "BANK MENU", "Bank", "Capture A", "Capture B", "Input (IN)", "Output (OUT)",
     "Language", "Save", "Back", "English"},
    {"USB: gravando capturas…", "Reiniciando…", "(vazio)", "BANCO ", "Posição vazia · clique PARA",
     "Envie capturas pelo USB", "MUDO · pise no esquerdo", "EFEITO DESLIGADO · som direto", "Não salvo · segure PARA",
     "Capturas: ", " de ", "máx", "NAM: desligado", "Bateria: ", "INFORMAÇÕES", "HOME para voltar", "Salvando…",
     "Erro ao salvar", "Carregando…", "Erro ao carregar a captura", "MENU DO BANCO", "Banco", "Captura A", "Captura B",
     "Entrada (IN)", "Saída (OUT)", "Idioma", "Salvar", "Voltar", "Português"},
};
static uint8_t ui_lang;                          /* 0 English, 1 Português */
static uint8_t ui_usb;                           /* a USB host (also the "on 5 V" sign for now) */
#define T(id) (STR[ui_lang][id])
static uint8_t led_test, led_test_on;            /* v0.16: 'l': a fixed LED mask for a while (mapping) */
static uint32_t led_test_until;
static void ui_message(const char *s, uint16_t c);
static uint8_t ms_paused;                        /* the NAM is stopped for flash work */
static uint32_t ms_last_ms;
static uint8_t ms_buf[260] __attribute__((aligned(4)));

static uint32_t crc32_upd(uint32_t c, const uint8_t *p, uint32_t n)   /* zlib's CRC-32 */
{
    uint32_t k;
    c = ~c;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static int ms_hdr_ok(const struct ms_hdr *h)
{
    return h->magic == MS_MAGIC && h->hlen == MS_HDR && h->ver == 1u && h->nw == A2L_NWEIGHTS &&
           h->hcrc == crc32_upd(0, (const uint8_t *)h, MS_HDR - 4u) && h->norm > 0.0f && h->norm < 1000.0f;
}

static void ms_scan(void)
{
    uint32_t i;
    ms_valid = 0;
    for (i = 0; i < MS_SLOTS; i++)
        if (fl_read(MS_BASE + i * MS_SLOT_LEN, &ms_dir[i], MS_HDR) == 0 && ms_hdr_ok(&ms_dir[i]))
            ms_valid |= (uint8_t)(1u << i);
}

static int ms_read_data(uint32_t slot, uint8_t *w)   /* the weights into w (MS_DATA bytes), CRC checked */
{
    uint32_t off = MS_BASE + slot * MS_SLOT_LEN + MS_HDR, i, n;
    for (i = 0; i < MS_DATA; i += n) {
        n = MS_DATA - i > 1024u ? 1024u : MS_DATA - i;
        if (fl_read(off + i, w + i, n))
            return 2;
        poll();
    }
    return crc32_upd(0, w, MS_DATA) == ms_dir[slot].dcrc ? 0 : 3;
}

static void ms_list_build(void)
{
    uint32_t i;
    for (i = ms_n = 0; i < MS_SLOTS; i++)
        if (ms_valid >> i & 1u)
            ms_list[ms_n++] = (uint8_t)i;
}

/* load a capture into the NAM (stopped: boot, or paused): 0 = ok; on an error the running one stays */
/* v0.18: load a capture's 1871 weights into the engine in use */
static int nam_load(const float *w)
{
    if (nam_eng == NAM_ENG_ONYX) {
        static uint32_t r[8];
        onyx_prepare(nam_ring, sizeof nam_ring, w, 1871u, r);
        return r[0] == 0u && r[1] ? 0 : 1;
    }
    if (a2l_load(&nam, w, A2L_NWEIGHTS, nam_ring))
        return 1;
    a2lx_load(&namq, &nam);
    a2lx_reset(&nam, &namq);
    return 0;
}

static int ms_use(uint32_t slot)
{
    uint32_t rc;
    if (slot >= MS_SLOTS || !(ms_valid >> slot & 1u))
        return 1;
    cap_state = 0;                               /* the JBW capture buffer is the scratch */
    rc = (uint32_t)ms_read_data(slot, (uint8_t *)cap_in);
    if (rc)
        return (int)rc;
    nam_ok = nam_load((const float *)cap_in) == 0;   /* (v0.13: the back half in fixed point, prewarmed through it) */
    nam_trim = ms_dir[slot].norm * NAM_OUT_BOOST;
    ms_active = (uint8_t)slot;
    return nam_ok ? 0 : 4;
}

/* v0.15: the store as an unencrypted XIP window (as Felucca's fl_plain_window_init, from the store up: our code is
 * far below), checked against the flash driver's reads. Then a capture loads straight from flash while core 1 runs */
static void ms_xip_init(void)
{
    static uint8_t a[64];
    uint32_t k, i, off;
    const volatile uint8_t *x;
    SFCENC_CON &= (uint8_t)~2u;
    SFCENC_UNENC_L = FL_XIP(MS_BASE);
    SFCENC_UNENC_H = 0x07FFFFFFu;
    SFCENC_CON |= 2u;
    fm1_irq_off();
    fl_inval(MS_BASE, 0x000C2000u - MS_BASE);    /* drop any lines read decrypted */
    fm1_irq_on();
    ms_xip_ok = 1;
    for (k = 0; k <= MS_SLOTS && ms_xip_ok; k++) {   /* every slot's header, then the settings */
        off = k < MS_SLOTS ? MS_BASE + k * MS_SLOT_LEN : MS_SET;
        if (fl_read(off, a, sizeof a)) {
            ms_xip_ok = 0;
            break;
        }
        x = (const volatile uint8_t *)FL_XIP(off);
        for (i = 0; i < sizeof a; i++)
            if (x[i] != a[i]) {
                ms_xip_ok = 0;
                break;
            }
    }
}

/* v0.15: switch the running capture live from XIP (no flash driver, core 1 idle but running): ~0.2 s of silence */
static int ms_switch(uint32_t slot)
{
    const uint8_t *src;
    uint32_t w0;
    if (!ms_xip_ok || slot >= MS_SLOTS || !(ms_valid >> slot & 1u) || !c1_ok)
        return 1;
    src = (const uint8_t *)FL_XIP(MS_BASE + slot * MS_SLOT_LEN + MS_HDR);
    if (crc32_upd(0, src, MS_DATA) != ms_dir[slot].dcrc)
        return 3;
    au_mute = 1;                                 /* silence out, then the DMA repeats silent halves meanwhile */
    delay_poll(12);
    nam_on = 0;
    w0 = fm1_ticks();
    while (pipe_busy && fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - w0 < 5000u * FM1_TICKS_PER_US)
        FM1_C1_SYNC();
    pipe_busy = 0;
    nam_ok = nam_load((const float *)src) == 0;
    nam_trim = ms_dir[slot].norm * NAM_OUT_BOOST;
    ms_active = (uint8_t)slot;
    pipe_primed = 0;
    nam_on = nam_ok && c1_ok && !ui_bypass;
    delay_poll(12);                              /* the pipeline refilled */
    au_mute = 0;
    return nam_ok ? 0 : 4;
}

/* v0.16: the settings sector holds bank records (64 B, appended; the last valid one counts; erased when full or
 * foreign, as the v0.14 "JBST" records are). 6 banks (the FX, AMP, MOD, DLY, REV, CAB buttons) of 2 captures
 * (A / B: slot or MS_NONE) with IN / OUT in half dB, and the bank and side to start on. */
#define BK_MAGIC 0x314B424Au                     /* "JBK1" */
#define BK_N 6u
struct bk_bank { uint8_t cap[2]; int8_t in_hd, out_hd; };
struct bk_rec {
    uint32_t magic;
    uint8_t bank, ab, lang, rsvd0;
    struct bk_bank b[BK_N];
    uint8_t rsvd[28];
    uint32_t crc;                                /* CRC-32 of the 60 bytes above */
};
typedef char bk_rec_size_check[sizeof(struct bk_rec) == 64u ? 1 : -1];
static struct bk_rec bk, bk_saved;               /* running, as last saved */

static int bk_ok(const struct bk_rec *r)
{
    uint32_t i;
    if (r->magic != BK_MAGIC || r->crc != crc32_upd(0, (const uint8_t *)r, 60) || r->bank >= BK_N || r->ab > 1u ||
        r->lang > 1u)
        return 0;
    for (i = 0; i < BK_N; i++)
        if (r->b[i].in_hd < -24 || r->b[i].in_hd > 24 || r->b[i].out_hd < -48 || r->b[i].out_hd > 24)
            return 0;
    return 1;
}

static void bk_defaults(void)                    /* the stored captures in order: bank 1 A, 1 B, 2 A, ... */
{
    uint32_t i;
    for (i = 0; i < sizeof bk; i++)
        ((uint8_t *)&bk)[i] = 0;
    bk.magic = BK_MAGIC;
    for (i = 0; i < 2u * BK_N; i++)
        bk.b[i / 2u].cap[i & 1u] = i < ms_n ? ms_list[i] : (uint8_t)MS_NONE;
}

static int bk_read(void)                         /* the last valid record into bk: 1 = found */
{
    static struct bk_rec r;
    uint32_t off, found = 0;
    for (off = 0; off < 0x1000u; off += 64u) {
        if (fl_read(MS_SET + off, &r, 64) || r.magic != BK_MAGIC)
            break;
        if (bk_ok(&r)) {
            bk = r;
            found = 1;
        }
    }
    return (int)found;
}

static int bk_write(void)                        /* bk as a new record (flash: core 1 held, see ms_pause) */
{
    uint32_t off, w[16], i, blank;
    bk.crc = crc32_upd(0, (const uint8_t *)&bk, 60);
    for (off = 0; off < 0x1000u; off += 64u) {
        if (fl_read(MS_SET + off, w, 64))
            return 2;
        for (i = 0, blank = 1; i < 16u; i++)
            blank &= w[i] == 0xFFFFFFFFu;
        if (blank)
            break;
        if (w[0] != BK_MAGIC) {
            off = 0x1000u;
            break;
        }
    }
    if (off >= 0x1000u) {
        if (fl_erase_sector(MS_SET))
            return 5;
        off = 0;
    }
    return fl_prog(MS_SET + off, (const uint8_t *)&bk, 64) ? 6 : 0;
}

static int bk_dirty(void)                        /* the banks differ from the saved ones (not the bank / side) */
{
    uint32_t i;
    if (bk.lang != bk_saved.lang)
        return 1;
    for (i = 0; i < BK_N; i++)
        if (bk.b[i].cap[0] != bk_saved.b[i].cap[0] || bk.b[i].cap[1] != bk_saved.b[i].cap[1] ||
            bk.b[i].in_hd != bk_saved.b[i].in_hd || bk.b[i].out_hd != bk_saved.b[i].out_hd)
            return 1;
    return 0;
}

static float db_half(int32_t hd)                 /* 10^(hd / 40): half-dB steps, no libm */
{
    float v = 1.0f;
    const float k = hd < 0 ? 0.94406088f : 1.05925373f;
    if (hd < 0)
        hd = -hd;
    while (hd--)
        v *= k;
    return v;
}

static uint32_t bk_slot(void)                    /* the running position's capture, MS_NONE if empty / gone */
{
    uint32_t s = bk.b[bk.bank].cap[bk.ab];
    return s < MS_SLOTS && (ms_valid >> s & 1u) ? s : MS_NONE;
}

static void ms_pause(void)                       /* NAM and audio off, core 1 held: flash work is safe */
{
    ms_last_ms = now_ms();
    if (ms_paused)
        return;
    audio_stop();
    if (c1_ok) {                                 /* never held mid-job (as the update entry) */
        uint32_t w0 = fm1_ticks();
        while (pipe_busy && fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - w0 < 5000u * FM1_TICKS_PER_US)
            FM1_C1_SYNC();
        pipe_busy = 0;
        fm1_cpu1_hold();
        c1_ok = 0;
    }
    nam_on = 0;
    ms_paused = 1;
    ui_message(T(S_USB_WRITE), C_WARN);
}

static void ms_resume(void)                      /* a reboot: core 1 cannot be started twice (see above) */
{
    bootguard.pending = 0;                       /* a wanted reboot, not a failed boot (two = the rescue loader) */
    bootguard.failed = 0;
    BG_SEAL();
    ui_message(T(S_REBOOT), C_WARN);
    delay_poll(100);                             /* the reply goes out */
    usb_detach();
    fm1_delay_ms(30);
    fm1_reboot();
}

static void ms_reply(uint8_t cmd, uint32_t a, uint32_t b)
{
    uint8_t m[8];
    m[0] = 0xF0; m[1] = 0x7D; m[2] = 'J'; m[3] = 'B'; m[4] = cmd;
    m[5] = (uint8_t)(a & 0x7Fu); m[6] = (uint8_t)(b & 0x7Fu); m[7] = 0xF7;
    ota_wire_send(m, 8);
}

static void ms_dir_send(void)
{
    static uint8_t msg[96];
    uint32_t i, k;
    for (i = 0; i < MS_SLOTS; i++) {
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'd';
        msg[5] = (uint8_t)i;
        msg[6] = (uint8_t)(ms_valid >> i & 1u);
        k = 7u + ota_pack7((const uint8_t *)&ms_dir[i], MS_HDR, msg + 7);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    }
    msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'd';
    msg[5] = 0x7F; msg[6] = ms_active; msg[7] = MS_PROTO; msg[8] = ms_xip_ok; msg[9] = bk.bank; msg[10] = 0xF7;
    ota_wire_send(msg, 11);
}

static int ms_erase_slot(uint32_t slot)
{
    uint32_t i;
    ms_valid &= (uint8_t)~(1u << slot);
    for (i = 0; i < MS_SLOT_LEN; i += 0x1000u) {
        if (fl_erase_sector(MS_BASE + slot * MS_SLOT_LEN + i))
            return 5;
        poll();
    }
    ms_erased |= (uint8_t)(1u << slot);
    return 0;
}

/* a store command (p: the frame without F0 / F7, p[3] the command); 1 = it was one */
static int ms_service(const uint8_t *p, uint32_t n)
{
    uint32_t slot, rc, i;
    uint8_t cmd;
    if (n < 4u || p[0] != 0x7D || p[1] != 'J' || p[2] != 'B')
        return 0;
    cmd = p[3];
    slot = n > 4u ? p[4] : 0u;
    if (cmd == 'd' && n == 4u) {
        ota_frame_done();
        ms_dir_send();
    } else if (cmd == 'e' && n == 5u) {
        ota_frame_done();
        ms_pause();
        rc = slot < MS_SLOTS && flash_ok ? (uint32_t)ms_erase_slot(slot) : 1u;
        ms_reply('e', slot, rc);
    } else if (cmd == 'w' && n >= 9u) {
        uint32_t off = (uint32_t)p[5] | (uint32_t)p[6] << 7 | (uint32_t)p[7] << 14;
        uint32_t len = ota_unpack7(p + 8, n - 8u, ms_buf, sizeof ms_buf);
        ota_frame_done();
        ms_pause();
        if (slot >= MS_SLOTS || !(ms_erased >> slot & 1u) || len > 256u || off + len > MS_SLOT_LEN)
            rc = 1;
        else
            rc = fl_prog(MS_BASE + slot * MS_SLOT_LEN + off, ms_buf, len) ? 6u : 0u;
        ms_reply('w', slot, rc);
    } else if (cmd == 'c' && n == 5u) {
        ota_frame_done();
        ms_pause();
        rc = 1;
        if (slot < MS_SLOTS) {
            rc = fl_read(MS_BASE + slot * MS_SLOT_LEN, &ms_dir[slot], MS_HDR) ? 2u
               : !ms_hdr_ok(&ms_dir[slot]) ? 7u : (uint32_t)ms_read_data(slot, (uint8_t *)cap_in);
            if (rc == 0) {
                ms_valid |= (uint8_t)(1u << slot);
                ms_erased &= (uint8_t)~(1u << slot);
            }
        }
        ms_reply('c', slot, rc);
    } else if (cmd == 's' && n == 5u) {
        ota_frame_done();
        ms_pause();
        rc = slot < MS_SLOTS && (ms_valid >> slot & 1u) ? 0u : 1u;
        if (rc == 0) {
            bk.b[bk.bank].cap[bk.ab] = (uint8_t)slot;
            rc = flash_ok ? (uint32_t)bk_write() : 2u;
        }
        ms_reply('s', slot, rc);
        ms_resume();
    } else if (cmd == 'x' && n == 4u) {
        ota_frame_done();
        ms_pause();
        rc = flash_ok ? 0u : 1u;
        for (i = 0; i < MS_SLOTS && !rc; i++)
            rc = (uint32_t)ms_erase_slot(i);
        if (!rc && fl_erase_sector(MS_SET))
            rc = 5;
        ms_reply('x', 0, rc);
    } else if (cmd == 'z' && n == 5u) {
        uint32_t st, job = 0x7Eu;
        ota_frame_done();
        ms_pause();                              /* core 1 held, as for flash work (none here) */
        delay_poll(50);
        if (slot >= 1u)
            CX2(0x34Cu) = 0;                     /* its write limits off while it starts */
        if (slot >= 2u) {
            CX2(0x248u) = 0xFFFFFFFFu;           /* DBG_MSG_CLR */
            *(volatile uint32_t *)0x1EEF2D4u = 0xFFFFFFFFu;   /* its EMU_MSG */
        }
        if (slot == 0u)
            c1_guard_setup();
        st = (uint32_t)fm1_cpu1_start();
        if (st == 0u) {
            c1_test_out = 0;
            job = fm1_cpu1_run(c1_int_job, 7u) ? (uint32_t)fm1_cpu1_wait() : 0x7Du;
        }
        if (slot >= 1u)
            c1_guard_setup();
        {
            uint8_t m[10];
            m[0] = 0xF0; m[1] = 0x7D; m[2] = 'J'; m[3] = 'B'; m[4] = 'z'; m[5] = (uint8_t)(slot & 0x7Fu);
            m[6] = (uint8_t)(st & 0x7Fu); m[7] = (uint8_t)(job & 0x7Fu); m[8] = c1_test_out == 22u; m[9] = 0xF7;
            ota_wire_send(m, 10);
        }
        fm1_cpu1_hold();                         /* paused again: the 20 s reboot follows */
        c1_ok = 0;
    } else if (cmd == 't' && n == 4u) {
        uint8_t m[96];
        uint32_t k;
        ota_frame_done();
        bk.crc = crc32_upd(0, (const uint8_t *)&bk, 60);
        m[0] = 0xF0; m[1] = 0x7D; m[2] = 'J'; m[3] = 'B'; m[4] = 't';
        k = 5u + ota_pack7((const uint8_t *)&bk, 64, m + 5);
        m[k++] = 0xF7;
        ota_wire_send(m, k);
    } else if (cmd == 'u' && n >= 4u + 74u) {
        static struct bk_rec r;
        uint32_t len = ota_unpack7(p + 4, n - 4u, (uint8_t *)&r, sizeof r);
        ota_frame_done();
        rc = len >= 64u && bk_ok(&r) ? 0u : 1u;
        if (rc == 0) {
            ms_pause();
            bk = r;
            rc = flash_ok ? (uint32_t)bk_write() : 2u;
        }
        ms_reply('u', 0, rc);
        if (rc == 0)
            ms_resume();
    } else if (cmd == 'p' && n >= 4u && n <= 4u + 24u) {
        static const uint8_t PORT[4] = {P_A, P_B, P_C, P_H};
        uint8_t m[96], ad[24];
        uint32_t k = 5u, i, na = n - 4u;
        for (i = 0; i < na; i++)
            ad[i] = p[4 + i];
        ota_frame_done();
        m[0] = 0xF0; m[1] = 0x7D; m[2] = 'J'; m[3] = 'B'; m[4] = 'p';
        for (i = 0; i < na; i++) {
            uint8_t v = fm1_p33_read(ad[i]);
            m[k++] = v & 0x7Fu;
            m[k++] = v >> 7;
        }
        m[k++] = (uint8_t)(bat_raw[3] & 0x7Fu);
        m[k++] = (uint8_t)(bat_raw[3] >> 7 & 0x7Fu);
        for (i = 0; i < 4u; i++) {
            uint32_t v = PORT_REG(PORT[i], R_IN) & 0xFFFFu;
            m[k++] = (uint8_t)(v & 0x7Fu);
            m[k++] = (uint8_t)(v >> 7 & 0x7Fu);
            m[k++] = (uint8_t)(v >> 14 & 0x7Fu);
        }
        m[k++] = 0xF7;
        ota_wire_send(m, k);
    } else if (cmd == 'l' && n == 7u) {
        ota_frame_done();
        led_test = (uint8_t)(p[4] | p[5] << 7);
        led_test_until = now_ms() + p[6] * 1000u;
        led_test_on = 1;
        {
            static char t[12] = "LED 0x00";
            hex(t + 6, led_test, 2);
            ui_message(t, C_HOT);
        }
        ms_reply('l', 0, 0);
    } else if (cmd == 'r' && n == 4u) {
        ota_frame_done();
        ms_reply('r', 0, 0);
        ms_resume();
    } else
        return 0;
    return 1;
}

/* -------------------------------------------------------------------- UI --- */
/* v0.16: banks. Panel -> actions in one place (din bits: DIN[] order, docs/06; 9..11 = the PC10 ladder); change
 * here to remap.
 *   FX AMP MOD DLY REV CAB   banks 1..6 (two captures each, A / B, with their IN / OUT levels)
 *   right footswitch         A <-> B in the bank
 *   left footswitch          tap: effect on / off (off = the dry signal); held: MUTE, a press un-mutes
 *   PARA click               the bank menu (bank, captures A / B, IN, OUT, save); turn: move / change
 *   PARA held 1 s            save (banks, and the bank / side to start on): a reboot, core 1 cannot restart
 *   HOME                     info screen / back          MASTER   output volume */
#define BTN_SEL (1u << 0)                         /* PH6: PARA click */
#define BTN_INFO (1u << 1)                        /* PA1: HOME */
#define BTN_FS_L (1u << 5)                        /* PA3: left footswitch */
#define BTN_FS_R (1u << 6)                        /* PA2: right footswitch */
static const uint32_t BTN_BANK[BK_N] = {1u << 2, 1u << 3, 1u << 4, 1u << 9, 1u << 10, 1u << 11};
static const char *const BANK_NAME[BK_N] = {"FX", "AMP", "MOD", "DLY", "REV", "CAB"};
static const uint8_t BANK_LED[BK_N] = {1, 2, 3, 4, 5, 6};   /* shift-register bits: to be mapped ('l') */
#define ENC_A 7u                                  /* PC1 */
#define ENC_B 8u                                  /* PC0: leads when turned clockwise */
#define UI_HOLD_MS 1000u                          /* PARA held: save */
#define UI_MUTE_MS 600u                           /* left footswitch held: mute */
#define UI_MSG_MS 2500u

#define C_TITLE RGB(246, 240, 230)
#define C_SUB RGB(150, 146, 140)
#define C_LINE RGB(40, 40, 46)
#define C_BAR RGB(40, 40, 46)
#define C_ROW RGB(34, 34, 40)

/* ---- battery (v0.17): PC9 (SARADC ch 8) = half the battery voltage, against a 3.3 V reference (to be calibrated).
 * The PMU's own VBAT / VBG channels read nothing here (v0.16), and the internal charger is off: the board has its
 * own charger chip (its LED). On 5 V = a USB host for now (its status pin: to be found, HOME shows the ports). */
#define BAT_REF_MV 6600u                          /* 3300 mV x 2 (the divider) at full scale */
static uint32_t bat_mv, bat_pct;
static uint8_t bat_5v, bat_shown = 0xFFu;
static void bat_update(int32_t pc9)
{
    /* v0.18: the top point 4180 -> 4150 mV: a full battery read 4161 mV here (the 3.3 V reference is assumed, not measured)
     * and showed 97 %. To redo with a multimeter (docs/07, battery calibration) */
    static const uint16_t MV[] = {3300, 3550, 3680, 3750, 3820, 3900, 3990, 4070, 4150};
    uint32_t mv, i;
    if (pc9 < 0)
        return;
    bat_raw[3] = (uint32_t)pc9;
    mv = (uint32_t)pc9 * BAT_REF_MV / 1023u;
    bat_mv = bat_mv ? (bat_mv * 3u + mv) / 4u : mv;
    bat_5v = ui_usb;
    for (i = 0; i + 1u < sizeof MV / sizeof MV[0] && bat_mv >= MV[i + 1u]; i++)
        ;
    bat_pct = bat_mv <= MV[0] ? 0u : i + 1u >= sizeof MV / sizeof MV[0] ? 100u
            : i * 100u / 8u + (bat_mv - MV[i]) * 100u / 8u / (MV[i + 1u] - MV[i]);
}

struct aafont { uint32_t h; const uint8_t *w; const uint16_t *off; const uint8_t *bmp; };
static const struct aafont FS = {FONT_S_H, FONT_S_W, FONT_S_OFF, FONT_S_BMP};
static const struct aafont FL = {FONT_L_H, FONT_L_W, FONT_L_OFF, FONT_L_BMP};

enum { UI_MAIN, UI_INFO, UI_MENU };
static uint8_t ui_screen, ui_dirty;
static uint32_t ui_msg_ms;
static const char *ui_msg;
static uint16_t ui_msg_c;

static uint32_t glyph_next(const char **s)        /* UTF-8 -> glyph index (fonts_aa.h); 0xFFFF at the end */
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = p[0], u;
    if (!c)
        return 0xFFFFu;
    if (c < 0x80u) {
        *s += 1;
        return c >= 32u && c < 127u ? c - 32u : 31u;   /* 31: '?' */
    }
    if ((c & 0xE0u) == 0xC0u && (p[1] & 0xC0u) == 0x80u) {
        u = (c & 0x1Fu) << 6 | (p[1] & 0x3Fu);
        *s += 2;
        return u >= 0xA0u && u <= 0xFFu ? 95u + (u - 0xA0u) : 31u;
    }
    if ((c & 0xF0u) == 0xE0u && p[1] && p[2]) {   /* 3 bytes: only the ellipsis is drawn ("...") */
        u = (c & 0x0Fu) << 12 | (p[1] & 0x3Fu) << 6 | (p[2] & 0x3Fu);
        *s += 3;
        return u == 0x2026u ? 0xFFFEu : 31u;
    }
    *s += 1;
    return 31u;
}

static uint32_t aa_width(const struct aafont *f, const char *s)
{
    uint32_t g, w = 0;
    while ((g = glyph_next(&s)) != 0xFFFFu)
        w += g == 0xFFFEu ? 3u * f->w[14] : f->w[g];
    return w;
}

/* text in the box [x, x + w) x [y, y + font height): the box is cleared to bg; align 0 left, 1 centre, 2 right */
static void aa_text(uint32_t x, uint32_t y, uint32_t w, const char *s, const struct aafont *f, uint16_t fg,
                    uint16_t bg, uint32_t align)
{
    uint16_t pal[16];
    uint32_t r, k, tw = aa_width(f, s), x0;
    for (k = 0; k < 16u; k++) {                   /* fg over bg, 16 alpha steps */
        uint32_t fr = fg >> 11, fgc = fg >> 5 & 63u, fb = fg & 31u, br = bg >> 11, bgc = bg >> 5 & 63u, bb = bg & 31u;
        pal[k] = (uint16_t)(((fr * k + br * (15u - k)) / 15u) << 11 | ((fgc * k + bgc * (15u - k)) / 15u) << 5 |
                            (fb * k + bb * (15u - k)) / 15u);
    }
    x0 = align == 1u && tw < w ? (w - tw) / 2u : align == 2u && tw < w ? w - tw : 0u;
    lcd_window(x, y, x + w - 1u, y + f->h - 1u);
    for (r = 0; r < f->h; r++) {
        const char *p = s;
        uint32_t g, px = x0, i;
        for (i = 0; i < w; i++) {
            lcd_buf[2u * i] = (uint8_t)(bg >> 8);
            lcd_buf[2u * i + 1u] = (uint8_t)bg;
        }
        while ((g = glyph_next(&p)) != 0xFFFFu && px < w) {
            uint32_t n = g == 0xFFFEu ? 3u : 1u, gi = g == 0xFFFEu ? 14u : g, gw = f->w[gi], c, rep;
            for (rep = 0; rep < n; rep++, px += gw) {
                const uint8_t *b = f->bmp + f->off[gi];
                for (c = 0; c < gw && px + c < w; c++) {
                    uint32_t idx = r * gw + c, a = b[idx >> 1];
                    a = idx & 1u ? a & 15u : a >> 4;
                    if (a) {
                        lcd_buf[2u * (px + c)] = (uint8_t)(pal[a] >> 8);
                        lcd_buf[2u * (px + c) + 1u] = (uint8_t)pal[a];
                    }
                }
            }
        }
        lcd_data(lcd_buf, w * 2u);
        poll();
    }
}

/* the capture's name in two lines of FONT_L, broken after a space, '-' or '_' when it can */
static void ui_wrap(const char *t, char *l1, char *l2, uint32_t maxw)
{
    char buf[25];
    uint32_t n = 0, i, cut, brk, w;
    while (n < 24u && t[n]) {
        buf[n] = t[n] >= 32 && t[n] < 127 ? t[n] : '?';
        n++;
    }
    buf[n] = 0;
    for (i = 0, w = 0, brk = 0, cut = n; i < n; i++) {
        w += FL.w[(uint8_t)buf[i] - 32u];
        if (w > maxw) {
            cut = brk ? brk : i;
            break;
        }
        if (buf[i] == ' ' || buf[i] == '-' || buf[i] == '_')
            brk = i + 1u;
    }
    for (i = 0; i < cut; i++)
        l1[i] = buf[i];
    l1[cut] = 0;
    while (buf[cut] == ' ')
        cut++;
    for (i = 0; cut + i <= n; i++)
        l2[i] = buf[cut + i];
    if (aa_width(&FL, l2) > maxw) {               /* too long for two lines: cut, "..." (buffers of 28) */
        uint32_t m = 0;
        while (l2[m])
            m++;
        do {
            m--;
            l2[m] = '.'; l2[m + 1u] = '.'; l2[m + 2u] = '.'; l2[m + 3u] = 0;
        } while (m && aa_width(&FL, l2) > maxw);
    }
}

static char *put_s(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}

static char *put_db_n(char *d, int32_t hd)        /* half dB -> "+1.5" */
{
    uint32_t u = (uint32_t)(hd < 0 ? -hd : hd);
    *d++ = hd < 0 ? '-' : '+';
    if (u / 2u >= 10u)
        *d++ = (char)('0' + u / 20u);
    *d++ = (char)('0' + u / 2u % 10u);
    *d++ = '.';
    *d++ = u & 1u ? '5' : '0';
    *d = 0;
    return d;
}

static char *put_db(char *d, int32_t hd)          /* half dB -> "+1.5 dB" */
{
    return put_s(put_db_n(d, hd), " dB");
}

static const char *slot_name(uint32_t s)
{
    return s < MS_SLOTS && (ms_valid >> s & 1u) ? ms_dir[s].title : T(S_EMPTY);
}

#define UI_TY 66u                                 /* the name: two lines of FONT_L */
#define UI_SY 136u                                /* the status line */
#define UI_MY 166u                                /* the meters: IN, OUT, VOL rows, 22 px apart */

static void ui_message(const char *s, uint16_t c)  /* the status line for UI_MSG_MS */
{
    ui_msg = s;
    ui_msg_c = c;
    ui_msg_ms = now_ms();
    if (ui_screen == UI_MAIN)
        aa_text(12, UI_SY, 216, s, &FS, c, C_BG, 0);
}

static void ui_vol(int32_t pot)
{
    uint32_t w = pot < 0 ? 0u : (uint32_t)pot * 136u / 1023u;
    if (ui_screen != UI_MAIN)
        return;
    if (w)
        lcd_fill(56, UI_MY + 2u * 22u + 6u, w, 8, C_HOT);
    if (w < 136u)
        lcd_fill(56u + w, UI_MY + 2u * 22u + 6u, 136u - w, 8, C_BAR);
}

static void ui_battery(void)                      /* top right: the percentage and a battery (green: on 5 V) */
{
    static char t[8];
    uint16_t c = bat_5v ? C_OK : bat_pct < 15u ? C_ERR : C_SUB;
    uint32_t w;
    if (ui_screen != UI_MAIN || !bat_mv)
        return;
    bat_shown = (uint8_t)(bat_pct | bat_5v << 7);
    {
        char *d = t;
        if (bat_5v && bat_pct < 100u)
            *d++ = '+';                           /* charging */
        if (bat_pct >= 100u)
            *d++ = '1';
        if (bat_pct >= 10u)
            *d++ = (char)('0' + bat_pct / 10u % 10u);
        *d++ = (char)('0' + bat_pct % 10u);
        put_s(d, "%");
    }
    aa_text(150, 6, 56, t, &FS, c, C_BG, 2);
    lcd_fill(210, 9, 20, 1, c);                   /* the outline, the tip, the fill */
    lcd_fill(210, 20, 20, 1, c);
    lcd_fill(210, 9, 1, 12, c);
    lcd_fill(229, 9, 1, 12, c);
    lcd_fill(230, 12, 2, 6, c);
    w = bat_pct * 16u / 100u;
    if (w)
        lcd_fill(212, 11, w, 8, c);
    if (w < 16u)
        lcd_fill(212u + w, 11, 16u - w, 8, C_BG);
}

static void ui_header(void)
{
    static char h[24];
    char *d = put_s(h, T(S_BANK));
    *d++ = (char)('1' + bk.bank);
    put_s(put_s(d, " · "), BANK_NAME[bk.bank]);
    aa_text(12, 6, 146, h, &FS, C_SUB, C_BG, 0);
    ui_battery();
}

static void ui_pill(uint32_t x, const char *s, uint32_t on)
{
    lcd_fill(x, 38, 30, 22, on ? C_HOT : C_ROW);
    aa_text(x + 3u, 39, 24, s, &FS, on ? C_BG : C_SUB, on ? C_HOT : C_ROW, 1);
}

static void ui_draw(uint32_t all)
{
    static char l1[28], l2[28], lv[32];
    const struct bk_bank *b = &bk.b[bk.bank];
    uint32_t slot = bk_slot();
    const char *sub = "";
    uint16_t subc = C_SUB, tc = C_TITLE;
    if (ui_screen != UI_MAIN)
        return;
    if (all) {
        lcd_fill(0, 0, LCD_W, LCD_H, C_BG);
        lcd_fill(12, 30, 216, 1, C_LINE);
        aa_text(12, UI_MY, 40, "IN", &FS, C_SUB, C_BG, 0);
        aa_text(12, UI_MY + 22u, 40, "OUT", &FS, C_SUB, C_BG, 0);
        aa_text(12, UI_MY + 44u, 40, "VOL", &FS, C_SUB, C_BG, 0);
    }
    ui_header();
    ui_pill(12, "A", bk.ab == 0u);
    ui_pill(46, "B", bk.ab == 1u);
    put_db_n(put_s(put_db_n(put_s(lv, "IN "), b->in_hd), " · OUT "), b->out_hd);
    aa_text(84, 40, 144, lv, &FS, C_DIM, C_BG, 2);
    if (slot == MS_NONE) {
        ui_wrap(T(S_EMPTY), l1, l2, 216);
        tc = C_DIM;
        sub = ms_n ? T(S_EMPTY_POS) : T(S_SEND_USB);
    } else
        ui_wrap(ms_dir[slot].title, l1, l2, 216);
    if (au_umute) {
        tc = C_DIM;
        sub = T(S_MUTE);
        subc = C_ERR;
    } else if (ui_bypass) {
        tc = C_DIM;
        sub = T(S_FX_OFF);
        subc = C_WARN;
    } else if (slot != MS_NONE && bk_dirty())
        sub = T(S_UNSAVED);
    aa_text(12, UI_TY, 216, l1, &FL, tc, C_BG, 0);
    aa_text(12, UI_TY + FL.h, 216, l2, &FL, tc, C_BG, 0);
    if (ui_msg && now_ms() - ui_msg_ms < UI_MSG_MS) {
        sub = ui_msg;
        subc = ui_msg_c;
    }
    aa_text(12, UI_SY, 216, sub, &FS, subc, C_BG, 0);
}

static char *put_u(char *d, uint32_t v)          /* decimal */
{
    char t[10];
    uint32_t n = 0;
    do
        t[n++] = (char)('0' + v % 10u);
    while ((v /= 10u) != 0u);
    while (n)
        *d++ = t[--n];
    *d = 0;
    return d;
}

static void ui_info(void)                         /* HOME: version, captures, CPU, battery, the input ports */
{
    static char l[48];
    uint32_t y = 40, k;
    char *d;
    if (ui_screen != UI_INFO)
        return;
    aa_text(12, y, 216, "Juice-Box " JB_VERSION, &FS, C_TITLE, C_BG, 0);
    y += 22u;
    put_u(put_s(put_u(put_s(l, T(S_CAPTURES)), ms_n), T(S_OF)), MS_SLOTS);
    aa_text(12, y, 216, l, &FS, C_SUB, C_BG, 0);
    y += 22u;
    d = put_u(put_s(l, "NAM: "), nam_us);
    d = put_u(put_s(put_s(put_s(d, " us ("), T(S_MAX)), " "), nam_us_max);
    put_s(d, ")");
    aa_text(12, y, 216, nam_on ? l : T(S_NAM_OFF), &FS, C_SUB, C_BG, 0);
    y += 22u;
    d = put_u(put_s(l, T(S_BATTERY)), bat_mv);
    put_s(put_u(put_s(d, " mV ("), bat_pct), "%)");
    aa_text(12, y, 216, l, &FS, C_SUB, C_BG, 0);
    y += 22u;
    d = put_u(put_s(l, "PC9 "), bat_raw[3]);
    put_s(d, ui_usb ? "  USB" : "");
    aa_text(12, y, 216, l, &FS, C_SUB, C_BG, 0);
    y += 22u;
    {                                             /* the ports' inputs: the charger's status pin, to be found */
        static const uint8_t PORT[4] = {P_A, P_B, P_C, P_H};
        d = l;
        for (k = 0; k < 4u; k++) {
            *d++ = "ABCH"[k];
            hex(d, PORT_REG(PORT[k], R_IN) & 0xFFFFu, 4);
            d += 4;
            *d++ = ' ';
        }
        *d = 0;
    }
    aa_text(12, y, 216, l, &FS, C_SUB, C_BG, 0);
    y += 22u;
    aa_text(12, y, 216, "ID " FELUCCA_ID, &FS, C_SUB, C_BG, 0);
}

static void ui_info_open(void)
{
    ui_screen = UI_INFO;
    lcd_fill(0, 0, LCD_W, LCD_H, C_BG);
    aa_text(12, 6, 216, T(S_INFO), &FS, C_SUB, C_BG, 0);
    lcd_fill(12, 30, 216, 1, C_LINE);
    aa_text(12, 214, 216, T(S_HOME_BACK), &FS, C_DIM, C_BG, 0);
    ui_info();
}

static void bk_save_reboot(void)                  /* the banks, the bank / side: saved, then a reboot */
{
    uint32_t rc;
    ms_pause();
    ui_message(T(S_SAVING), C_WARN);
    rc = flash_ok ? (uint32_t)bk_write() : 2u;
    if (rc)
        ui_message(T(S_SAVE_ERR), C_ERR);
    ms_resume();
}

static void bk_gains(void)
{
    nam_in = db_half(bk.b[bk.bank].in_hd);
    nam_out = db_half(bk.b[bk.bank].out_hd);
}

/* play the bank / side now: its levels, its capture (live from XIP; without XIP: saved, then a reboot) */
static void bk_go(uint32_t bank, uint32_t ab)
{
    uint32_t slot;
    bk.bank = (uint8_t)bank;
    bk.ab = (uint8_t)ab;
    slot = bk_slot();
    if (slot == MS_NONE) {                        /* an empty position: the dry signal */
        au_mute = 1;
        delay_poll(12);
        nam_on = 0;
        nam_ok = 0;
        ms_active = MS_NONE;
        bk_gains();
        delay_poll(6);
        au_mute = 0;
    } else if (slot != ms_active || !nam_ok) {
        if (!ms_xip_ok) {
            bk_save_reboot();
            return;
        }
        bk_gains();
        ui_message(T(S_LOADING), C_HOT);
        if (ms_switch(slot))
            ui_message(T(S_LOAD_ERR), C_ERR);
        else
            ui_msg = 0;
    } else
        bk_gains();
    ui_draw(0);
}

/* ---- the bank menu (PARA): rows, a value being edited */
enum { MI_BANK, MI_CAPA, MI_CAPB, MI_IN, MI_OUT, MI_LANG, MI_SAVE, MI_BACK, MI_N };
static uint8_t mn_pos, mn_edit;
static int32_t mn_val;

static void mn_value(uint32_t i, int32_t v, char *d)
{
    switch (i) {
    case MI_BANK:
        put_s(put_s(d, "1 · "), BANK_NAME[v]);
        d[0] = (char)('1' + v);
        break;
    case MI_CAPA:
    case MI_CAPB:
        put_s(d, v < 0 ? T(S_EMPTY) : slot_name(ms_list[v]));
        break;
    case MI_LANG:
        put_s(d, T(S_LANG));
        break;
    case MI_IN:
    case MI_OUT:
        put_db(d, v);
        break;
    default:
        d[0] = 0;
    }
}

static int32_t mn_current(uint32_t i)             /* the row's value now (captures: the list index, -1 empty) */
{
    const struct bk_bank *b = &bk.b[bk.bank];
    uint32_t k;
    switch (i) {
    case MI_BANK: return bk.bank;
    case MI_CAPA:
    case MI_CAPB:
        for (k = 0; k < ms_n; k++)
            if (ms_list[k] == b->cap[i - MI_CAPA])
                return (int32_t)k;
        return -1;
    case MI_IN: return b->in_hd;
    case MI_OUT: return b->out_hd;
    default: return 0;
    }
}

static void mn_row(uint32_t i)
{
    static const uint8_t LABEL[MI_N] = {S_M_BANK, S_M_CAPA, S_M_CAPB, S_M_IN, S_M_OUT, S_M_LANG, S_M_SAVE, S_M_BACK};
    static char v[32];
    uint32_t y = 36u + i * 24u, sel = i == mn_pos;
    uint16_t bg = sel ? C_ROW : C_BG;
    lcd_fill(6, y - 2u, 228, 23, bg);
    aa_text(12, y, 96, T(LABEL[i]), &FS, sel ? C_TITLE : C_SUB, bg, 0);
    mn_value(i, sel && mn_edit ? mn_val : mn_current(i), v);
    aa_text(110, y, 118, v, &FS, sel && mn_edit ? C_HOT : C_TITLE, bg, 2);
}

static void mn_open(void)
{
    uint32_t i;
    ui_screen = UI_MENU;
    mn_pos = 0;
    mn_edit = 0;
    lcd_fill(0, 0, LCD_W, LCD_H, C_BG);
    aa_text(12, 6, 216, T(S_MENU), &FS, C_SUB, C_BG, 0);
    lcd_fill(12, 30, 216, 1, C_LINE);
    for (i = 0; i < MI_N; i++)
        mn_row(i);
}

static void mn_close(void)
{
    ui_screen = UI_MAIN;
    ui_draw(1);
    ui_dirty = 1;
}

static void mn_turn(int32_t d)
{
    uint32_t old = mn_pos;
    if (!mn_edit) {
        mn_pos = (uint8_t)((mn_pos + MI_N + (uint32_t)(d > 0 ? 1 : MI_N - 1)) % MI_N);
        mn_row(old);
        mn_row(mn_pos);
        return;
    }
    switch (mn_pos) {
    case MI_BANK: mn_val = (mn_val + (int32_t)BK_N + d) % (int32_t)BK_N; break;
    case MI_CAPA:
    case MI_CAPB: mn_val = (mn_val + 1 + (int32_t)ms_n + 1 + d) % ((int32_t)ms_n + 1) - 1; break;
    case MI_IN:                                   /* the levels change live */
        mn_val = mn_val + d < -24 ? -24 : mn_val + d > 24 ? 24 : mn_val + d;
        bk.b[bk.bank].in_hd = (int8_t)mn_val;
        bk_gains();
        break;
    case MI_OUT:
        mn_val = mn_val + d < -48 ? -48 : mn_val + d > 24 ? 24 : mn_val + d;
        bk.b[bk.bank].out_hd = (int8_t)mn_val;
        bk_gains();
        break;
    }
    mn_row(mn_pos);
}

static void mn_click(void)
{
    uint32_t i;
    if (mn_pos == MI_SAVE) {
        bk_save_reboot();
        return;
    }
    if (mn_pos == MI_BACK) {
        mn_close();
        return;
    }
    if (mn_pos == MI_LANG) {                      /* a click switches it (saved with Save) */
        ui_lang ^= 1u;
        bk.lang = ui_lang;
        i = mn_pos;
        mn_open();
        mn_pos = (uint8_t)i;
        for (i = 0; i < MI_N; i++)
            mn_row(i);
        return;
    }
    if (!mn_edit) {
        mn_edit = 1;
        mn_val = mn_current(mn_pos);
        mn_row(mn_pos);
        return;
    }
    mn_edit = 0;                                  /* confirmed: the bank / a capture now */
    if (mn_pos == MI_BANK && (uint32_t)mn_val != bk.bank) {
        bk_go((uint32_t)mn_val, 0);
        for (i = 0; i < MI_N; i++)
            mn_row(i);
    } else if (mn_pos == MI_CAPA || mn_pos == MI_CAPB) {
        uint32_t side = mn_pos - MI_CAPA;
        bk.b[bk.bank].cap[side] = mn_val < 0 ? (uint8_t)MS_NONE : ms_list[mn_val];
        if (side == bk.ab)
            bk_go(bk.bank, side);
    }
    mn_row(mn_pos);
}

/* the panel, every 5 ms: debounced buttons (pressed in two scans); ladder = the PC10 reading */
static void ui_buttons(uint32_t din, int32_t ladder)
{
    static uint32_t prev, deb, t_sel, sel_long, t_left, left_done;
    uint32_t now = now_ms(), was = deb, press, rel, i;
    if (ladder >= 0 && ladder < 661)              /* the DLY / REV / CAB ladder (docs/06) -> bits 9..11 */
        din |= 1u << 11;
    else if (ladder >= 661 && ladder < 746)
        din |= 1u << 9;
    else if (ladder >= 746 && ladder < 905)
        din |= 1u << 10;
    deb = (din & prev) | (deb & (din | prev));
    prev = din;
    press = deb & ~was;
    rel = was & ~deb;
    if (press & BTN_INFO) {
        if (ui_screen == UI_MAIN)
            ui_info_open();
        else
            mn_close();
        return;
    }
    /* footswitches and banks work on every screen (the player keeps playing) */
    if (press & BTN_FS_L) {
        t_left = now;
        left_done = 0;
        if (au_umute) {                           /* muted: a press un-mutes */
            au_umute = 0;
            left_done = 1;
            ui_draw(0);
        }
    }
    if ((deb & BTN_FS_L) && !left_done && now - t_left >= UI_MUTE_MS) {
        left_done = 1;                            /* held: mute */
        au_umute = 1;
        ui_draw(0);
    }
    if ((rel & BTN_FS_L) && !left_done) {         /* a tap: the effect on / off */
        ui_bypass ^= 1u;
        nam_on = nam_ok && c1_ok && !ui_bypass;
        ui_draw(0);
    }
    if (press & BTN_FS_R) {
        bk_go(bk.bank, bk.ab ^ 1u);
        if (ui_screen == UI_MENU)
            mn_open();
    }
    for (i = 0; i < BK_N; i++)
        if ((press & BTN_BANK[i]) && i != bk.bank) {
            bk_go(i, 0);
            if (ui_screen == UI_MENU)
                mn_open();
            break;
        }
    if (press & BTN_SEL) {
        t_sel = now;
        sel_long = 0;
    }
    if ((deb & BTN_SEL) && !sel_long && now - t_sel >= UI_HOLD_MS && ui_screen == UI_MAIN) {
        sel_long = 1;                             /* held on the player: save */
        bk_save_reboot();
    }
    if ((rel & BTN_SEL) && !sel_long) {           /* released: a click */
        if (ui_screen == UI_MAIN)
            mn_open();
        else if (ui_screen == UI_MENU)
            mn_click();
    }
    if (ui_msg && now - ui_msg_ms >= UI_MSG_MS) {
        ui_msg = 0;
        ui_draw(0);
    }
    if (ui_screen == UI_MAIN && bat_shown != (uint8_t)(bat_pct | bat_5v << 7))
        ui_battery();
}

/* PARA's quadrature, every loop: 4 transitions per detent */
static void ui_encoder(uint32_t din)
{
    static const int8_t T[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
    static uint32_t st;
    static int32_t acc;
    uint32_t s = (din >> ENC_A & 1u) << 1 | (din >> ENC_B & 1u);
    acc += T[st << 2 | s];
    st = s;
    if (acc >= 4 || acc <= -4) {
        int32_t d = acc > 0 ? 1 : -1;
        acc = 0;
        if (ui_screen == UI_MENU)
            mn_turn(d);
    }
}

/* v0.17.1 (JBD): the chip's matrix x vector accelerator at 0x01EE0400 (docs/09-onyx-analysis.md). The stock V20 drives it with
 * the same protocol (app_v20 0x2045ED2..0x2045F06): a 24-byte command block {config, src_a, src_b, dst, ctrl, stride}, then
 * [+8] = 0, [+4] = &block, [+0] = 0x149 (start); done when [+0] bit 7 is set, acknowledged by setting bit 6. Only run on request
 * (USB), never at boot; every wait is bounded, so a unit that never answers reports a timeout instead of hanging.
 *   ctrl = shift << 24 | rows << 12 | cols
 *   mode C ("conv", stride & 0xFFFF = row step in bytes, stride bits 23:20 = output step in bytes): src_a = matrix, src_b = vector,
 *          dst[r * ostep] = sat((sum_j A[r * step + j] * B[j] + round) >> shift)        (what an A2-Lite layer needs, Onyx)
 *   mode G (the stock's own job: config 0xAA1, ctrl 0x15009011, stride 0x411000): src_a = vector, src_b = matrix 9 x 17 */
#define ACC_REG ((volatile uint32_t *)0x01EE0400u)
#define ACC_CFG_MATVEC 0x0AA1u
#define ACC_TIMEOUT_TICKS (24u * 2000u)           /* 2 ms (TIMER4 24 MHz): a 64 x 20 job takes far less */
/* the buffers live in the high RAM (C1RAM, past core 1's data): the 72 KB data region is full */
#define ACC_SEC __attribute__((section(".c1_bss.acc"), aligned(8)))
static volatile uint32_t acc_cmd[6] ACC_SEC;
static int32_t acc_a[64 * 48] ACC_SEC;            /* matrix (up to 64 rows x 47 columns) */
static int32_t acc_b[48] ACC_SEC;                 /* vector */
static int32_t acc_d[64 * 3 + 8] ACC_SEC;         /* results (up to 64 records of 12 B) + a guard */

/* one job; returns its time in TIMER4 ticks, or 0xFFFFFFFF on a timeout (the unit is then stopped and acknowledged) */
static uint32_t acc_job(const void *a, const void *b, void *d, uint32_t ctrl, uint32_t stride)
{
    uint32_t t0;
    acc_cmd[0] = ACC_CFG_MATVEC; acc_cmd[1] = (uint32_t)a; acc_cmd[2] = (uint32_t)b; acc_cmd[3] = (uint32_t)d;
    acc_cmd[4] = ctrl; acc_cmd[5] = stride;
    __asm__ volatile("csync" ::: "memory");
    fm1_irq_off();                                /* interrupts off for the job (at most 2 ms): the time is the unit's alone */
    ACC_REG[2] = 0u;
    ACC_REG[1] = (uint32_t)acc_cmd;
    t0 = fm1_ticks();
    ACC_REG[0] = 0x149u;
    while (!(ACC_REG[0] & 0x80u))
        if (fm1_ticks() - t0 > ACC_TIMEOUT_TICKS) {
            ACC_REG[0] |= 0x40u;
            ACC_REG[0] = 0u;
            fm1_irq_on();
            return 0xFFFFFFFFu;
        }
    t0 = fm1_ticks() - t0;
    ACC_REG[0] |= 0x40u;
    fm1_irq_on();
    __asm__ volatile("csync" ::: "memory");
    return t0;
}

static int32_t acc_ref(int64_t acc, uint32_t shift)              /* the rounding and saturation the unit applies (Onyx, verified there) */
{
    acc = (acc + (shift ? (int64_t)1 << (shift - 1u) : 0)) >> shift;
    if (acc > 2147483647LL) acc = 2147483647LL;
    if (acc < -2147483647LL - 1) acc = -2147483647LL - 1;
    return (int32_t)acc;
}

static uint32_t acc_seed = 0x1234567u;
static int32_t acc_rnd(int32_t lim)                               /* signed, |v| < lim */
{
    acc_seed = acc_seed * 1664525u + 1013904223u;
    return (int32_t)((acc_seed >> 1) % (uint32_t)(2 * lim - 1)) - (lim - 1);
}

/* test t: 0 = 4 x 3 (Onyx's self-test), 1 = 64 x 20 with 12-byte output records (an A2-Lite k = 6 layer), 2 = 64 x 47 (k = 15),
 * 3 = the stock's 9 x 17 job (mode G). Result r[8]: 0 status (0 ok, 1 mismatch, 2 timeout, 3 guard overwritten), 1 rows, 2 cols,
 * 3 ticks, 4 mismatches, 5 first bad index, 6 its value, 7 the expected value. */
static void acc_test(uint32_t t, uint32_t *r)
{
    uint32_t rows, cols, shift = 24u, ostep = 1u, i, j, bad = 0, first = 0xFFFFu, ctrl, stride;
    int32_t got = 0, want = 0;
    const int g = t == 3u;
    if (t == 0u) { rows = 4u; cols = 3u; }
    else if (t == 1u) { rows = 64u; cols = 20u; ostep = 3u; }
    else if (t == 2u) { rows = 64u; cols = 47u; ostep = 3u; }
    else { rows = 9u; cols = 17u; shift = 21u; }
    for (i = 0; i < rows * cols; i++)
        acc_a[i] = acc_rnd(1 << 22);                              /* about +-0.25 in Q24: sums of up to 47 terms stay in range */
    for (j = 0; j < cols; j++)
        acc_b[j] = acc_rnd(1 << 24);
    for (i = 0; i < sizeof acc_d / 4u; i++)
        acc_d[i] = (int32_t)0x7FEEDBACu;
    if (g) { ctrl = 0x15009011u; stride = 0x411000u; }
    else { ctrl = shift << 24 | rows << 12 | cols; stride = (ostep * 4u) << 20 | cols * 4u; }
    r[3] = g ? acc_job(acc_b, acc_a, acc_d, ctrl, stride) : acc_job(acc_a, acc_b, acc_d, ctrl, stride);
    r[1] = rows; r[2] = cols;
    if (r[3] == 0xFFFFFFFFu) {
        r[0] = 2u; r[4] = r[5] = r[6] = r[7] = 0u;
        return;
    }
    for (i = 0; i < rows; i++) {
        int64_t s = 0;
        for (j = 0; j < cols; j++)
            s += (int64_t)acc_a[i * cols + j] * acc_b[j];
        const int32_t e = acc_ref(s, shift), v = acc_d[i * ostep];
        if (v != e) {
            if (!bad) { first = i; got = v; want = e; }
            bad++;
        }
    }
    r[0] = bad ? 1u : acc_d[rows * ostep] != (int32_t)0x7FEEDBACu ? 3u : 0u;   /* 3: right results, but it wrote past its rows */
    r[4] = bad; r[5] = first; r[6] = (uint32_t)got; r[7] = (uint32_t)want;
}

/* v0.18.0-dev (JBO): Onyx's A2-Lite block engine on the accelerator, as a test (fw/src/jb_onyx.c, tools/jb_onyx.py) */
extern void onyx_prepare(void *mem, uint32_t size, const float *w, uint32_t n, uint32_t *r);
extern void onyx_run(uint32_t blocks, uint32_t *r, float *last, void (*between)(void));
extern void onyx_options(uint32_t asm_on, uint32_t irq_off);
extern void onyx_state(uint32_t *c);
extern void onyx_c1_start(void), onyx_c1_next_input(void), onyx_c1_block(uint32_t);
extern float *onyx_c1_out(void);
extern int onyx_ready_c1(void);
static uint8_t onyx_ready;

static void debug_service(void)                   /* our SysEx; anything else stays for ota_service */
{
    const uint8_t *p;
    uint32_t n;
    if (!ota_frame_get(&p, &n))
        return;
    if (ms_service(p, n))                         /* v0.14: the capture store */
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
    } else if (n == 8u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'C') {
        /* remote control: F0 7D 'J' 'B' 'C' fmt tone gain_hi gain_lo F7 (gain 14 bits, Q14; 0x3FFF = MASTER) */
        uint32_t gq = (uint32_t)p[6] << 7 | p[7];
        ota_frame_done();
        au_fmt = (uint8_t)(p[4] % N_FMT);
        au_tone = (uint8_t)(p[5] & 1u);
        au_gain_force = gq == 0x3FFFu ? -1 : (int32_t)(gq << 2);
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == '1') {
        /* core 1 test: start, an integer job, a float job (each with Felucca's 4 ms timeout) */
        static uint8_t msg[64];
        uint32_t st[8], k, sv = au_on;
        ota_frame_done();
        au_on = 0;
        st[0] = (uint32_t)fm1_cpu1_start();      /* 0 = running */
        st[1] = fm1_c1_trace;
        c1_test_out = 0;
        st[2] = fm1_cpu1_run(c1_int_job, 7u) ? (uint32_t)fm1_cpu1_wait() : 0xEEu;
        st[3] = c1_test_out;                      /* 22 expected */
        c1_test_out = 0;
        st[4] = fm1_c1_on ? (fm1_cpu1_run(c1_float_job, 7u) ? (uint32_t)fm1_cpu1_wait() : 0xEEu) : 0xDDu;
        st[5] = c1_test_out;                      /* 4.75f = 0x40980000 expected */
        st[6] = fm1_c1_gave_up | fm1_c1_mb.bad << 8;
        st[7] = *(volatile uint32_t *)0x1C07F20u; /* core 0's icfg as the SPL handed it over (crt0) */
        fm1_cpu1_hold();
        au_on = (uint8_t)sv;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = '1';
        k = 5u + ota_pack7((const uint8_t *)st, 32, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 14u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'M') {
        /* RAM peek: F0 7D 'J' 'B' 'M' addr (5 x 7 bits, LSB first) len (5 x 7 bits) F7 -> 64-byte chunks;
         * only RAM 0x01C00000..0x01C80000 */
        static uint8_t msg[96];
        uint32_t a = 0, len = 0, i, k;
        for (i = 0; i < 5u; i++) {
            a |= (uint32_t)p[4 + i] << (7u * i);
            len |= (uint32_t)p[9 + i] << (7u * i);
        }
        ota_frame_done();
        if (!((a >= 0x01C00000u && a + len <= 0x01C80000u) || (a >= 0x10000u && a + len <= 0x10100u)) || len > 4096u)
            len = 0;   /* RAM, or the clock / reset registers (v0.13.9) */
        for (i = 0; i < len; i += 48u) {
            uint32_t nb = len - i > 48u ? 48u : len - i;
            msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'M';
            msg[5] = (uint8_t)((i / 48u) & 0x7Fu);
            k = 6u + ota_pack7((const uint8_t *)(uintptr_t)(a + i), nb, msg + 6);
            msg[k++] = 0xF7;
            ota_wire_send(msg, k);
        }
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'M'; msg[5] = 0x7F; msg[6] = 0xF7;
        ota_wire_send(msg, 7);
    } else if ((n == 5u || n == 6u) && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == '6') {
        const uint32_t mr_secs = n == 6u && p[5] ? p[5] : 2u;
        /* JB6 r: misread census, region r: 0 = core 1's own stack, 1 = RAM (.bss), 2 = free high RAM 0x01C60000,
         * 3 = free RAMTEXT 0x01C02000. Core 0 runs a2l_front + poll meanwhile. Reply: 6 words */
        static uint8_t msg[48];
        static a2l_t din[AU_HALF / 2u], dlin[AU_HALF / 2u * A2L_C], dhs[AU_HALF / 2u * A2L_C];
        volatile uint32_t *b;
        uint32_t i, k, t0, fronts = 0;
        ota_frame_done();
        b = p[4] == 0 ? (volatile uint32_t *)mr_low_buf : p[4] == 1 ? mr_test_buf
          : p[4] == 2 ? (volatile uint32_t *)0x01C60000u : (volatile uint32_t *)0x01C02000u;
        for (i = 0; i < MR_WORDS; i++)
            b[i] = MR_PAT((uint32_t)(uintptr_t)(b + i));
        mr_base = b;
        for (i = 0; i < 6u; i++)
            mr_res[i] = 0;
        FM1_C1_SYNC();
        fm1_p33_write(0x09, (uint8_t)((fm1_p33_read(0x09) & 0xF0u) | nam_vlevel));
        fm1_delay_ms(2);
        if (!c1_ok)
            c1_ok = fm1_cpu1_start() == 0;
        if (c1_ok && fm1_cpu1_run(mr_job, 24000000u * mr_secs)) {
            t0 = fm1_ticks();
            while (fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - t0 < (mr_secs + 5u) * 1000000u * FM1_TICKS_PER_US) {
                void (*volatile front)(struct a2l *, const a2l_t *, a2l_t *, a2l_t *, uint32_t) = a2l_front;
                if (nam_ok)
                    front(&nam, din, dlin, dhs, AU_HALF / 2u);
                fronts++;
                poll();
                FM1_C1_SYNC();
            }
            mr_res[5] = fm1_c1_mb.done == fm1_c1_mb.job ? fronts : 0xEEEEEEEEu;
        } else
            mr_res[5] = 0xDDDDDDDDu;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = '6';
        k = 5u + ota_pack7((const uint8_t *)mr_res, 24, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == '7') {
        /* JB7 m: core 1 runs fp_job for 2 s; core 0 meanwhile: m = 0 integer work + poll, 1 = the same float
         * chain (checked), 2 = the NAM front. Reply: core 1 runs, bad, first, ref, ichk bad; core 0 runs, bad */
        static uint8_t msg[64];
        uint32_t r[8], i, k, t0, ref0, rk0, kk, v, runs0 = 0, bad0 = 0;
        static a2l_t din[AU_HALF / 2u], dlin[AU_HALF / 2u * A2L_C], dhs[AU_HALF / 2u * A2L_C];
        ota_frame_done();
        ref0 = fp_chain(0.999f, 0.001f, 500u, &rk0);
        fp_go = fp_ready = 0;
        for (i = 0; i < 5u; i++)
            fp_res1[i] = 0;
        FM1_C1_SYNC();
        fm1_p33_write(0x09, (uint8_t)((fm1_p33_read(0x09) & 0xF0u) | nam_vlevel));
        fm1_delay_ms(2);
        if (!c1_ok)
            c1_ok = fm1_cpu1_start() == 0;
        if (c1_ok && fm1_cpu1_run(fp_job, 24000000u * 2u)) {
            t0 = fm1_ticks();
            while (!fp_ready && fm1_ticks() - t0 < 100000u * FM1_TICKS_PER_US)
                FM1_C1_SYNC();
            fp_go = 1;
            FM1_C1_SYNC();
            t0 = fm1_ticks();
            while (fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - t0 < 3000000u * FM1_TICKS_PER_US) {
                if (p[4] == 1u) {
                    for (i = 0; i < 8u; i++) {
                        v = fp_chain(0.999f, 0.001f, 500u, &kk);
                        bad0 += v != ref0 || kk != rk0;
                        runs0++;
                    }
                } else if (p[4] == 2u) {
                    void (*volatile front)(struct a2l *, const a2l_t *, a2l_t *, a2l_t *, uint32_t) = a2l_front;
                    if (nam_ok)
                        front(&nam, din, dlin, dhs, AU_HALF / 2u);
                    runs0++;
                } else {
                    for (i = 0, kk = 1; i < 20000u; i++)
                        kk = kk * 0x01000193u + i;
                    bad0 += kk == 0;
                    runs0++;
                }
                poll();
                FM1_C1_SYNC();
            }
        }
        r[0] = fp_res1[0]; r[1] = fp_res1[1]; r[2] = fp_res1[2]; r[3] = fp_res1[3]; r[4] = fp_res1[4];
        r[5] = runs0; r[6] = bad0; r[7] = fm1_c1_mb.done == fm1_c1_mb.job;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = '7';
        k = 5u + ota_pack7((const uint8_t *)r, 32, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == '8') {
        /* JB8 s: ir_job on core 1 for s seconds, the NAM front + poll on core 0; core 1 under its guards */
        static uint8_t msg[48];
        static a2l_t din[AU_HALF / 2u], dlin[AU_HALF / 2u * A2L_C], dhs[AU_HALF / 2u * A2L_C];
        uint32_t r[6], k, t0, fronts = 0, secs = p[4] ? p[4] : 1u;
        ota_frame_done();
        ir_res[0] = ir_res[1] = ir_res[2] = 0;
        ir_res[3] = int_rec(10u, 0x1234u);
        FM1_C1_SYNC();
        fm1_p33_write(0x09, (uint8_t)((fm1_p33_read(0x09) & 0xF0u) | nam_vlevel));
        fm1_delay_ms(2);
        c1_guard_setup();
        if (!c1_ok)
            c1_ok = fm1_cpu1_start() == 0;
        if (c1_ok && fm1_cpu1_run(ir_job, 24000000u * secs)) {
            t0 = fm1_ticks();
            while (fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - t0 < (secs + 1u) * 1000000u * FM1_TICKS_PER_US
                   && fm1_c1_crash.magic != FM1_CRASH_MAGIC) {
                void (*volatile front)(struct a2l *, const a2l_t *, a2l_t *, a2l_t *, uint32_t) = a2l_front;
                if (nam_ok)
                    front(&nam, din, dlin, dhs, AU_HALF / 2u);
                fronts++;
                poll();
                FM1_C1_SYNC();
            }
        }
        r[0] = ir_res[0]; r[1] = ir_res[1]; r[2] = ir_res[2]; r[3] = ir_res[3]; r[4] = fronts;
        r[5] = (fm1_c1_mb.done == fm1_c1_mb.job) | (fm1_c1_crash.magic == FM1_CRASH_MAGIC) << 1;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = '8';
        k = 5u + ota_pack7((const uint8_t *)r, 24, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 6u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == '9') {
        /* JB9 m s: core 1 loops a2lq_back for s seconds; core 0 meanwhile: m = 0 the float front, 1 integer work,
         * 2 only poll(). The profiler runs (a lock shows in the next boot's log). Reply: core 1 runs, core 0 runs */
        static uint8_t msg[32];
        static a2l_t din[AU_HALF / 2u], dlin[AU_HALF / 2u * A2L_C], dhs[AU_HALF / 2u * A2L_C];
        uint32_t r[3], k, t0, i, kk, runs0 = 0, secs = p[5] ? p[5] : 1u;
        ota_frame_done();
        qb_runs = 0;
        nam_on = 0;
        fm1_p33_write(0x09, (uint8_t)((fm1_p33_read(0x09) & 0xF0u) | nam_vlevel));
        fm1_delay_ms(2);
        c1_guard_setup();
        if (!c1_ok)
            c1_ok = fm1_cpu1_start() == 0;
        prof_start();
        if (c1_ok && fm1_cpu1_run(qback_loop_job, 24000000u * secs)) {
            t0 = fm1_ticks();
            while (fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - t0 < (secs + 5u) * 1000000u * FM1_TICKS_PER_US) {
                if (p[4] == 0u) {
                    void (*volatile front)(struct a2l *, const a2l_t *, a2l_t *, a2l_t *, uint32_t) = a2l_front;
                    if (nam_ok)
                        front(&nam, din, dlin, dhs, AU_HALF / 2u);
                } else if (p[4] == 1u) {
                    for (i = 0, kk = 1; i < 20000u; i++)
                        kk = kk * 0x01000193u + i;
                    runs0 += kk == 0;
                }
                runs0++;
                poll();
                FM1_C1_SYNC();
            }
        }
        r[0] = qb_runs; r[1] = runs0; r[2] = fm1_c1_mb.done == fm1_c1_mb.job;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = '9';
        k = 5u + ota_pack7((const uint8_t *)r, 12, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 6u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'X') {
        /* JBX m s: ar_job(m) on core 1 for s seconds while core 0 runs the float front; the profiler runs */
        static uint8_t msg[40];
        static a2l_t din[AU_HALF / 2u], dlin[AU_HALF / 2u * A2L_C], dhs[AU_HALF / 2u * A2L_C];
        uint32_t r[6], k, t0, i, runs0 = 0, secs = p[5] ? p[5] : 1u;
        ota_frame_done();
        for (i = 0; i < 256u; i++)
            ar_tab[i] = (int32_t)(i * 0x9E3779B1u) >> 3;
        for (i = 0; i < 4u; i++)
            ar_res[i] = 0;
        nam_on = 0;
        FM1_C1_SYNC();
        fm1_p33_write(0x09, (uint8_t)((fm1_p33_read(0x09) & 0xF0u) | nam_vlevel));
        fm1_delay_ms(2);
        c1_guard_setup();
        if (!c1_ok)
            c1_ok = fm1_cpu1_start() == 0;
        prof_start();
        if (c1_ok && fm1_cpu1_run(ar_job, (uint32_t)p[4] << 28 | (24000000u * secs) >> 4)) {
            t0 = fm1_ticks();
            while (fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - t0 < (secs + 5u) * 1000000u * FM1_TICKS_PER_US) {
                void (*volatile front)(struct a2l *, const a2l_t *, a2l_t *, a2l_t *, uint32_t) = a2l_front;
                if (nam_ok)
                    front(&nam, din, dlin, dhs, AU_HALF / 2u);
                runs0++;
                poll();
                FM1_C1_SYNC();
            }
        }
        r[0] = ar_res[0]; r[1] = ar_res[1]; r[2] = ar_res[2]; r[3] = ar_res[3]; r[4] = runs0;
        r[5] = fm1_c1_mb.done == fm1_c1_mb.job;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'X';
        k = 5u + ota_pack7((const uint8_t *)r, 24, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if ((n == 15u || n == 16u) && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'Y') {
        /* JBY X Y s [mode] (X, Y: 5 x 7 bits each): RAM bank test. Core 1 reads a 4 KB pattern at X (mr_job), core 0
         * reads one at Y back to back (+ poll), both checked. v0.13.12 mode bits (v0.13.11 reset the chip ~12 ms in):
         * 1 = no profiler, 2 = core 0 only polls, 4 = core 1 not started (core 0 alone for s seconds);
         * v0.13.13: 8 = core 1 writes and reads back (mw_job), 16 = core 0 runs the NAM front (float) instead of reading.
         * Reply: core 1 rounds (x 1024 words), bad, core 0 rounds, bad, done */
        static uint8_t msg[72];
        static a2l_t din[AU_HALF / 2u], dlin[AU_HALF / 2u * A2L_C], dhs[AU_HALF / 2u * A2L_C];
        volatile uint32_t *x, *y;
        uint32_t r[11], i, k, t0, xa = 0, ya = 0, secs = p[14] ? p[14] : 1u, bad0 = 0, rounds0 = 0;
        uint32_t ba0 = 0, bv0 = 0, be0 = 0;
        const uint32_t mode = n == 16u ? p[15] : 0u;
        int run1;
        for (i = 0; i < 5u; i++) {
            xa |= (uint32_t)p[4 + i] << (7u * i);
            ya |= (uint32_t)p[9 + i] << (7u * i);
        }
        ota_frame_done();
        x = (volatile uint32_t *)(uintptr_t)xa;
        y = (volatile uint32_t *)(uintptr_t)ya;
        if (xa >= 0x01C01000u && ya >= 0x01C01000u && xa + 4096u <= 0x01C74000u && ya + 4096u <= 0x01C74000u) {
            for (i = 0; i < MR_WORDS; i++) {
                x[i] = MR_PAT((uint32_t)(uintptr_t)(x + i));
                y[i] = MR_PAT((uint32_t)(uintptr_t)(y + i));
            }
            mr_base = x;
            for (i = 0; i < 6u; i++)
                mr_res[i] = 0;
            FM1_C1_SYNC();
            fm1_p33_write(0x09, (uint8_t)((fm1_p33_read(0x09) & 0xF0u) | nam_vlevel));
            fm1_delay_ms(2);
            if (!(mode & 4u) && !c1_ok)
                c1_ok = fm1_cpu1_start() == 0;
            if (!(mode & 1u))
                prof_start();
            run1 = !(mode & 4u) && c1_ok && fm1_cpu1_run(mode & 8u ? mw_job : mr_job, 24000000u * secs);
            if (run1 || (mode & 4u)) {
                t0 = fm1_ticks();
                while (run1 ? fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - t0 < (secs + 5u) * 1000000u * FM1_TICKS_PER_US
                            : fm1_ticks() - t0 < secs * 1000000u * FM1_TICKS_PER_US) {
                    if (mode & 16u) {
                        void (*volatile front)(struct a2l *, const a2l_t *, a2l_t *, a2l_t *, uint32_t) = a2l_front;
                        if (nam_ok)
                            front(&nam, din, dlin, dhs, AU_HALF / 2u);
                    } else if (!(mode & 2u))
                        for (i = 0; i < MR_WORDS; i++) {
                            uint32_t v = y[i], e = MR_PAT((uint32_t)(uintptr_t)(y + i));
                            if (v != e) {
                                if (!bad0) {
                                    ba0 = (uint32_t)(uintptr_t)(y + i); bv0 = v; be0 = e;
                                }
                                bad0++;
                            }
                        }
                    rounds0++;
                    if (!(rounds0 & 7u) || (mode & 18u))
                        poll();
                    FM1_C1_SYNC();
                }
            }
        }
        r[0] = mr_res[0]; r[1] = mr_res[1]; r[2] = rounds0; r[3] = bad0; r[4] = fm1_c1_mb.done == fm1_c1_mb.job;
        r[5] = mr_res[2]; r[6] = mr_res[3]; r[7] = mr_res[4];     /* core 1's first bad word: address, value, expected */
        r[8] = ba0; r[9] = bv0; r[10] = be0;                      /* core 0's */
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'Y';
        k = 5u + ota_pack7((const uint8_t *)r, 44, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'Q') {
        /* core 1's registers (q32DSP(1), 0x1EEF200: r0..r15, RETI..PCRS), its EMU / ETM, the debug unit, the
         * windows and core 1's own fault record: 48-byte chunks, index byte, 0x7F ends */
        static uint32_t w[160];
        static uint8_t msg[96];
        uint32_t i, k, nw = 0, b;
        ota_frame_done();
        for (i = 0; i < 32u; i++)
            w[nw++] = *(volatile uint32_t *)(0x1EEF200u + 4u * i);
        for (i = 0; i < 4u; i++)
            w[nw++] = *(volatile uint32_t *)(0x1EEF2D0u + 4u * i);
        for (i = 0; i < 5u; i++)
            w[nw++] = *(volatile uint32_t *)(0x1EEF3C0u + 4u * i);
        w[nw++] = CX2(0x244u); w[nw++] = CX2(0x340u); w[nw++] = CX2(0x344u); w[nw++] = CX2(0x348u);
        w[nw++] = CX2(0x34Cu); w[nw++] = FM1_WRERR(); w[nw++] = fm1_c1_trace;
        w[nw++] = fm1_c1_mb.job; w[nw++] = fm1_c1_mb.done; w[nw++] = fm1_c1_mb.bad; w[nw++] = c1_nwin;
        for (i = 0; i < 8u; i++)
            w[nw++] = c1_win[i][0];
        for (i = 0; i < 8u; i++)
            w[nw++] = c1_win[i][1];
        for (i = 0; i < sizeof fm1_c1_crash / 4u; i++)
            w[nw++] = ((const uint32_t *)&fm1_c1_crash)[i];
        for (i = 0; i < 48u; i++)
            w[nw++] = fm1_c1_regs[i];
        for (b = 0; b < nw * 4u; b += 48u) {
            uint32_t nb = nw * 4u - b > 48u ? 48u : nw * 4u - b;
            msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'Q'; msg[5] = (uint8_t)(b / 48u);
            k = 6u + ota_pack7((const uint8_t *)w + b, nb, msg + 6);
            msg[k++] = 0xF7;
            ota_wire_send(msg, k);
        }
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'Q'; msg[5] = 0x7F; msg[6] = 0xF7;
        ota_wire_send(msg, 7);
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'F') {
        static uint8_t msg[96];
        uint32_t k;
        ota_frame_done();
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'F';
        k = 5u + ota_pack7((const uint8_t *)&fm1_crash, sizeof fm1_crash, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'N') {
        /* NAM on two cores now: core supply 1.32 V, core 1 started (once), pipeline on */
        static uint8_t msg[16];
        uint32_t st[2], k;
        ota_frame_done();
        nam_start();
        nam_dbg_marks = 1;
        st[0] = c1_ok; st[1] = nam_on;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'N';
        k = 5u + ota_pack7((const uint8_t *)st, 8, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if ((n == 5u || n == 6u) && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'V') {
        /* P33 analog registers 0x00..0x1F; p[4] 1..15 = set SYSVDD_VOL_SEL (P3_ANA_CON9 bits 3..0) to that
         * level first (0.93 V + 0.03 V x level), 0x7F = read only. v0.13.15: p[5] 0..7 = VDC14_VOL_SEL (P3_ANA_CON6
         * bits 2..0, 1.25 V + 0.05 V x level; the SYSVDD LDO's input), set before SYSVDD */
        static uint8_t msg[64];
        uint8_t r[32];
        uint32_t i, k;
        ota_frame_done();
        if (n == 6u && p[5] <= 7u) {
            fm1_p33_write(0x06, (uint8_t)((fm1_p33_read(0x06) & 0xF8u) | p[5]));
            fm1_delay_ms(2);
        }
        if (p[4] <= 15u) {
            fm1_p33_write(0x09, (uint8_t)((fm1_p33_read(0x09) & 0xF0u) | p[4]));
            if (p[4] >= 11u)
                nam_vlevel = p[4];
        }
        for (i = 0; i < 32u; i++)
            r[i] = fm1_p33_read(i);
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'V';
        k = 5u + ota_pack7(r, 32, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == '5') {
        /* FPU sharing, all code in XIP, register-only loads, mode p[4]: 1 = c1 int + c0 float,
         * 2 = c1 float + c0 int, 3 = c1 float + c0 float; core 1 stays on afterwards unless it failed */
        static uint8_t msg[32];
        uint32_t st[4], k, r, mode = p[4], sv = au_on, fails = 0;
        void (*volatile c0f)(uint32_t) = x_fload, (*volatile c0i)(uint32_t) = x_iload;
        ota_frame_done();
        au_on = 0;
        st[0] = (uint32_t)fm1_cpu1_start();
        for (r = 0; r < 20u && !st[0]; r++) {
            int ran = fm1_cpu1_run(mode == 1u ? x_iload : x_fload, 100000u);
            STAGE(70 + mode);
            if (mode == 2u)
                c0i(100000u);
            else
                c0f(100000u);
            STAGE(69);
            if (ran && fm1_cpu1_wait() != 0) {
                fails++;
                break;
            }
            fm1_wdt_feed();
        }
        st[1] = r; st[2] = fails; st[3] = fm1_c1_gave_up | fm1_c1_mb.bad << 8;
        au_on = (uint8_t)sv;
        STAGE(9);
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = '5';
        k = 5u + ota_pack7((const uint8_t *)st, 16, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == '4') {
        /* where the code runs, mode p[4]: 1 = c1 XIP + c0 RAM, 2 = c1 RAM + c0 XIP, 3 = c1 XIP + c0 XIP;
         * integer loads, 20 rounds; crumb stage 60 + mode while both run */
        static uint8_t msg[32];
        uint32_t st[4], k, r, mode = p[4], sv = au_on, fails = 0;
        void (*volatile c0ram)(uint32_t) = c1_iload, (*volatile c0xip)(uint32_t) = x_iload;
        ota_frame_done();
        au_on = 0;
        st[0] = (uint32_t)fm1_cpu1_start();
        for (r = 0; r < 20u && !st[0]; r++) {
            int ran = fm1_cpu1_run(mode == 2u ? c1_iload : x_iload, 100000u);
            STAGE(60 + mode);
            if (mode == 1u)
                c0ram(100000u);
            else
                c0xip(100000u);
            STAGE(59);
            if (ran && fm1_cpu1_wait() != 0)
                fails++;
            fm1_wdt_feed();
        }
        st[1] = r; st[2] = fails; st[3] = fm1_c1_gave_up | fm1_c1_mb.bad << 8;
        fm1_cpu1_hold();
        au_on = (uint8_t)sv;
        STAGE(9);
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = '4';
        k = 5u + ota_pack7((const uint8_t *)st, 16, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == '3') {
        /* concurrency probe, mode p[4]: 1 = c1 float + c0 float, 2 = c1 float + c0 int, 3 = c1 int + c0 float,
         * 4 = c1 int + c0 int; 20 rounds of ~2 ms each; crumb stage 50 + mode while both run */
        static uint8_t msg[32];
        uint32_t st[4], k, r, mode = p[4], sv = au_on, fails = 0;
        void (*volatile c0f)(uint32_t) = c1_fload, (*volatile c0i)(uint32_t) = c1_iload;
        ota_frame_done();
        au_on = 0;
        st[0] = (uint32_t)fm1_cpu1_start();
        for (r = 0; r < 20u && !st[0]; r++) {
            int ran = fm1_cpu1_run(mode <= 2u ? c1_fload : c1_iload, 100000u);
            STAGE(50 + mode);
            if (mode == 1u || mode == 3u)
                c0f(100000u);
            else
                c0i(100000u);
            STAGE(49);
            if (ran && fm1_cpu1_wait() != 0)
                fails++;
            fm1_wdt_feed();
        }
        st[1] = r; st[2] = fails; st[3] = fm1_c1_gave_up | fm1_c1_mb.bad << 8;
        fm1_cpu1_hold();
        au_on = (uint8_t)sv;
        STAGE(9);
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = '3';
        k = 5u + ota_pack7((const uint8_t *)st, 16, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == '2') {
        /* the two-core pipeline on a synthetic signal, p[4] x 10 blocks, audio paused; the crumb stage
         * (40 + step) shows in the next boot's log if it hangs */
        static uint8_t msg[64];
        uint32_t st[8], k, b, nb = (uint32_t)p[4] * 10u, sv = au_on, t, tmax = 0, wmax = 0, fails = 0;
        void (*volatile front)(struct a2l *, const a2l_t *, a2l_t *, a2l_t *, uint32_t) = a2l_front;
        float acc = 0;
        ota_frame_done();
        au_on = 0;
        STAGE(40);
        st[0] = (uint32_t)fm1_cpu1_start();
        for (b = 0; b < nb && !st[0]; b++) {
            uint32_t slot = b & 1u, prev = slot ^ 1u, f, ran = 0;
            for (f = 0; f < AU_HALF / 2u; f++)
                pipe.in[slot][f] = 0.3f * (float)((int32_t)((b * 128u + f) % 100u) - 50) / 50.0f;
            t = fm1_ticks();
            STAGE(41);
            if (b)
                ran = (uint32_t)fm1_cpu1_run(nam_back_job, prev);
            STAGE(42);
            nam_front_slot(slot);
            STAGE(43);
            if (ran) {
                uint32_t w0 = fm1_ticks();
                if (fm1_cpu1_wait() != 0) {
                    fails++;
                    break;
                }
                if (fm1_ticks() - w0 > wmax)
                    wmax = fm1_ticks() - w0;
                acc += (float)pipe.qout[prev][5];
            }
            STAGE(44);
            t = fm1_ticks() - t;
            if (t > tmax)
                tmax = t;
            fm1_wdt_feed();
        }
        STAGE(45);
        st[1] = b; st[2] = tmax / FM1_TICKS_PER_US; st[3] = wmax / FM1_TICKS_PER_US; st[4] = fails;
        st[5] = fm1_c1_gave_up | fm1_c1_mb.bad << 8;
        { union { float f; uint32_t u; } cv; cv.f = acc; st[6] = cv.u; }
        st[7] = fm1_c1_wait_max / FM1_TICKS_PER_US;
        fm1_cpu1_hold();
        au_on = (uint8_t)sv;
        STAGE(9);
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = '2';
        k = 5u + ota_pack7((const uint8_t *)st, 32, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'P') {
        /* microbenchmarks (nam/bench_dev.c): 100 x (128 frames x 9 MACs) each, TIMER4 ticks */
        extern uint32_t bench_f32(uint32_t, const float *), bench_q31(uint32_t, const int32_t *),
            bench_q31h(uint32_t, const int32_t *), bench_fmac(uint32_t, const float *), fmac_check(void),
            bench_lat(float, float), bench_thr(float, float), bench_muladd(float, float);
        static const float wf[9] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f};
        static const int32_t wq[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        static uint8_t msg[80];
        uint32_t st[9], k, t, sv = au_on;
        ota_frame_done();
        au_on = 0;                                /* (the audio would steal time) */
        t = fm1_ticks(); st[3] = bench_f32(100, wf); st[0] = fm1_ticks() - t;
        t = fm1_ticks(); st[3] += bench_q31(100, wq); st[1] = fm1_ticks() - t;
        t = fm1_ticks(); st[3] += bench_q31h(100, wq); st[2] = fm1_ticks() - t;
        t = fm1_ticks(); st[3] += bench_fmac(100, wf); st[4] = fm1_ticks() - t;
        st[5] = fmac_check();
        t = fm1_ticks(); st[3] += bench_lat(1.0001f, 0.5f); st[6] = fm1_ticks() - t;
        t = fm1_ticks(); st[3] += bench_thr(1.0001f, 0.5f); st[7] = fm1_ticks() - t;
        t = fm1_ticks(); st[3] += bench_muladd(1.0001f, 0.5f); st[8] = fm1_ticks() - t;
        au_on = (uint8_t)sv;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'P';
        k = 5u + ota_pack7((const uint8_t *)st, 36, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'K') {
        static uint8_t msg[96];
        uint32_t st[16], k;
        ota_frame_done();
        st[0] = clk_loop(200000u);
        st[1] = *(volatile uint32_t *)0x16A00u; st[2] = *(volatile uint32_t *)0x16A04u;
        st[3] = *(volatile uint32_t *)0x16A08u; st[4] = *(volatile uint32_t *)0x16A0Cu;
        st[5] = *(volatile uint32_t *)0x10000u; st[6] = *(volatile uint32_t *)0x10004u;
        st[7] = *(volatile uint32_t *)0x10008u; st[8] = *(volatile uint32_t *)0x1000Cu;
        st[9] = *(volatile uint32_t *)0x10010u; st[10] = *(volatile uint32_t *)0x10014u;
        st[11] = *(volatile uint32_t *)0x40404u; st[12] = *(volatile uint32_t *)0x4040Cu;
        st[13] = *(volatile uint32_t *)0x119A0u; st[14] = *(volatile uint32_t *)0x1EEF0F4u;
        st[15] = nam_us;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'K';
        k = 5u + ota_pack7((const uint8_t *)st, 64, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'B') {
        ota_frame_done();                         /* v0.13.15: reboot (a clean state after a test left core 1 stuck) */
        fm1_delay_ms(20);
        fm1_reboot();
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'H') {
        /* JBH d: HSB divider field := d (0..3). Reply: SYS_DIV before, after, clk_loop(200000) ticks */
        static uint8_t msg[24];
        uint32_t st[3], k;
        ota_frame_done();
        st[0] = *(volatile uint32_t *)0x10008u;
        if (p[4] <= 3u) {
            fm1_irq_off();
            FL_FAR(hsb_div_ram)(p[4]);
            fm1_irq_on();
        }
        st[1] = *(volatile uint32_t *)0x10008u;
        st[2] = clk_loop(200000u);
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'H';
        k = 5u + ota_pack7((const uint8_t *)st, 12, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'R') {
        ota_frame_done();
        au_in_max = au_clip_in = au_clip_out = au_us_max = 0;   /* reset the audio statistics */
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'S') {
        static uint8_t msg[64];
        uint32_t st[12], k;
        ota_frame_done();
        st[8] = au_out_late;
        st[9] = gap_max; st[10] = gap_long; st[11] = gap_at;
        gap_max = 0;
        st[0] = au_halves; st[1] = au_resync; st[2] = au_in_max; st[3] = pipe_late;
        st[4] = pipe_late_max; st[5] = au_us_max; st[6] = nam_us; st[7] = nam_us_max;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'S';
        k = 5u + ota_pack7((const uint8_t *)st, 48, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'E') {
        static uint8_t msg[8];
        uint32_t k;
        ota_frame_done();
        quiet = p[4];
        led_on = !(quiet & 1u);
        for (k = 0; k < 8u; k++)
            led_bri[k] = quiet & 4u ? 16u : 0u;
        led_write(quiet & 1u ? 0xFFu : quiet & 4u ? 0x00u : 0xFFu);
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'E'; msg[5] = quiet & 0x7Fu; msg[6] = 0xF7;
        ota_wire_send(msg, 7);
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'W') {
        static uint8_t msg[64];                   /* v0.13.19: p[4] 1 = arm; reply state, count, the buffers, gain */
        uint32_t st[6], k;
        ota_frame_done();
        if (p[4] == 1u) {
            cap_n = 0;
            cap_state = 1;
        }
        st[0] = cap_state; st[1] = cap_n; st[2] = (uint32_t)cap_in; st[3] = (uint32_t)cap_out;
        st[4] = au_gain; st[5] = CAP_BLK;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'W';
        k = 5u + ota_pack7((const uint8_t *)st, 24, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if ((n == 5u || n == 7u) && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'O') {
        /* JBO 0: stop the NAM (for good, until a reboot), load the active capture into Onyx's engine in nam_ring's memory;
         *        reply O 0 pack7(7 words: rc, blk_ok, blk_low, asm ok, model size, accel broken, weights address)
         * JBO 1 b0 b1: run b0 | b1 << 7 blocks of 64 on the test input; reply O 1 pack7(6 words: blocks, min, mean, max ticks,
         *        accel broken, errors), then O 2 i pack7(16 floats) for i = 0..3: the last block's raw output */
        static uint8_t msg[96];
        static float last[64];
        uint32_t r[8], k, i;
        ota_frame_done();
        if (p[4] == 0u) {
            const uint32_t slot = ms_active, w0 = fm1_ticks();
            au_mute = 1;
            delay_poll(12);
            nam_on = 0;
            nam_ok = 0;                           /* nothing turns the old NAM back on: its ring is about to be reused */
            while (pipe_busy && fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - w0 < 5000u * FM1_TICKS_PER_US)
                FM1_C1_SYNC();
            pipe_busy = 0;
            au_mute = 0;
            if (!ms_xip_ok || slot >= MS_SLOTS || !(ms_valid >> slot & 1u)) {
                r[0] = 0xFFFFFFF1u; r[1] = r[2] = r[3] = r[4] = r[5] = r[6] = 0;
            } else {
                onyx_prepare(nam_ring, sizeof nam_ring, (const float *)FL_XIP(MS_BASE + slot * MS_SLOT_LEN + MS_HDR), 1871u, r);
                onyx_ready = r[0] == 0u && r[1];
            }
            msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'O'; msg[5] = 0;
            k = 6u + ota_pack7((const uint8_t *)r, 28, msg + 6);
            msg[k++] = 0xF7;
            ota_wire_send(msg, k);
        } else if (p[4] == 5u && n == 7u) {      /* JBO 5 b0 b1: the same run with every block on CORE 1; reply as JBO 1 */
            const uint32_t blocks = (uint32_t)p[5] | (uint32_t)p[6] << 7;
            uint32_t b, mn = 0xFFFFFFFFu, mx = 0, sum = 0, late = 0;
            for (k = 0; k < 8u; k++) r[k] = 0;
            if (onyx_ready && c1_ok) {
                CX2(0x34Cu) = 0;                  /* core 1's write limits off: it writes the accelerator, the low SRAM, the driver's state */
                onyx_c1_start();
                for (b = 0; b < (blocks > 2000u ? 2000u : blocks); b++) {
                    uint32_t t0, t, w0;
                    onyx_c1_next_input();
                    t0 = fm1_ticks();
                    if (!fm1_cpu1_run(onyx_c1_block, b))
                        break;
                    w0 = t0;
                    while (fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - w0 < 20000u * FM1_TICKS_PER_US)
                        FM1_C1_SYNC();
                    t = fm1_ticks() - t0;
                    if (fm1_c1_mb.done != fm1_c1_mb.job) { late++; break; }
                    if (t < mn) mn = t;
                    if (t > mx) mx = t;
                    sum += t;
                    poll();
                }
                r[0] = b; r[1] = mn; r[2] = b ? sum / b : 0; r[3] = mx; r[5] = late;
                for (i = 0; i < 64u; i++) last[i] = onyx_c1_out()[i];
            }
            msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'O'; msg[5] = 1;
            k = 6u + ota_pack7((const uint8_t *)r, 32, msg + 6);
            msg[k++] = 0xF7;
            ota_wire_send(msg, k);
            for (i = 0; i < 4u; i++) {
                msg[5] = 2; msg[6] = (uint8_t)i;
                k = 7u + ota_pack7((const uint8_t *)(last + 16u * i), 64, msg + 7);
                msg[k++] = 0xF7;
                ota_wire_send(msg, k);
            }
        } else if (p[4] == 4u && onyx_ready) {   /* JBO 4: the engine's state (47 words) as O 4 i pack7(48 B), i = 0..3 */
            static uint32_t c[48];
            onyx_state(c);
            for (i = 0; i < 4u; i++) {
                msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'O'; msg[5] = 4; msg[6] = (uint8_t)i;
                k = 7u + ota_pack7((const uint8_t *)(c + 12u * i), 48, msg + 7);
                msg[k++] = 0xF7;
                ota_wire_send(msg, k);
            }
        } else if (p[4] == 3u && n == 7u) {      /* JBO 3 asm irq_off: diagnostics (reply O 3) */
            onyx_options(p[5], p[6]);
            msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'O'; msg[5] = 3; msg[6] = 0xF7;
            ota_wire_send(msg, 7);
        } else if (p[4] == 1u && n == 7u) {
            const uint32_t blocks = (uint32_t)p[5] | (uint32_t)p[6] << 7;
            if (onyx_ready)
                onyx_run(blocks > 2000u ? 2000u : blocks, r, last, poll);
            else
                r[0] = r[1] = r[2] = r[3] = r[4] = r[5] = r[6] = r[7] = 0;
            msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'O'; msg[5] = 1;
            k = 6u + ota_pack7((const uint8_t *)r, 32, msg + 6);
            msg[k++] = 0xF7;
            ota_wire_send(msg, k);
            for (i = 0; i < 4u; i++) {
                msg[5] = 2; msg[6] = (uint8_t)i;
                k = 7u + ota_pack7((const uint8_t *)(last + 16u * i), 64, msg + 7);
                msg[k++] = 0xF7;
                ota_wire_send(msg, k);
            }
        }
    } else if ((n == 4u || n == 7u) && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'K') {
        static uint8_t msg[16];                   /* v0.17.2: JBK [g0 g1 g2]: the bypass gain (Q16, 21 bits LSB first), reply it */
        uint32_t k, v;
        ota_frame_done();
        if (n == 7u) {
            v = (uint32_t)p[4] | (uint32_t)p[5] << 7 | (uint32_t)p[6] << 14;
            if (v <= 4u * 65536u)
                au_bypass_gain = v;
        }
        v = au_bypass_gain;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'K';
        k = 5u + ota_pack7((const uint8_t *)&v, 4, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 5u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'D') {
        static uint8_t msg[48];                   /* v0.17.1: JBD t -> acc_test(t) result, 8 words (t 0..3) */
        uint32_t r[8], k;
        ota_frame_done();
        if (p[4] <= 3u)
            acc_test(p[4], r);
        else
            for (k = 0; k < 8u; k++) r[k] = 0xFFu;
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'D';
        k = 5u + ota_pack7((const uint8_t *)r, 32, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
    } else if (n == 4u && p[0] == 0x7D && p[1] == 'J' && p[2] == 'B' && p[3] == 'G') {
        static uint8_t msg[64];                   /* v0.13.18: sec_max[24] (uint16, us), then cleared */
        uint32_t k, i;
        ota_frame_done();
        msg[0] = 0xF0; msg[1] = 0x7D; msg[2] = 'J'; msg[3] = 'B'; msg[4] = 'G';
        k = 5u + ota_pack7((const uint8_t *)sec_max, 48, msg + 5);
        msg[k++] = 0xF7;
        ota_wire_send(msg, k);
        for (i = 0; i < 24u; i++)
            sec_max[i] = 0;
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
extern uint32_t _rt_start[], _rt_end[], _rt_load[], _pool_start[], _pool_end[], _c1_start[], _c1_end[], _c1_load[];

static void jb_main(void)
{
    char line[32];
    uint32_t t_boot, shown = 0xFFu, rescue_ok;

    usb_start();                                  /* 1. USB + update entry first */
    /* v0.13.16: VDC14 (P3_ANA_CON6 bits 2..0, the SYSVDD LDO's input) 1.40 V -> 1.50 V. At 1.40 V the two cores at
     * 240 MHz under write traffic misread single bits (core 0 fetched code from au_rx and faulted); at 1.50 V: 0 errors
     * (JBY modes 9/25, 20 s) and the two-core NAM ran 120 s at 2700 us/block (docs/07, v0.13.15). */
    fm1_p33_write(0x06, (uint8_t)((fm1_p33_read(0x06) & 0xF8u) | VDC14_LEVEL));
    STAGE(3);
    t_boot = now_ms();
    delay_poll(20);
    flash_id = FL_FAR(fl_jedec_ram)();
    flash_ok = (flash_id & 0xFFu) == 0x14u && (flash_id >> 16) != 0u && (flash_id >> 16) != 0xFFu;   /* a 1 MiB part */
    STAGE(4);
    if (flash_ok)
        log_write();
    if (flash_ok && prof.magic == PROF_MAGIC && boot_rec.warm) {   /* the profiler ran: its last 2 x 3 samples */
        uint32_t j, last = prof.n;
        for (j = 0; j < 2u; j++) {
            uint32_t i0 = (last - 1u - 3u * j) & 7u, i1 = (last - 2u - 3u * j) & 7u, i2 = (last - 3u - 3u * j) & 7u;
            boot_rec.magic = 0x46504A42u;         /* "BJPF" */
            boot_rec.boots = last;
            boot_rec.prev_stage = prof.pc0[i0];
            boot_rec.prev_ms = prof.pc1[i0];
            boot_rec.prev_loops = prof.pc0[i1];
            boot_rec.rst_src = prof.pc1[i1];
            boot_rec.p3_rst = (uint8_t)prof.stage[i0];
            boot_rec.wdt_con = (uint8_t)prof.stage[i1];
            boot_rec.failed = (uint8_t)prof.stage[i2];
            log_write();
        }
    }
    prof.magic = 0;
    if (flash_ok && fm1_crash.magic == FM1_CRASH_MAGIC) {   /* the last run faulted: one more record */
        boot_rec.magic = 0x52434A42u;             /* "BJCR" */
        boot_rec.boots = fm1_crash.vec | fm1_crash.core << 16;
        boot_rec.prev_stage = fm1_crash.pc;
        boot_rec.prev_ms = fm1_crash.rets;
        boot_rec.prev_loops = fm1_crash.sp;
        boot_rec.rst_src = fm1_crash.emu;
        boot_rec.p3_rst = (uint8_t)fm1_crash.count;
        boot_rec.wdt_con = (uint8_t)(fm1_crash.dbg & 0xFFu);
        log_write();
        boot_rec.magic = 0x32434A42u;             /* "BJC2": DBG_MSG, write-limit errors, branch trace */
        boot_rec.boots = fm1_crash.dbg;
        boot_rec.prev_stage = fm1_crash.wrerr;
        boot_rec.prev_ms = fm1_crash.etm[0];
        boot_rec.prev_loops = fm1_crash.etm[1];
        boot_rec.rst_src = fm1_crash.uptime_ms;
        log_write();
        fm1_crash.magic = 0;
    }
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
    delay_poll(400);                              /* v0.15: 2 s -> 0.4 s (the boot is part of a capture save) */
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
    if (flash_ok) {                               /* v0.15: the saved capture, else the first one; none: the dry signal */
        ms_scan();
        ms_list_build();
        if (!bk_read())                           /* v0.16: the banks (none saved: the captures in order) */
            bk_defaults();
        bk_saved = bk;
        ui_lang = bk.lang;
        if (bk_slot() != MS_NONE)
            ms_use(bk_slot());                    /* (an error: the dry signal) */
        bk_gains();
        ms_xip_init();
    } else
        bk_defaults();
    /* Two cores need a higher core supply on this board: at the default SYSVDD 1.26 V a busy core 1 makes the
     * chip misbehave (v0.10.x: hangs, brown-out-like resets, USB / audio DMA stalls); 1.32 V (SYSVDD_VOL_SEL 13,
     * P3_ANA_CON9 bits 3..0; the SDK's own range goes to 1.38 V) ran every JB5 load clean. */
    c1_ok = 0;
    nam_on = 0;
    led_init();
    led_on = 1;
    audio_start();
    nam_start();                                  /* v0.13.17: NAM on at boot again (stable since VDC14 = 1.50 V) */
    ui_draw(1);                                   /* v0.15: the player screen */
    ui_dirty = 1;                                 /* (the volume bar and the CLIP labels on the first scan) */
    STAGE(9);
    {
    uint32_t din = 0, first = 1, t_scan = 0, t_bat = 0, t_meter = 0, t_led = 0, t_cpu = 0, i, adc_i = 0, adc_busy = 0;
    int32_t a[N_AIN] = {-1, -1, -1}, a_old[N_AIN] = {-1, -1, -1};   /* v0.17: -1 = no reading yet (0 on the
                                                   * PC10 ladder read as CAB held: bank 6 at every boot) */
    for (;;) {
        uint32_t now = now_ms(), st;
        crumb.ms = now;
        crumb.loops++;
        CRUMB_SEAL();
        if (nam_dbg_marks)
            jb_stage(90);
        loop_at = 0;
        poll();
        if (nam_dbg_marks)
            jb_stage(91);
        loop_at = 1;
        debug_service();
        if (nam_dbg_marks)
            jb_stage(92);
        loop_at = 2;
        usb_retry(now);
        if (usb.uboot_req)                        /* the stock soft key: no UBOOT here (no Mac recovery) */
            usb.uboot_req = 0;
        loop_at = 3;
        ota_service();                            /* M-UPGRADE handshake */
        if (nam_dbg_marks)
            jb_stage(93);
        if (usb.ota_req) {
            usb.ota_req = 0;
            audio_stop();                         /* no buzz while the flash is busy */
            if (c1_ok) {                          /* the flash driver runs from RAM: core 1 must not run RAM
                                                   * code then (its idle loop is RAM): hold it (NAM off until reboot) */
                uint32_t w0 = fm1_ticks();        /* v0.13.18: never mid-job (it now runs between the calls too) */
                while (pipe_busy && fm1_c1_mb.done != fm1_c1_mb.job && fm1_ticks() - w0 < 5000u * FM1_TICKS_PER_US)
                    FM1_C1_SYNC();
                pipe_busy = 0;
                fm1_cpu1_hold();
                c1_ok = 0;
                nam_on = 0;
            }
            if (flash_ok)
                ota_session();                    /* returns only if nothing was committed */
            audio_start();
            shown = 0xFFu;
        }
        if (ms_paused && (int32_t)(now_ms() - ms_last_ms) > (int32_t)MS_IDLE_MS)
            ms_resume();                          /* the host went away mid-way: reboot, back to playing (v0.14.2: a
                                                   * fresh clock, signed: `now` from the loop top is older than
                                                   * ms_last_ms, and unsigned that read as 49 days: a reboot right
                                                   * after the first store command) */
        if (bootguard.pending && now - t_boot > 30000u) {
            bootguard.pending = 0;                /* 30 s up: a good boot */
            bootguard.failed = 0;
            BG_SEAL();
        }
        loop_at = 4;
        st = usb.up && usb.config && !usb.suspended ? 2u : 1u;   /* suspended: bus idle, the cable is out */
        if (st != shown) {
            shown = st;
            ui_usb = st == 2u;
            if (ui_screen == UI_MAIN)
                ui_draw(0);
            else
                ui_info();
        }
        if (nam_dbg_marks)
            jb_stage(94);
        loop_at = 5;
        if (now - t_cpu >= 500u && !(quiet & 2u)) {                /* the info screen (NAM time per block) */
            t_cpu = now;
            ui_info();
        }
        loop_at = 6;
        if (snap_ready) {
            snap_send();
            snap_ready = 0;
        }
        if (now - t_led >= 120u && !(quiet & 5u)) {                /* LED ping-pong: a lit ball with a fading trail */
            uint32_t k;
            t_led = now;
            if (led_test_on && (int32_t)(now - led_test_until) >= 0)
                led_test_on = 0;
            for (k = 0; k < 8u; k++)              /* v0.16: the running bank's LED (mapping: BANK_LED) */
                led_bri[k] = led_test_on ? (led_test >> k & 1u ? 16u : 0u) : 0u;
            if (!led_test_on)
                led_bri[BANK_LED[bk.bank]] = au_umute ? (uint8_t)(now / 240u & 1u ? 16u : 0u) : ui_bypass ? 3u : 16u;
        }
        if (nam_dbg_marks)
            jb_stage(95);
        loop_at = 7;
        if (now - t_meter >= 60u && !(quiet & 2u) && ui_screen == UI_MAIN) {   /* IN / OUT meters, CLIP held 1 s */
            static uint32_t clip_until[2], ev_seen[2];
            static uint8_t clip_shown[2];
            uint32_t ch, w, ev[2];
            t_meter = now;
            ev[0] = au_clip_in_ev;
            ev[1] = au_clip_out_ev;
            for (ch = 0; ch < 2u; ch++) {
                uint32_t pk = au_peak[ch], lv = 0, clip;
                au_peak[ch] = 0;
                if (ev[ch] != ev_seen[ch]) {
                    ev_seen[ch] = ev[ch];
                    clip_until[ch] = now + 1000u;
                }
                clip = (int32_t)(clip_until[ch] - now) > 0;
                {   /* v0.17.2: dBFS, -60 = empty .. 0 = full (136 px): the noise floor (-77..-91 dBFS) no longer lights
                     * the bar. log2 from the top bit plus 4 bits below it: about 0.4 dB steps */
                    uint32_t msb = 0, fr;
                    int32_t db10;                 /* dBFS x 10 */
                    if (pk) {
                        while (pk >> (msb + 1u))
                            msb++;
                        fr = msb >= 4u ? (pk >> (msb - 4u)) & 15u : (pk << (4u - msb)) & 15u;
                        db10 = (int32_t)((msb * 16u + fr) * 602u / 160u) - 1386;   /* 6.02 dB a bit; 2^23 = 0 dBFS */
                    } else
                        db10 = -1200;
                    w = db10 <= -600 ? 0u : db10 >= 0 ? 136u : (uint32_t)(db10 + 600) * 136u / 600u;
                    lv = db10 >= -60 ? 15u : 0u;  /* the last 6 dB: warning colour */
                }
                if (w)
                    lcd_fill(56, UI_MY + 6u + ch * 22u, w, 8, clip ? C_ERR : lv >= 15u ? C_WARN : C_OK);
                if (w < 136u)
                    lcd_fill(56u + w, UI_MY + 6u + ch * 22u, 136u - w, 8, C_BAR);
                if (clip != clip_shown[ch] || ui_dirty) {
                    clip_shown[ch] = (uint8_t)clip;
                    aa_text(198, UI_MY + ch * 22u, 34, clip ? "CLIP" : "", &FS, C_ERR, C_BG, 0);
                }
            }
        }
        ui_encoder(din_read());                   /* v0.15: PARA's quadrature, every loop (4 steps per detent) */
        loop_at = 8;
        if (now - t_scan >= 5u) {                 /* inputs every 5 ms, ADC every 15 ms */
            uint32_t changed = 0;
            t_scan = now;
            din = din_read();
            if (adc_busy) {                       /* one channel per scan: each read every 15 ms, as before */
                a[adc_i] = adc_take();
                if (first || a[adc_i] - a_old[adc_i] > 6 || a_old[adc_i] - a[adc_i] > 6)
                    changed = 1;
                adc_i = (adc_i + 1u) % N_AIN;
            }
            if (now - t_bat >= 1000u) {           /* v0.17: the battery (PC9) once a second */
                t_bat = now;
                bat_update(a[2]);
            }
            adc_start(AIN[adc_i][2]);
            adc_busy = 1;
            loop_at = 10;                         /* (10: after the ADC) */
            if (au_gain_force >= 0)
                au_gain = (uint32_t)au_gain_force;
            else if (a[0] >= 0) {                 /* MASTER (PA10) -> output gain, squared curve, 0..1.0 */
                uint32_t v = (uint32_t)a[0];
                au_gain = v * v * 65536u / (1023u * 1023u);
            }
            loop_at = 9;
            ui_buttons(din, a[1]);                /* v0.16: banks, footswitches, the menu (a[1]: the PC10 ladder) */
            if (first || changed || ui_dirty) {
                if (first || ui_dirty || a[0] - a_old[0] > 6 || a_old[0] - a[0] > 6)
                    ui_vol(a[0]);
                for (i = 0; i < N_AIN; i++)
                    a_old[i] = a[i];
                first = 0;
            }
            ui_dirty = 0;
        }
    }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_cpu1_halt();                              /* a core 1 still running from before this reset would read the
                                                   * RAM cleared below */
    fm1_time_init();
    fm1_reset_reason();                           /* before the watchdog is re-armed (shown at boot) */
    fm1_wdt_arm(0x0D);                            /* ~8 s; fed by poll() */
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (d = _c1b_start; d < _c1b_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                  /* flash driver, NAM kernel: run from RAM */
    for (s = _c1_load, d = _c1_start; d < _c1_end; s++, d++)
        *d = *s;                                  /* the second core's waiting loop */
    fm1_irq_init();                               /* every vector -> the crash record; only the CPU exception on */
    fm1_irq_enable_all();
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
