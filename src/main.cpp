/*
 * Ismanusis saldytuvo terminalas — ESP32-S3-CAM-OVxxxx (Waveshare)
 * Pradinis projekto sablonas: Wi-Fi + NTP, CH32V003 IO pletiklis (EXIO),
 * SD kortele (native SDMMC), ST7796 LCD + LVGL, FT6336 touch.
 *
 * MAITINIMAS (v1, "stupid simple"): irenginys po SCREEN_AWAKE_TIMEOUT_MS
 * (app_state_machine.cpp) be aktyvumo iskart uzmiega (esp_deep_sleep_start).
 * Vienintelis pabudinimo saltinis — fizinis PWR mygtukas (IO15, jau yra
 * plokstej/korpuse), per ext0 RTC GPIO wake. Zr. lib/deep_sleep/.
 *
 * LD2410 radaras + PCF8574 — PASALINTA (nebuvo naudojama v1 nuo pat pradzios,
 * nes visi ESP32 native GPIO uzimti; PCF8574 buvo vienintelis I2C apejimas).
 * Pilna architektura (I2C 0x20, wiring, ir rasta klaida — digitalRead()
 * grazina HIGH, ne saugu LOW, kai irenginys neprijungtas) — zr. README.md
 * "Radaras / PCF8574 (v2 galimybe, kodas pasalintas)".
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <SD_MMC.h>
#include <lvgl.h>

#include "io_extension.h"
#include "lcd_st7796.h"
#include "touch_ft6336.h"
#include "app_state_machine.h"
#include "family_messages.h"
#include "deep_sleep.h"

// ---------------------------------------------------------------------------
// KONFIGURACIJA
// ---------------------------------------------------------------------------

// --- Wi-Fi ---
static const char *WIFI_SSID = "TAVO_WIFI_SSID";
static const char *WIFI_PASSWORD = "TAVO_WIFI_SLAPTAZODIS";

// --- NTP / laiko sinchronizacija ---
static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "time.google.com";
static const char *TIME_ZONE = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// --- Bendra I2C magistrale (patikrinta schema: IO7=SCL, IO8=SDA) ---
// Ant sios magistrales kabo: CH32V003 EXIO pletiklis (0x24), FT6336 touch
// (0x38), audio kodekai (nenaudojami dar).
static const int I2C_SDA_PIN = 8;
static const int I2C_SCL_PIN = 7;

// --- SD kortele (pagrindines plokstes lizdas, native SDMMC 1-bit) ---
// PATIKRINTA pagal Waveshare BSP (bsp_sdcard_sdmmc_mount): CLK=IO16,
// CMD=IO43, D0=IO44, CS nenaudojamas.
static const int SD_CLK_PIN = 16;
static const int SD_CMD_PIN = 43;
static const int SD_D0_PIN = 44;

static lv_display_t *g_lcdDisplay = nullptr;

// ---------------------------------------------------------------------------
// PAGALBINES FUNKCIJOS
// ---------------------------------------------------------------------------

void connectWiFi() {
    Serial.printf("[WiFi] Jungiamasi prie \"%s\"...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
        delay(300);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Prisijungta. IP adresas: %s\n",
                       WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Nepavyko prisijungti per 15s. Bus bandoma veliau.");
        // TODO: numatyti fallback i Access Point (WiFi.softAP) pirmam setup'ui
    }
}

void syncTime() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[NTP] Praleista — nera Wi-Fi rysio.");
        return;
    }

    Serial.println("[NTP] Sinchronizuojamas laikas...");
    configTzTime(TIME_ZONE, NTP_SERVER_1, NTP_SERVER_2);

    struct tm timeInfo;
    if (getLocalTime(&timeInfo, 10000)) {
        Serial.printf("[NTP] Laikas sinchronizuotas: %02d:%02d:%02d  %04d-%02d-%02d\n",
                      timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec,
                      timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday);
    } else {
        Serial.println("[NTP] Nepavyko gauti laiko per 10s.");
    }
}

bool initSDCard() {
    Serial.println("[SD] Inicijuojama SD kortele (native SDMMC 1-bit)...");
    SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
    if (!SD_MMC.begin("/sdcard", true /* 1-bit rezimas */)) {
        Serial.println("[SD] Klaida: SD kortele nerasta arba nepavyko inicijuoti.");
        return false;
    }

    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[SD] Klaida: kortele neiterpta.");
        return false;
    }

    uint64_t cardSizeMB = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("[SD] Kortele rasta. Dydis: %llu MB\n", cardSizeMB);
    return true;
}

