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
#define JB_VERSION "v0.2.2"
#define FELUCCA_LOADER 1                 /* usb.c: the polled, MIDI-only build of Felucca's loader */
#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_OTA_RAMONLY 1            /* RAM update record only: no flash record at a location
                                          * the BlackBox SPL was never seen to scan */
#define FELUCCA_USB_PID 0x0001
#ifndef FELUCCA_ID
#define FELUCCA_ID "BlackBox_906"        /* handshake identity == package marker (fw/build.py) */
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

/* BlackBox flash map (docs/01): app area 0x4000..0xA9000 (flash.bin = 0xA5000), VM 0xA9000..0xC2000,
 * BTIF 0xC2000, USR 0xC3000.., key_mac 0xFF000. The loader is staged near the end of VM, as the stock
 * firmware and Felucca do on the FM-1 (whose VM ends at 0xE8000). */
#define OTA_AREA 0x000BA000u
#define OTA_AREA_LEN 0x5000u
#define OTA_FL_LEN 0x000A5000u
#define OTA_LOADER_ID "ota-BlackBox_906"

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

static void poll(void)                          /* watchdog, USB at ~2 kHz */
{
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
    uint32_t magic, pending, failed, rescues, boots;
} bootguard __attribute__((section(".noinit")));

/* where a boot got to (RAM that survives warm resets): logged by the next boot */
static struct {
    uint32_t magic, stage, ms, loops;
} crumb __attribute__((section(".noinit")));
#define STAGE(n) (crumb.stage = (n))

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
    bootguard.pending = 0;                        /* intentional reset */
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
    }
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

    STAGE(9);
    for (;;) {
        uint32_t now = now_ms(), st;
        crumb.ms = now;
        crumb.loops++;
        poll();
        debug_service();
        usb_retry(now);
        if (usb.uboot_req)                        /* the stock soft key: no UBOOT here (no Mac recovery) */
            usb.uboot_req = 0;
        ota_service();                            /* M-UPGRADE handshake */
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                    /* returns only if nothing was committed */
            shown = 0xFFu;
        }
        if (bootguard.pending && now - t_boot > 30000u) {
            bootguard.pending = 0;                /* 30 s up: a good boot */
            bootguard.failed = 0;
        }
        st = usb.up && usb.config && !usb.suspended ? 2u : 1u;   /* suspended: bus idle, the cable is out */
        if (st != shown) {
            shown = st;
            status(st == 2u ? JB_NAME " " JB_VERSION "  USB OK" : JB_NAME " " JB_VERSION,
                   st == 2u ? C_OK : C_TEXT);
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
    if (bootguard.magic != BOOTGUARD_MAGIC || bootguard.boots > 100000u || bootguard.rescues > 1000u ||
        bootguard.failed > 16u || bootguard.pending > 1u) {      /* (the update loaders use this RAM too) */
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.pending = 0;
        bootguard.failed = 0;
        bootguard.rescues = 0;
        bootguard.boots = 0;
    }
    bootguard.boots++;
    boot_rec.magic = LOG_MAGIC;
    boot_rec.boots = bootguard.boots;
    boot_rec.warm = crumb.magic == BOOTGUARD_MAGIC;
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
    if (bootguard.pending)
        bootguard.failed++;
    boot_rec.failed = (uint8_t)bootguard.failed;
    bootguard.pending = 1;
    fm1_updata_parm_clear();                      /* no stale update record for the next warm reset */
    if (bootguard.failed >= 2u) {
        bootguard.failed = 0;
        if (rescue_staged())
            rescue_enter();
    }
    jb_main();
}
