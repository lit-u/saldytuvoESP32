#include "lcd_st7796.h"
#include "io_extension.h"
#include <SPI.h>

static SPIClass s_spi(HSPI);

static void lcd_send_cmd(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                          const uint8_t *param, size_t param_size) {
    (void)cmd_size;
    s_spi.beginTransaction(SPISettings(LCD_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(LCD_PIN_CS, LOW);

    digitalWrite(LCD_PIN_DC, LOW);
    s_spi.transfer(cmd[0]);

    if (param_size) {
        digitalWrite(LCD_PIN_DC, HIGH);
        s_spi.writeBytes(param, param_size);
    }

    digitalWrite(LCD_PIN_CS, HIGH);
    s_spi.endTransaction();
}

static void lcd_send_color(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                            uint8_t *param, size_t param_size) {
    (void)cmd_size;
    // RASTA TIKSLI PRIEZASTIS 2026-09-03 (ChatGPT pasiulymas palyginti baitus
    // baitas-baitu su LCD_RawFill): LVGL laiko kiekviena pikseli KAIP NATIVE
    // (little-endian) uint16_t — RAUDONA (0xF800) buferyje guli kaip baitai
    // {0x00, 0xF8}, ne {0xF8, 0x00}. ST7796 (kaip ir musu pacios patikrinta
    // LCD_RawFill) tikisi BIG-ENDIAN (auksta baita pirma) per SPI. Anksciau
    // bandytas "kanalu rotacijos" pataisymas buvo simptomo, ne priezasties,
    // korekcija — sitas paprastas baitu apsikeitimas yra TIKRAS sprendimas.
    size_t pixelCount = param_size / 2;
    uint16_t *pixels = reinterpret_cast<uint16_t *>(param);
    for (size_t i = 0; i < pixelCount; i++) {
        pixels[i] = __builtin_bswap16(pixels[i]);
    }

    s_spi.beginTransaction(SPISettings(LCD_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(LCD_PIN_CS, LOW);

    digitalWrite(LCD_PIN_DC, LOW);
    s_spi.transfer(cmd[0]);

    digitalWrite(LCD_PIN_DC, HIGH);
    s_spi.writeBytes(param, param_size);

    digitalWrite(LCD_PIN_CS, HIGH);
    s_spi.endTransaction();

    lv_display_flush_ready(disp);
}

static void raw_cmd(uint8_t cmd, const uint8_t *data, size_t len) {
    s_spi.beginTransaction(SPISettings(LCD_SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(LCD_PIN_CS, LOW);
    digitalWrite(LCD_PIN_DC, LOW);
    s_spi.transfer(cmd);
    if (len) {
        digitalWrite(LCD_PIN_DC, HIGH);
        s_spi.writeBytes(data, len);
    }
    digitalWrite(LCD_PIN_CS, HIGH);
    s_spi.endTransaction();
}

// Waveshare oficialaus ESP-IDF pavyzdzio (esp_lcd_st7796.c, produktas
// "ESP32-S3-Touch-LCD-3.5" — tiksliai musu plokstes ekranas) galios/gama
// derinimo reiksmes — PATIKRINTA 2026-09-03, siunciamos PO LVGL init, kad
// perrasytu tuos pacius registrus tiksliomis Waveshare reiksmemis (kontrasto/
// spalvu kokybes pagerinimui). PASTABA: sios reiksmes NEVALDO R/G/B kanalu
// tvarkos (tai MADCTL/BGR flag'o darbas, jau teisingas) — nesitiketi, kad tai
// pataisys ziname zalios/melynos sukeitima (tas yra LVGL piesimo pipeline'e).
static void apply_waveshare_gamma_power_tuning() {
    raw_cmd(0xF0, (const uint8_t[]){0xC3}, 1);
    raw_cmd(0xF0, (const uint8_t[]){0x96}, 1);
    raw_cmd(0xB4, (const uint8_t[]){0x01}, 1);
    raw_cmd(0xB7, (const uint8_t[]){0xC6}, 1);
    raw_cmd(0xC0, (const uint8_t[]){0x80, 0x45}, 2);
    raw_cmd(0xC1, (const uint8_t[]){0x13}, 1);
    raw_cmd(0xC2, (const uint8_t[]){0xA7}, 1);
    raw_cmd(0xC5, (const uint8_t[]){0x0A}, 1);
    raw_cmd(0xE8, (const uint8_t[]){0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 8);
    raw_cmd(0xE0, (const uint8_t[]){0xD0, 0x08, 0x0F, 0x06, 0x06, 0x33, 0x30, 0x33,
                                     0x47, 0x17, 0x13, 0x13, 0x2B, 0x31}, 14);
    raw_cmd(0xE1, (const uint8_t[]){0xD0, 0x0A, 0x11, 0x0B, 0x09, 0x07, 0x2F, 0x33,
                                     0x47, 0x38, 0x15, 0x16, 0x2C, 0x32}, 14);
    raw_cmd(0xF0, (const uint8_t[]){0x3C}, 1);
    raw_cmd(0xF0, (const uint8_t[]){0x69}, 1);
    delay(120);
}

static void lcd_reset() {
    // Patikrinta seka (lcd_driver.cpp -> lcd_reset()): P1=0 10ms, P1=1 50ms.
    IO_EXTENSION_Output(IO_EXTENSION_LCD_RST_PIN, 0);
    delay(10);
    IO_EXTENSION_Output(IO_EXTENSION_LCD_RST_PIN, 1);
    delay(50);
}

void LCD_Backlight_Set(uint8_t percent) {
    IO_EXTENSION_Pwm_Output(percent);
}

lv_display_t *LCD_ST7796_Init() {
    pinMode(LCD_PIN_DC, OUTPUT);
    pinMode(LCD_PIN_CS, OUTPUT);
    digitalWrite(LCD_PIN_CS, HIGH);

    s_spi.begin(LCD_PIN_SCLK, -1 /* MISO nenaudojamas */, LCD_PIN_MOSI, LCD_PIN_CS);

    lcd_reset();

    lv_display_t *disp = lv_st7796_create(LCD_H_RES, LCD_V_RES, LV_LCD_FLAG_BGR,
                                           lcd_send_cmd, lcd_send_color);
    // Diagnostika 2026-09-03: BALTA<->JUODA tiksliai apsikeite realiu testu
    // (zr. main.cpp LCD spalvu testas) — bandome LVGL inversijos perjungima.
    lv_st7796_set_invert(disp, true);
    apply_waveshare_gamma_power_tuning();

    // lv_st7796_create() nepriskiria pieszimo buferio — tai turime padaryti
    // patys (kaip ir patikrintame Waveshare pavyzdyje, lvgl_driver.cpp).
    // Du pilno ekrano buferiai PSRAM, LV_DISPLAY_RENDER_MODE_FULL.
    uint32_t bufSizePx = (uint32_t)LCD_H_RES * LCD_V_RES;
    size_t bufBytes = bufSizePx * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_display_set_buffers(disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_FULL);

    return disp;
}
