/*
 * Deep sleep valdymas — v1, "stupid simple": vienintelis wake saltinis yra
 * fizinis PWR mygtukas, jau esantis plokstej ir korpuse (nereikia jokio
 * papildomo litavimo).
 *
 * PATIKRINTA: PWR mygtukas = IO15 (BSP_BUTTONS_IO_1 is oficialaus Waveshare
 * "esp32_s3_cam_ovxxxx.h", ir sutampa su ju Arduino pavyzdzio
 * "#define KEY_PIN 15" + INPUT_PULLUP + LOW-kai-paspausta logika).
 * IO15 yra RTC-capable (ESP32-S3 RTC GPIO diapazonas = 0-21), tad tinka
 * esp_sleep_enable_ext0_wakeup() — GREITAS pabudimas (RTC atmintis
 * islieka), NE pilnas reboot per EN/CHIP_PU.
 *
 * SAMONINGAI NEIDIEGTA sioje versijoje (atidėta, kol nebus fiziskai
 * patikrinta su plokste): FT6336 touch "Monitor mode" wake per IO9,
 * LD2410/PCF8574 radaro wake. Zr. README.md "Zinomos spragos".
 */
#pragma once
#include <Arduino.h>

#define PWR_BUTTON_PIN 15

void DeepSleep_Init();              // kviesti setup() pradzioje
bool DeepSleep_WasWokenByButton();  // true, jei sis paleidimas — pabudimas is deep sleep
void DeepSleep_EnterSleep();        // niekada negrazina valdymo — MCU uzmiega/persikraus pabudes
