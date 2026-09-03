/*
 * ST7796 (3.5" LCD) draiveris — SPI transportas LVGL v9 "generic MIPI"
 * varikliui (lv_st7796_create), pritaikytas Waveshare ESP32-S3-CAM-OVxxxx
 * plokstei.
 *
 * PATIKRINTA is oficialaus Waveshare pavyzdzio (Board_Configuration.h,
 * lcd_driver.cpp — ten naudota 2 coliu ST7789 versija, cia analogiskai
 * pritaikyta ST7796):
 *   MOSI=IO1, SCLK=IO5, DC=IO3, CS=IO6, MISO nenaudojamas (rasyk-only SPI),
 *   80MHz pclk. RESET -> CH32V003 EXIO P1, BACKLIGHT -> EXIO PWM registras.
 *
 * PATIKRINTA REALIU HARDWARE 2026-09-03 (nufilmuotas 5-spalvu testas su
 * tiksliais laiko zymekliais): MADCTL BGR + invert=true (zr. .cpp) parodo
 * RAUDONA/BALTA/JUODA teisingai. Likusi ZALIA/MELYNA (ciklinė R/G/B kanalu
 * rotacija LVGL piesimo kelyje, tiksli vieta LVGL viduje nerasta) pataisyta
 * lcd_send_color() funkcijoje, TIESIOGIAI pries SPI siuntima (zr. .cpp
 * komentara) — patvirtinta, kad visos 5 spalvos dabar rodomos teisingai.
 */
#pragma once
#include <Arduino.h>
#include <lvgl.h>

#define LCD_PIN_MOSI   1
#define LCD_PIN_SCLK   5
#define LCD_PIN_DC     3
#define LCD_PIN_CS     6
#define LCD_SPI_HZ     40000000  // pradziai 40MHz (saugiau uz 80MHz pirmam bandymui)

#define LCD_H_RES      320
#define LCD_V_RES      480

lv_display_t *LCD_ST7796_Init();
void LCD_Backlight_Set(uint8_t percent);  // 0-100
