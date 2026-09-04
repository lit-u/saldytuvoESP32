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
#include <esp_camera.h>
#include <ESPAsyncWebServer.h>
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
// server-face-recognition saka: veido atpazinimas per laptopo serveri (zr.
// README "Serveris-pagrindu atpazinimas") — WiFi PRIVALOMAS, ne pasirenkamas.
// Tikri kredencialai include/secrets.h (NIEKADA necommit'inti — .gitignore).
#include "secrets.h"
static const char *WIFI_SSID = SECRET_WIFI_SSID;
static const char *WIFI_PASSWORD = SECRET_WIFI_PASSWORD;

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

// --- Kamera (OV5640, DVP sasaja) ---
// PATIKRINTA pagal oficialu Waveshare BSP (esp32_s3_cam_ovxxxx.h,
// BSP_CAMERA_DEFAULT_CONFIG makrosas): SCCB (I2C) EINA PER TA PACIA bendra
// magistrale (IO7/IO8), ne per atskirus pin'us — todel sccb_i2c_port
// naudoja jau vykdoma Wire.begin(), o pin_sccb_sda/scl = -1. PWDN valdomas
// per CH32V003 EXIO3 (IO_EXTENSION_CAM_PWDN_PIN), NE tiesiogini GPIO.
static const int CAM_XCLK_PIN = 38;
static const int CAM_PCLK_PIN = 41;
static const int CAM_VSYNC_PIN = 17;
static const int CAM_HREF_PIN = 18;
static const int CAM_D0_PIN = 45;
static const int CAM_D1_PIN = 47;
static const int CAM_D2_PIN = 48;
static const int CAM_D3_PIN = 46;
static const int CAM_D4_PIN = 42;
static const int CAM_D5_PIN = 40;
static const int CAM_D6_PIN = 39;
static const int CAM_D7_PIN = 21;

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
    // Diagnostika 2026-09-03: 0x107 (ESP_ERR_TIMEOUT) send_op_cond zingsnyje
    // net su TIKRAI veikiancia (kitame kompiuteryje ka tik performatuota)
    // kortele — bandome zemesni SDMMC_FREQ_DEFAULT (20MHz) vietoj numatytojo
    // SDMMC_FREQ_HIGHSPEED (40MHz), jei tai laikinimo/signalo kokybes klausimas.
    if (!SD_MMC.begin("/sdcard", true /* 1-bit rezimas */, false, SDMMC_FREQ_DEFAULT)) {
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

bool initCamera() {
    Serial.println("[Camera] Inicijuojama OV5640...");

    // PWDN=0 -> kamera aktyvi. Butina PALAUKTI po EXIO komandos, kad
    // sensorius spetu pilnai atsibusti pries SCCB (I2C) kalba.
    IO_EXTENSION_Output(IO_EXTENSION_CAM_PWDN_PIN, 0);
    delay(10);

    camera_config_t config = {};
    config.pin_pwdn = -1;      // valdoma per CH32V003 EXIO3, ne cia
    config.pin_reset = -1;     // OV5640 sioje plokstej neturi atskiro RESET pin'o
    config.pin_xclk = CAM_XCLK_PIN;
    config.pin_sccb_sda = -1;  // SCCB per JAU VEIKIANCIA Wire magistrale
    config.pin_sccb_scl = -1;
    config.sccb_i2c_port = 0;  // Wire numatytasis I2C portas
    config.pin_d7 = CAM_D7_PIN;
    config.pin_d6 = CAM_D6_PIN;
    config.pin_d5 = CAM_D5_PIN;
    config.pin_d4 = CAM_D4_PIN;
    config.pin_d3 = CAM_D3_PIN;
    config.pin_d2 = CAM_D2_PIN;
    config.pin_d1 = CAM_D1_PIN;
    config.pin_d0 = CAM_D0_PIN;
    config.pin_vsync = CAM_VSYNC_PIN;
    config.pin_href = CAM_HREF_PIN;
    config.pin_pclk = CAM_PCLK_PIN;
    // server-face-recognition saka: JPEG (serveriui siunciamas tiesiogiai,
    // nereikia konversijos). BUVO FRAMESIZE_SXGA (1280x1024) — PATIKRINTA
    // 2026-08-31: 640x480 (VGA) MTCNN VISAI NERADO veido (per mazai detaliu).
    //
    // DIAGNOSTIKA 2026-09-03: vartotojas patvirtino svariu A/B testu (tas
    // pats failas kompe->telefonui per WiFi = 31-35s, tas pats failas
    // TIESIOGIAI telefone/localhost = 2s), kad letumo priezastis NE telefono
    // apdorojimas (greitas!), o DUOMENU PERDAVIMAS i telefona per WiFi —
    // telefonas VIENU METU yra ir hotspot'as (routina ESP32+kompo srauta),
    // IR vykdo atpazinima, tad realus pralaidumas i ji ribotas net su geru
    // signalu (88%, 144Mbps). ESP32 SIUNCIA TA PATI kelia telefonui, tad
    // mazesnis kadras = maziau duomenu per apkrauta hotspot'a = greiciau.
    // SVGA (800x600) — tarpinis pasirinkimas tarp SXGA (per daug duomenu)
    // ir VGA (jau zinoma nepakankama detale MTCNN veido aptikimui).
    // xclk_freq_hz=10MHz + fb_count=1 PALIKTA is ankstesnio (eloquent-facelib-
    // experiment sakos) rasto DMA/cam_task crash fix'o (RGB565+20MHz+fb_count=2
    // sukeldavo "EV-EOF-OVF" -> Guru Meditation kas boot'a) — sios nuostatos
    // patikrintos stabilios realiu hardware, nekeisti be priezasties.
    config.xclk_freq_hz = 10000000;
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_SVGA;      // 800x600 (buvo SXGA 1280x1024)
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[Camera] KLAIDA: esp_camera_init() nepavyko (0x%x).\n", err);
        return false;
    }

    // Vienkartinis bandomasis kadras — patikrinti, kad sensorius fiziskai
    // gyvas, PRIES investuojant i veido atpazinimo integracija.
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == nullptr) {
        Serial.println("[Camera] KLAIDA: init pavyko, bet kadro paimti nepavyko.");
        return false;
    }

    // PASTABA (JPEG formatas): suspaustas srautas beveik visada bus ~100%
    // ne-nulinis nepriklausomai nuo realaus vaizdo turinio (net visiskai
    // juodas kadras turi nemaza JPEG antrasciu/DC koeficientu baitu srauta).
    // Sitas patikrinimas TIK patvirtina, kad apskritai gavome baitus, NE kad
    // vaizdas ne juodas. Dydis/rezoliucija VIENA taip pat nepatvirtina —
    // paprastas patikrinimas: suskaiciuoti ne-nulinius baitus is imties
    // (zingsnis 97, nesvarbu formatui, paliktas is ankstesnio RGB565 etapo).
    size_t sampled = 0, nonZero = 0;
    for (size_t i = 0; i < fb->len; i += 97) {
        sampled++;
        if (fb->buf[i] != 0) nonZero++;
    }
    uint8_t nonZeroPct = sampled ? (uint8_t)((nonZero * 100) / sampled) : 0;

    Serial.printf("[Camera] Bandomasis kadras: %ux%u, %u baitu, ~%u%% ne-nuliniu baitu (imtis).\n",
                  fb->width, fb->height, (unsigned)fb->len, nonZeroPct);
    if (nonZeroPct == 0) {
        Serial.println("[Camera] ISPEJIMAS: kadras atrodo VISISKAI TUSCIAS/JUODAS. "
                        "GALIMOS PRIEZASTYS: (a) kamera testo metu nukreipta i "
                        "tamsu/uzdengta plota (stalvirsi, tamsu kambari) — TIKRAS "
                        "vaizdas, ne klaida, arba (b) PWDN valdymas (EXIO3) / FPC "
                        "kabelio prijungimas sugedes. Pries diagnozuojant hardware — "
                        "nukreipk kamera i kontrastinga objekta (langa, spalvota "
                        "daikta) ir bandyk vel.");
    }

    esp_camera_fb_return(fb);
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