// LVGL touch input device read callback (FT6336 per bendra I2C magistrale).
static void touchpadReadCb(lv_indev_t *indev, lv_indev_data_t *data) {
    TouchPoint tp = Touch_FT6336_Read();
    if (tp.touched) {
        data->point.x = tp.x;
        data->point.y = tp.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static uint32_t lvglTickCb() {
    return millis();
}

void initDisplay() {
    lv_init();
    lv_tick_set_cb(lvglTickCb);

    g_lcdDisplay = LCD_ST7796_Init();

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpadReadCb);

    // Ekrano isjungimas/ijungimas ir pradinio turinio rodymas dabar
    // priklauso AppStateMachine (zr. app_state_machine.cpp) — cia LCD tik
    // paruosiamas aparaturiskai (SPI/EXIO), backlight liks isjungtas kol
    // AppStateMachine_Init() ivykdys enterStandby().
    LCD_Backlight_Set(0);
}

// ---------------------------------------------------------------------------
// SETUP / LOOP
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Ismanusis saldytuvo terminalas — starta ===");

    DeepSleep_Init();

    if (psramFound()) {
        Serial.printf("[PSRAM] Rasta. Laisva: %u KB\n", ESP.getFreePsram() / 1024);
    } else {
        Serial.println("[PSRAM] KLAIDA: PSRAM nerastas! Patikrink platformio.ini "
                        "nustatymus (memory_type = qio_opi).");
    }

    // Bendra I2C magistrale — turi buti inicijuota PRIES visus I2C
    // irenginius (EXIO, touch, PCF8574).
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    IO_EXTENSION_Init(Wire);

    // PATAISYTA po pastabos: WiFi.begin() (iki 15s) + NTP sync (iki 10s)
    // KIEKVIENA pabudima is deep sleep butu paneigie visa ext0/RTC-wake
    // greicio nauda (~25s delsa vietoj "greito" pabudimo). ESP32-S3 sistemos
    // laikrodis (settimeofday/SNTP nustatytas) ISLIEKA per deep sleep, nes
    // RTC domenas nemiega — tad WiFi+NTP kviesim TIK saltam paleidimui
    // (USB power-on/flash), NE kiekvienam mygtuko paspaudimui.
    if (!DeepSleep_WasWokenByButton()) {
        connectWiFi();
        syncTime();
    } else {
        Serial.println("[WiFi/NTP] Praleista — pabudimas is deep sleep, "
                        "sistemos laikrodis islikes is RTC domeno.");
    }

    initSDCard();

    initDisplay();
    Touch_FT6336_Init(Wire);

    FamilyMessages_Init();
    AppStateMachine_Init();

    // v1 "stupid simple": kiekvienas paleidimas (tiek pirmas USB power-on,
    // tiek pabudimas is deep sleep per PWR mygtuka) is karto rodo ekrana —
    // nedarome skirtumo tarp priezasciu, kad logika liktu paprasta.
    AppStateMachine_Update(true);

    // TODO 1: initCamera() — OV5640 inicializavimas (esp_camera.h), kad
    //         FaceRecognition_Identify() (face_recognition.cpp) galetu
    //         gauti tikrus kadrus is esp_camera_fb_get().
    // TODO 2: initWebServer() — ESPAsyncWebServer: MJPEG/stream endpoint,
    //         POST /api/message -> FamilyMessages_Set(), POST /api/brightness,
    //         POST /api/radar-distance (zr. app_state_machine.h TODO apie
    //         LD2410 atstumo konfiguracijos apribojima).
    // TODO 3: initAudio() — I2S mikrofonai (MIC1/MIC2) ir I2S garsiakalbis,
    //         kad ui_screens.cpp galetu atkurti PersonProfile.greetingAudioFile.
}

void loop() {
    // Radaras pasalintas — v1 nebeturi "judesio" ivesties, tik fiksuotas
    // SCREEN_AWAKE_TIMEOUT_MS nuo pasisveikinimo pradzios (zr. WAKE ->
    // AppStateMachine_Update(true) setup() f-joje).
    AppStateMachine_Update(false);
    lv_timer_handler();

    // v1: kai state machine pati nusprendzia grizti i STANDBY (15s be
    // aktyvumo — zr. app_state_machine.cpp), is karto uzmiegame. PWR
    // mygtukas (IO15/ext0) yra vienintelis pazadinimo saltinis.
    if (AppStateMachine_GetState() == APP_STATE_STANDBY) {
        DeepSleep_EnterSleep();
    }

    // TODO: cia vėliau bus web serverio handleClient() (jei ne async),
    // garso buferio apdorojimas ir t.t.

    delay(5);
}
