/*
 * IO_EXTENSION — draiveris pagalbiniam CH32V003 IO pletiklio kontroleriui,
 * pasiekiamam per bendra I2C magistrale (IO7=SCL/IO8=SDA).
 *
 * PATIKRINTA pagal oficialu Waveshare source koda (du nepriklausomi
 * saltiniai sutampa):
 *   - waveshareteam/ESP32-S3-CAM-OVxxxx :
 *     examples/Arduino-v3.2.0/examples/01_lvgl_example/io_extension.h
 *   - waveshareteam/Waveshare-ESP32-components :
 *     bsp/esp32_s3_cam_ovxxxx/esp32_s3_cam_ovxxxx.c
 *     (custom_io_expander_new_i2c_ch32v003, adresas BSP_IO_EXPANDER_I2C_ADDRESS_CH32V003)
 *
 * Kanalu paskirtis (patvirtinta faktiniu kodu, ne vien schemos zymejimu):
 *   P0  -> Touch (FT6336) RESET
 *   P1  -> LCD (ST7796) RESET
 *   P3  -> Kameros (OV5640) PWDN — patikrinta pagal "PIN OUT" lentele
 *          schemoje (EXIO3 -> CAM_PWDN). PWDN=0 -> kamera aktyvi.
 *   P4  -> Garsiakalbio stiprintuvo (NS4150B) IJUNGIMAS (PA_EN) — PATIKRINTA
 *          2026-09-05 pagal TIKRA oficialu Waveshare source koda (ne vien
 *          schemos zymejimu): waveshareteam/ESP32-S3-CAM-OVxxxx,
 *          examples/ESP-IDF-v5.5.1/03_audio_play/components/bsp_extra/src/
 *          bsp_board_extra.c — `Audio_PA_EN()` kviecia
 *          `esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_4, 1)`
 *          PRIES kiekviena grojima, `Audio_PA_DIS()` (level=0) po/pries.
 *          BE SIO garsiakalbis TYLI, nors ES8311+I2S viskas veikia teisingai
 *          (zr. lib/audio_output/ — rasta po ilgos diagnostikos, kai visi
 *          kiti sluoksniai raportavo sekme, bet garso NEBUVO).
 *   P6  -> Waveshare BSP komentuoja sia kaip raudona LED (bsp.h,
 *          "BSP_LED_RED = IO_EXPANDER_PIN_NUM_6"), BET fiziskai isbandyta
 *          2026-09-05 IR NEUZSIDEGA siame irenginyje (galimai nesulituota
 *          si konkreti versija, arba korpusas ja pilnai dengia) — "veikia"
 *          indikatoriui naudojamas VIRTUALUS raudonas taskas EKRANE
 *          (zr. ui_screens.cpp createStatusDot), NE si pin'as.
 *   PWM registro reiksme (0-100) -> LCD BACKLIGHT skaistis
 *   ADC registras -> baterijos itampa (BAT_ADC)
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>

#define IO_EXTENSION_I2C_ADDR         (uint8_t)0x24

#define IO_EXTENSION_REG_MODE         0x02
#define IO_EXTENSION_REG_OUTPUT       0x03
#define IO_EXTENSION_REG_INPUT        0x04
#define IO_EXTENSION_REG_PWM          0x05
#define IO_EXTENSION_REG_ADC          0x06

#define IO_EXTENSION_TOUCH_RST_PIN    0  // P0
#define IO_EXTENSION_LCD_RST_PIN      1  // P1
#define IO_EXTENSION_CAM_PWDN_PIN     3  // P3
#define IO_EXTENSION_AUDIO_PA_PIN     4  // P4 — garsiakalbio stiprintuvo (NS4150B) ijungimas

void IO_EXTENSION_Init(TwoWire &wire);
void IO_EXTENSION_Output(uint8_t pin, uint8_t value);
uint8_t IO_EXTENSION_Input(uint8_t pin);
void IO_EXTENSION_Pwm_Output(uint8_t percent);   // 0-100, backlight skaistis
uint16_t IO_EXTENSION_Adc_Input();