// --- DEVELOPMENT_MODE: HTTP kameros peržiūra --------------------------------
// GET /snapshot grazina TIKRA JPEG kadra — atidaryk naršykleje
// (http://<esp32-ip>/snapshot) tame pačiame WiFi, kad matytum, ka kamera
// realiai fiksuoja (kadravimo/atstumo kalibravimui). Naudinga TIK dev metu —
// PASALINTI kartu su DEVELOPMENT_MODE flag'u pries gamybini flash'inima.
//
// PASTABA 2026-09-03: bandytas platesnis "/calibrate" (fotografuoti + is
// karto atpazinti + rodyti viename puslapyje) — PASALINTA. Blokuojantis
// HTTPClient.POST() AsyncWebServer callback'e sukele "task_wdt" reset'a;
// perkelus i loop() (atidetas atsakymas) — nauja klaida, "Guru Meditation
// Error: Core 1 panic'ed (StoreProhibited)" (AsyncWebServerRequest rodykle
// tampa negaliojanti, kol laukia loop() eiles). Per rizikinga pagalbiniam
// kalibravimo irankiui — liko tik paprastas, PATIKIMAI veikiantis /snapshot.
static AsyncWebServer s_webServer(80);

void initWebServer() {
    s_webServer.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest *request) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            request->send(503, "text/plain", "Kameros kadras nepavyko");
            return;
        }
        AsyncWebServerResponse *response = request->beginResponse(
            "image/jpeg", fb->len,
            [fb](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                size_t remain = fb->len - index;
                size_t toCopy = remain < maxLen ? remain : maxLen;
                memcpy(buffer, fb->buf + index, toCopy);
                if (index + toCopy >= fb->len) {
                    esp_camera_fb_return(fb);
                }
                return toCopy;
            });
        request->send(response);
    });

    s_webServer.begin();
    Serial.printf("[WebServer] /snapshot pasiekiamas: http://%s/snapshot\n",
                  WiFi.localIP().toString().c_str());
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

    // server-face-recognition saka: WiFi DABAR BUTINAS kas pabudima (ne tik
    // saltam paleidimui), nes veido atpazinimas vyksta per laptopo serveri
    // (zr. lib/face_recognition/). Tai PANAIKINA anksciau cia buvusi
    // optimizacija (WiFi tik saltam boot'ui) — sitas kompromisas SAMONINGAI
    // priimtas kartu su visa client-server architektura, zr. README
    // "Serveris-pagrindu atpazinimas": pabudimas dabar vel uztruks iki ~15s
    // (WiFi.begin() timeout), o ne buves greitas ext0-wake. NTP sync vis
    // dar praleidziamas pabudus mygtuku — laikrodis islieka RTC domene.
    connectWiFi();
    if (!DeepSleep_WasWokenByButton()) {
        syncTime();
    } else {
        Serial.println("[NTP] Praleista — pabudimas is deep sleep, "
                        "sistemos laikrodis islikes is RTC domeno.");
    }

    initSDCard();
    initCamera();

    initDisplay();
    Touch_FT6336_Init(Wire);

