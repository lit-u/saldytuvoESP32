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

void IO_EXTENSION_Init(TwoWire &wire);
void IO_EXTENSION_Output(uint8_t pin, uint8_t value);
uint8_t IO_EXTENSION_Input(uint8_t pin);
void IO_EXTENSION_Pwm_Output(uint8_t percent);   // 0-100, backlight skaistis
uint16_t IO_EXTENSION_Adc_Input();
