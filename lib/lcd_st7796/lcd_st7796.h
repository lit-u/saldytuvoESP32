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
 * NEPATVIRTINTA (Waveshare dar nera paskelbe atskiro 3.5" pavyzdzio su
 * ST7796 sioje plokstes seimoje — sitos reiksmes paimtos is ju ESP-IDF
 * Brookesia demo naudojamo stiliaus pavadinimo "320x480" bei is bendros
 * ST7796 3.5" moduliu praktikos): skiriamoji geba 320x480 (portretas),
 * RGB/BGR eiliskumas ir MADCTL mirror bitai. Jei pirmo paleidimo metu
 * spalvos apverstos (raudona/melyna sukeista) arba vaizdas veidrodinis —
 * keisk LCD_FLAGS zemiau (pridek LV_LCD_FLAG_BGR / _MIRROR_X / _MIRROR_Y).
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