#ifdef DEVELOPMENT_MODE
    // Kadravimo/atstumo kalibravimo pagalba: HTTP /snapshot (zr. initWebServer())
    // vietoj tiesioginio vaizdo pacaime LCD — bandyta LVGL canvas + jpg2rgb565,
    // bet gauta rimta duomenu "plesymo" (tearing) klaida (SPI flush lenktyniauja
    // su buferio perrasymu), be to net teisingai suderintos spalvos butu tik
    // kosmetinis patobulinimas siam pagalbiniam irankiui — PASALINTA, zr. git
    // istorija jei reikes grizti. Naršyklė dekoduoja tikra JPEG teisingai visada.
    initWebServer();
#endif

    FamilyMessages_Init();
    AppStateMachine_Init();

    // v1 "stupid simple": kiekvienas paleidimas (tiek pirmas USB power-on,
    // tiek pabudimas is deep sleep per PWR mygtuka) is karto rodo ekrana —
    // nedarome skirtumo tarp priezasciu, kad logika liktu paprasta.
    AppStateMachine_Update(true);

    // TODO 1: veido atpazinimo modelis (esp-who/ESP-DL) — face_recognition.cpp
    //         dabar tik stub; kamera jau inicijuota ir kadra paduoti gali.
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
    //
    // DEVELOPMENT_MODE (platformio.ini build_flags): deep sleep isjungtas,
    // nes giliame miege native USB CDC dingsta, o COM prievadas issijungia
    // TIKSLIAI tada, kai pio upload bando prisijungti (build+pasiruosimas
    // uztrunka ilgiau nei SCREEN_AWAKE_TIMEOUT_MS). Pasalinti sia vėliavėlę
    // pries gamybini (ne dev) flash'inima.
#ifndef DEVELOPMENT_MODE
    if (AppStateMachine_GetState() == APP_STATE_STANDBY) {
        DeepSleep_EnterSleep();
    }
#endif

    // TODO: cia vėliau bus web serverio handleClient() (jei ne async),
    // garso buferio apdorojimas ir t.t.

    delay(5);
}
