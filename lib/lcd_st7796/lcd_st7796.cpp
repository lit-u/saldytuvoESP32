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
    // PASTABA 2026-09-03: nufilmuotas 5-spalvu testas parode, kad LVGL
    // piesimo kelias daro ciklinę R/G/B kanalu rotacija (RAUDONA<->MELYNA<->
    // ZALIA, balta/juoda nepaveiktos). Bandytas pataisymas CIA (per-pikselio
    // korekcija pries SPI siuntima) VEIKE statiniams testo ekranams, bet
    // SUGADINO realu UI su animacijomis (LV_DISPLAY_RENDER_MODE_FULL + spinner
    // = daug pilno ekrano flush'u, per-pikselio ciklas per lete/nesuderinta) —
    // realiu testu matytas "sniego" triuksmas ekrane. PASALINTA del rizikos.
    // Zalia/melyna sukeitimas lieka kaip zinomas kosmetinis trukumas — zr.
    // git istorija, jei bus noro dar karta bandyti (pvz. lookup lentele vietoj
    // ciklo, arba rasti tiksli klaidos vieta LVGL viduje).
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
