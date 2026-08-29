/*
 * FT6336 talpinio touch valdiklio minimalus I2C nuskaitymas.
 *
 * PATIKRINTA pagal oficialu Espressif/Waveshare komponenta:
 *   ESP32-S3-CAM-OVxxxx/examples/ESP-IDF-v5.5.1/.../
 *   components/waveshare__esp_lcd_touch_ft6336/esp_lcd_touch_ft6336.c
 *
 * I2C adresas 0x38, registrai: 0x02=lieciamu tasku sk., 0x03.. = X_H/X_L/Y_H/Y_L.
 * RESET valdomas per CH32V003 IO pletiklio P0 kanala (ne tiesioginis GPIO) —
 * zr. io_extension.h. INT signalas — ESP32 native GPIO9 (patikrinta schema).
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>

#define TOUCH_FT6336_I2C_ADDR   (uint8_t)0x38
#define TOUCH_FT6336_INT_PIN    9   // native ESP32 GPIO, patikrinta schema

struct TouchPoint {
    bool touched;
    uint16_t x;
    uint16_t y;
};

void Touch_FT6336_Init(TwoWire &wire);
TouchPoint Touch_FT6336_Read();
