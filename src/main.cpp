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

#include <LittleFS.h>
#include <Update.h>

#include "io_extension.h"
#include "lcd_st7796.h"
#include "touch_ft6336.h"
#include "app_state_machine.h"
#include "family_messages.h"
#include "deep_sleep.h"
#include "audio_output.h"

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
// (0x38), ES8311 (0x18) ir ES7210 (0x40) audio kodekai.
static const int I2C_SDA_PIN = 8;
static const int I2C_SCL_PIN = 7;

// --- Balso zinutes irasymas per irenginio mikrofona (admin panele) ---
// 2026-09-05 (vartotojo pastaba: "Pavyko. Bet nutrauke antra sakini. Koks
// limitas?") — pradzioje 6000ms per trumpa dviem sakiniams, padidinta iki
// 12000ms.
static const uint32_t MIC_RECORD_DURATION_MS = 12000;

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

    // Tamsaus kambario derinimas (vartotojo pastaba 2026-09-04: "tamsu
    // kambaryje, neatpazista mane") — numatytoji gain riba per zema, kad
    // sensorius pilnai kompensuotu mazo apsvietimo salygas. Leidziame daugiau
    // automatinio stiprinimo (trioksmas priimtina kaina uz tai, kad veidas
    // apskritai butu matomas).
    //
    // 2026-09-05: /snapshot diagnostika parode, kad realus atvejis NE tik
    // "visur tamsu", o KONTRAVIESA (backlighting) — kambario sviesos saltinis
    // UZ vartotojo nugaros apgauna automatine ekspozicija (kamera taikosi i
    // rysku fona, veidas priesais lieka juodas siluetas). AE_LEVEL i
    // MAKSIMUMA (+2, ne +1) — priverciam sensoriu taikytis i sviesesni
    // VIDURKI, tai pakelia ir tamsu priekini plana (veida), net jei fonas dar
    // labiau perdega. Papildo (ne pakeicia) LCD "blykstes" sprendima (zr.
    // app_state_machine.cpp onWakeSequenceDone) — plokstej nera atskiro
    // kameros LED.
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_gainceiling(sensor, GAINCEILING_16X);
        sensor->set_ae_level(sensor, 2);
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

// --- Admin panele (2026-09-05) ----------------------------------------------
// Seimos zinuciu redagavimas per WiFi, be USB flash'inimo (vartotojo
// pastaba: "gal galima per kita wifi?" — taip, ESP32 jau turi savo web
// serveri). Zinutes issaugomos i NVS (zr. family_messages.cpp), tad islieka
// po perkrovimo/deep sleep. TYCIA VISADA veikianti (NE tik DEVELOPMENT_MODE)
// — tai reali produkto funkcija, ne dev irankis (zr. initWebServer()
// struktura zemiau — TIK /snapshot lieka DEVELOPMENT_MODE-only).
static String escapeHtml(const String &s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            default:   out += c; break;
        }
    }
    return out;
}

// 2026-09-05 (vartotojo pastaba: "dar pagalvokime del adminkes dizaino")
// — akcentine spalva kiekvienam zmogui (vien tik admin puslapio CSS,
// NESUSIJE su LVGL p.themeAccent — tas yra lv_color_t, cia paprasciau
// turėti atskira, tiesiog hex, paletę web puslapiui).
static const char *ADMIN_ACCENT_COLORS[PERSON_COUNT] = {
    "#7f8c8d",  // PERSON_UNKNOWN — nenaudojama (ciklas prasideda nuo 1)
    "#e67e22",  // GRANDDAUGHTER_1
    "#16a085",  // GRANDDAUGHTER_2
    "#2980b9",  // SON
    "#c0392b",  // WIFE
    "#8e44ad",  // SELF
};

static String buildAdminPage() {
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>Saldytuvo adminke</title>"
            "<style>"
            "*{box-sizing:border-box;}"
            "body{font-family:-apple-system,'Segoe UI',sans-serif;max-width:480px;margin:0 auto;"
            "padding:12px;background:#eef1f4;color:#222;}"
            "header{display:flex;align-items:center;gap:10px;margin-bottom:12px;}"
            ".eyes{width:56px;height:30px;position:relative;flex-shrink:0;}"
            ".brow{width:20px;height:4px;background:#2C3E50;border-radius:2px;position:absolute;top:0;}"
            ".brow.l{left:1px;transform:rotate(-10deg);}"
            ".brow.r{right:1px;transform:rotate(10deg);}"
            ".eye{width:20px;height:20px;background:#2C3E50;border-radius:50%;position:absolute;"
            "top:9px;animation:blink 4s infinite;}"
            ".eye.l{left:1px;}"
            ".eye.r{right:1px;}"
            "@keyframes blink{0%,90%,100%{transform:scaleY(1);}95%{transform:scaleY(0.12);}}"
            "h1{font-size:18px;margin:0;color:#2C3E50;}"
            ".tabs{display:flex;gap:4px;margin-bottom:10px;}"
            ".tab{flex:1;text-align:center;padding:9px 4px;border-radius:9px 9px 0 0;"
            "background:#dde3e9;color:#7a8593;font-weight:600;cursor:pointer;font-size:14px;"
            "user-select:none;}"
            ".tab.active{background:#fff;color:#2C3E50;}"
            ".tab.disabled{cursor:default;opacity:.55;}"
            ".panel{display:none;}"
            ".panel.active{display:block;}"
            ".card{background:#fff;border-radius:10px;padding:12px 14px;margin-bottom:10px;"
            "border-left-width:5px;border-left-style:solid;"
            "box-shadow:0 1px 2px rgba(0,0,0,.06);}"
            ".card h3{margin:0 0 6px;font-size:15px;}"
            ".card label{font-size:11px;color:#8a8a8a;display:block;margin:7px 0 2px;"
            "text-transform:uppercase;letter-spacing:.03em;}"
            "textarea{width:100%;font-size:14px;font-family:inherit;border-radius:6px;"
            "border:1px solid #d5d9dd;padding:6px 8px;resize:vertical;}"
            "button{padding:7px 12px;font-size:13px;border:none;border-radius:7px;"
            "background:#2C3E50;color:#fff;cursor:pointer;}"
            "button:disabled{opacity:.4;cursor:default;}"
            "button.save{margin-top:5px;}"
            ".status{font-size:12px;color:#666;margin-left:6px;}"
            ".placeholder{color:#9aa4ad;text-align:center;padding:50px 10px;font-size:14px;}"
            "</style></head><body>"
            "<header>"
            "<div class='eyes'><div class='brow l'></div><div class='brow r'></div>"
            "<div class='eye l'></div><div class='eye r'></div></div>"
            "<h1>Šaldytuvo terminalas</h1>"
            "</header>"
            "<div class='tabs'>"
            "<div class='tab active' id='tab-fridge' onclick=\"showTab('fridge')\">🧊 Šaldytuvas</div>"
            "<div class='tab disabled' id='tab-home'>🏠 Namai</div>"
            "</div>"
            "<div class='panel active' id='panel-fridge'>";

    for (int i = 1; i < PERSON_COUNT; i++) {
        RecognizedPerson person = (RecognizedPerson)i;
        const PersonProfile &p = FamilyProfiles_Get(person);
        const FamilyMessage &pubMsg = FamilyMessages_Get(person, MessageKind::PUBLIC);
        const FamilyMessage &privMsg = FamilyMessages_Get(person, MessageKind::PRIVATE);
        const char *accent = ADMIN_ACCENT_COLORS[i];
        String pid = String((int)person);

        html += "<div class='card' style='border-left-color:" + String(accent) + "'>";
        html += "<h3 style='color:" + String(accent) + "'>" + escapeHtml(String(p.publicName)) + "</h3>";

        html += "<form method='POST' action='/admin/message'>";
        html += "<input type='hidden' name='person' value='" + pid + "'>";
        html += "<input type='hidden' name='kind' value='public'>";
        html += "<label>Vieša žinutė (mato visi, kas pasirenka šį vardą)</label>";
        html += "<textarea name='text' rows='2' placeholder='Nėra'>";
        if (pubMsg.hasMessage) html += escapeHtml(String(pubMsg.text));
        html += "</textarea>";
        html += "<button class='save' type='submit'>Išsaugoti</button>";
        html += "</form>";

        // 2026-09-05 (vartotojo pastaba: "galime padaryti slapta skyriu,
        // kuri matys tiktai tas, kuri atpazins... reikia lenteleje dar
        // vieno stulpelio - labai asmeninems zinutems").
        html += "<form method='POST' action='/admin/message'>";
        html += "<input type='hidden' name='person' value='" + pid + "'>";
        html += "<input type='hidden' name='kind' value='private'>";
        html += "<label>🔒 Slapta žinutė (tik kai kamera TIKRAI atpažins)</label>";
        html += "<textarea name='text' rows='2' placeholder='Nėra'>";
        if (privMsg.hasMessage) html += escapeHtml(String(privMsg.text));
        html += "</textarea>";
        html += "<button class='save' type='submit'>Išsaugoti</button>";
        html += "</form>";

        // Balso žinutė — 2026-09-05: keliauta per keleta variantu (narsykles
        // mikrofonas su getUserMedia, failo ikelimas su decodeAudioData),
        // bet visi turejo naršykles/OS suderinamumo apribojimu (Safari
        // nera apejimo http:// atveju). GALUTINIS sprendimas (vartotojo
        // pastaba: "super, bet isimk ir windows ir Mac ir failo pasirinkima")
        // — TIK irasymas TIESIOGIAI per irenginio ES7210 mikrofona.
        html += "<label>Balso žinutė (" + String((int)(MIC_RECORD_DURATION_MS / 1000)) + "s prie šaldytuvo)</label>";
        html += "<button type='button' id='rec-mic-" + pid +
                "' onclick='recordViaDevice(" + pid + ")'>🎤 Įrašyti</button> ";
        html += "<button type='button' id='rec-mic-stop-" + pid +
                "' onclick='stopDeviceRecording(" + pid + ")' disabled>⏹ Stop</button>";
        html += "<span id='rec-status-" + pid + "' class='status'></span>";
        html += "</div>";
    }

    html += "</div>"  // #panel-fridge
            "<div class='panel' id='panel-home'>"
            "<p class='placeholder'>Netrukus — viso buto įrenginių valdymas.</p>"
            "</div>"
            // 2026-09-05 (vartotojo pastaba: "man reikia kitos [adminkes]")
            // — nedidele, neiskyla nuoroda i savininko puslapi (slaptazodis
            // ten, ne cia).
            "<p style='text-align:center;margin-top:16px;'>"
            "<a href='/admin/owner' style='color:#aaa;font-size:12px;'>🔧 Savininkui</a></p>";

    // JS irasymui per irenginio ES7210 mikrofona — POST /admin/record_mic
    // uzduoda irasyma (loop() viduje, zr. RequestMicRecording()), tada
    // poll'inam /admin/record_status kas 300ms, kol busena taps 'done'.
    // POST /admin/record_stop leidzia baigti anksciau (vartotojo pastaba:
    // "1 sek sustoja, nera stop. Gal padaryti?").
    html += "<script>"
            // 2026-09-05 (vartotojo pastaba: "reikia dar vieno tab - Namai,
            // kol kas tuscia, ar neaktyvu") — "Namai" tab'as saMoningai
            // neaktyvus (nera onclick), kol nera ka jame rodyti.
            "function showTab(name){"
            "document.getElementById('panel-fridge').classList.toggle('active',name==='fridge');"
            "document.getElementById('tab-fridge').classList.toggle('active',name==='fridge');"
            "}"
            "async function recordViaDevice(p){"
            "let remaining=" + String((int)(MIC_RECORD_DURATION_MS / 1000)) + ";"
            "document.getElementById('rec-mic-'+p).disabled=true;"
            "document.getElementById('rec-mic-stop-'+p).disabled=false;"
            "document.getElementById('rec-status-'+p).textContent='Kalbėk! Liko '+remaining+'s';"
            "let countdown=setInterval(()=>{"
            "remaining--;"
            "if(remaining>0)document.getElementById('rec-status-'+p).textContent='Kalbėk! Liko '+remaining+'s';"
            "},1000);"
            "await fetch('/admin/record_mic?person='+p,{method:'POST'});"
            "for(let tries=0;tries<60;tries++){"
            "await new Promise(r=>setTimeout(r,300));"
            "let r=await fetch('/admin/record_status?person='+p);"
            "let j=await r.json();"
            "if(j.state==='done'||j.state==='error'){"
            "clearInterval(countdown);"
            "document.getElementById('rec-mic-'+p).disabled=false;"
            "document.getElementById('rec-mic-stop-'+p).disabled=true;"
            "document.getElementById('rec-status-'+p).textContent=j.state==='done'?'Įrašyta!':'Klaida';"
            "return;"
            "}"
            "}"
            "clearInterval(countdown);"
            "document.getElementById('rec-mic-'+p).disabled=false;"
            "document.getElementById('rec-mic-stop-'+p).disabled=true;"
            "document.getElementById('rec-status-'+p).textContent='Per ilgai laukta...';"
            "}"
            "async function stopDeviceRecording(p){"
            "await fetch('/admin/record_stop?person='+p,{method:'POST'});"
            "}"
            "</script>";

    html += "</body></html>";
    return html;
}

void RequestTestSoundPlayback(int personIdx);  // apibrezta zemiau, prie loop()
void RequestMicRecording(int personIdx);       // apibrezta zemiau, prie loop()
void RequestMicRecordStop();                   // apibrezta zemiau, prie loop()
const char *MicRecordGetStateStr();            // apibrezta zemiau, prie loop()

// 2026-09-05 (vartotojo pastaba: "ok, pasw: OldBoy") — adminke dabar
// pasiekiama IR per Tailscale is bet kur (zr. README naujausia sesija),
// tad reikalingas bent paprastas HTTP Basic Auth. Grazina true, jei
// autentifikuotas; jei ne, PATI issiuncia 401 atsakyma (naršykle parodys
// prisijungimo langa) — kviecianti funkcija tada TURI iskart grizti.
static bool checkAdminAuth(AsyncWebServerRequest *request) {
    if (request->authenticate(SECRET_ADMIN_USER, SECRET_ADMIN_PASSWORD)) return true;
    request->requestAuthentication();
    return false;
}

// 2026-09-05 (vartotojo pastaba: "juk turi buti dvi adminkes... man reikia
// kitos" + "svarbu ne tik parasyti zinutes... o valdyti esp-32 flashinima")
// — SAVININKO puslapis (slaptazodis, zr. checkAdminAuth): sistemos bukle,
// kameros/atpazinimo nustatymai, IR nuotolinis firmware atnaujinimas (OTA) —
// TIKSLAS: valdyti visa saldytuvo projekta net busiant 15km atstumu (zr.
// README, Tailscale sesija), be USB kabelio.
static String formatUptime(uint32_t ms) {
    uint32_t totalSec = ms / 1000;
    uint32_t days = totalSec / 86400;
    uint32_t hours = (totalSec % 86400) / 3600;
    uint32_t mins = (totalSec % 3600) / 60;
    uint32_t secs = totalSec % 60;
    char buf[48];
    if (days > 0) {
        snprintf(buf, sizeof(buf), "%ud %uh %um %us", days, hours, mins, secs);
    } else if (hours > 0) {
        snprintf(buf, sizeof(buf), "%uh %um %us", hours, mins, secs);
    } else {
        snprintf(buf, sizeof(buf), "%um %us", mins, secs);
    }
    return String(buf);
}

static String buildOwnerPage() {
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>Savininko adminke</title>"
            "<style>"
            "*{box-sizing:border-box;}"
            "body{font-family:-apple-system,'Segoe UI',sans-serif;max-width:480px;margin:0 auto;"
            "padding:12px;background:#1c1f26;color:#e4e6eb;}"
            "h1{font-size:18px;margin:0 0 12px;color:#f0a500;}"
            "a{color:#6cb6ff;}"
            ".card{background:#262b36;border-radius:10px;padding:12px 14px;margin-bottom:12px;"
            "border-left:5px solid #f0a500;}"
            ".card h3{margin:0 0 8px;font-size:15px;color:#f0a500;}"
            ".row{display:flex;justify-content:space-between;font-size:13px;padding:3px 0;"
            "border-bottom:1px solid #333;}"
            ".row span:last-child{color:#9fd88a;font-family:monospace;}"
            "label{font-size:12px;color:#9aa4ad;display:block;margin:8px 0 3px;}"
            "input[type=number],input[type=file]{width:100%;font-size:14px;font-family:inherit;"
            "border-radius:6px;border:1px solid #444;padding:6px 8px;background:#1c1f26;color:#e4e6eb;}"
            "button{padding:8px 14px;font-size:13px;border:none;border-radius:7px;"
            "background:#f0a500;color:#1c1f26;font-weight:600;cursor:pointer;margin-top:8px;}"
            "button:disabled{opacity:.4;cursor:default;}"
            "button.danger{background:#e74c3c;color:#fff;}"
            ".status{font-size:12px;color:#9aa4ad;margin-top:6px;}"
            "progress{width:100%;margin-top:8px;}"
            "</style></head><body>"
            "<h1>🔧 Savininko adminke</h1>"
            "<p><a href='/admin'>&larr; Šeimos adminkė</a></p>";

    html += "<div class='card'><h3>Sistemos būklė</h3>";
    html += "<div class='row'><span>Veikimo laikas</span><span>" + formatUptime(millis()) + "</span></div>";
    html += "<div class='row'><span>WiFi signalas</span><span>" + String(WiFi.RSSI()) + " dBm</span></div>";
    html += "<div class='row'><span>Laisva RAM</span><span>" + String(ESP.getFreeHeap() / 1024) + " KB</span></div>";
    html += "<div class='row'><span>LittleFS naudojama</span><span>" +
            String(LittleFS.usedBytes() / 1024) + " / " + String(LittleFS.totalBytes() / 1024) + " KB</span></div>";
    html += "<div class='row'><span>Firmware versija</span><span>" + String(__DATE__) + " " + String(__TIME__) + "</span></div>";
    html += "</div>";

    sensor_t *sensor = esp_camera_sensor_get();
    html += "<div class='card'><h3>Kameros nustatymai</h3>";
    html += "<form method='POST' action='/admin/owner/camera'>";
    html += "<label>AE lygis (-2 .. 2, didesnis = šviesiau)</label>";
    html += "<input type='number' name='ae_level' min='-2' max='2' value='" +
            String(sensor ? sensor->status.ae_level : 2) + "'>";
    html += "<label>Gain ceiling (0-6, didesnis = daugiau triukšmo, bet šviesiau)</label>";
    html += "<input type='number' name='gainceiling' min='0' max='6' value='" +
            String(sensor ? sensor->status.gainceiling : 4) + "'>";
    html += "<button type='submit'>Pritaikyti</button>";
    html += "</form>";
    html += "<p style='margin-top:10px;'><a href='/admin/owner/snapshot' target='_blank'>📷 Peržiūrėti dabartinį kameros kadrą</a></p>";
    html += "</div>";

    // 2026-09-05 (vartotojo pastaba: "svarbu... valdyti esp-32 flashinima")
    // — OTA (Over-The-Air) firmware atnaujinimas: ikeliamas naujas .bin
    // (PlatformIO "Build" veiksmo rezultatas, .pio/build/esp32-s3-cam/
    // firmware.bin), ESP32 israso i NEAKTYVIA OTA partiicija (app0/app1,
    // jau numatytos default_16MB.csv), po sekmingo israsymo persikrauna i
    // ja. NEBEREIKIA USB kabelio/fizinio priejimo prie irenginio.
    html += "<div class='card'><h3>⚠️ Firmware atnaujinimas (OTA)</h3>";
    html += "<p style='font-size:13px;color:#9aa4ad;'>Ikelk .pio/build/esp32-s3-cam/firmware.bin "
            "(po 'pio run' kompiliavimo). Irenginys persikraus automatiskai po sekmingo irasymo.</p>";
    html += "<input type='file' id='ota-file' accept='.bin'>";
    html += "<button type='button' id='ota-btn' onclick='uploadOta()'>⬆️ Įkelti ir flash'inti</button>";
    html += "<progress id='ota-progress' value='0' max='100' style='display:none;'></progress>";
    html += "<div class='status' id='ota-status'></div>";
    html += "</div>";

    html += "<script>"
            "function uploadOta(){"
            "let f=document.getElementById('ota-file').files[0];"
            "if(!f){alert('Pasirink .bin faila');return;}"
            "let btn=document.getElementById('ota-btn');"
            "let prog=document.getElementById('ota-progress');"
            "let status=document.getElementById('ota-status');"
            "btn.disabled=true;prog.style.display='block';prog.value=0;"
            "status.textContent='Siunčiama...';"
            "let xhr=new XMLHttpRequest();"
            "xhr.open('POST','/admin/owner/ota');"
            // KRITINE KLAIDA rasta 2026-09-05 testuojant per curl — jei
            // Content-Type NEBUNA aiskiai 'application/octet-stream',
            // AsyncWebServer bando .bin faila skaityti kaip
            // "application/x-www-form-urlencoded" forma (simbolis po
            // simbolio), kas UZSTRIGDO IRENGINI VISISKAI (be watchdog
            // gaudymo, reikejo fizinio reset). Naršyklė .bin failui gali
            // NESUSTATYTI jokio Content-Type (tuscias), todel BUTINA
            // nurodyti aiskiai, o ne pasitiketi numatytuoju elgesiu.
            "xhr.setRequestHeader('Content-Type','application/octet-stream');"
            "xhr.upload.onprogress=function(e){"
            "if(e.lengthComputable){prog.value=Math.round(e.loaded/e.total*100);}"
            "};"
            "xhr.onload=function(){"
            "if(xhr.status===200){"
            "status.textContent='Sėkmė! Įrenginys persikrauna su nauja versija (~10s)...';"
            "}else{"
            "status.textContent='Klaida: '+xhr.responseText;"
            "btn.disabled=false;"
            "}"
            "};"
            "xhr.onerror=function(){status.textContent='Klaida siunčiant (tikriausiai jau persikrauna)';};"
            "xhr.send(f);"
            "}"
            "</script>";

    html += "</body></html>";
    return html;
}

void initWebServer() {
#ifdef DEVELOPMENT_MODE
    // Kadravimo/atstumo kalibravimo pagalba — TIK dev metu, zr. platesne
    // pastaba virs (PASALINTI su DEVELOPMENT_MODE flag'u pries gamybini
    // flash'inima).
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

    // Garso derinimo pagalba (2026-09-05) — leidzia paleisti bet kuri
    // "/audio_N.wav" faila TIESIOGIAI per curl, be poreikio liesti fizini
    // ekrana kiekvienam bandymui. TIK dev metu, PASALINTI kartu su likusiu
    // DEVELOPMENT_MODE blocku.
    s_webServer.on("/testsound", HTTP_GET, [](AsyncWebServerRequest *request) {
        int personIdx = request->hasParam("person") ? request->getParam("person")->value().toInt() : 5;
        // 2026-09-05: NEBEKVIEcIA Audio_PlayFile() cia tiesiogiai (as_tcp
        // uzduoties task_wdt crash ilgesniems failams) — tik pazymi, o
        // TIKRAS grojimas vyksta loop() viduje, zr. RequestTestSoundPlayback().
        RequestTestSoundPlayback(personIdx);
        request->send(200, "text/plain", "OK (grojama fone)");
    });
#endif

    // 2026-09-05 (vartotojo pastaba: "man reikia kitos [adminkes]" +
    // "svarbu... valdyti esp-32 flashinima") — SAVININKO puslapis, TIK per
    // checkAdminAuth() (slaptazodis, zr. secrets.h). Zr. buildOwnerPage()
    // pastaba virs del viso konteksto.
    //
    // TVARKA SVARBI: plikas "/admin/owner" (GET) registruojamas ZEMIAU,
    // PO "/admin/owner/snapshot" (GET) — priesingu atveju "/admin/owner"
    // prarytu tos pacios metodo GET uzklausas i "/admin/owner/snapshot"
    // (zr. platesne pastaba prie plikojo "/admin" initWebServer() gale).
    s_webServer.on("/admin/owner/camera", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAdminAuth(request)) return;
        sensor_t *sensor = esp_camera_sensor_get();
        if (sensor) {
            if (request->hasParam("ae_level", true)) {
                sensor->set_ae_level(sensor, request->getParam("ae_level", true)->value().toInt());
            }
            if (request->hasParam("gainceiling", true)) {
                sensor->set_gainceiling(sensor, (gainceiling_t)request->getParam("gainceiling", true)->value().toInt());
            }
        }
        request->redirect("/admin/owner");
    });

    // Nuotolinis kadro patikrinimas — TAS PATS principas kaip dev-only
    // /snapshot virs, bet PASTOVIAI prieinamas (uz slaptazodzio), nes
    // savininkui naudinga per Tailscale patikrinti, ka kamera mato, be
    // fizinio priejimo.
    s_webServer.on("/admin/owner/snapshot", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAdminAuth(request)) return;
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            request->send(503, "text/plain", "Kameros kadras nepavyko");
            return;
        }
        AsyncWebServerResponse *response = request->beginResponse(
            "image/jpeg", fb->len,
            [fb](uint8_t *buffer, size_t maxLen, size_t alreadySent) -> size_t {
                size_t toCopy = fb->len - alreadySent;
                if (toCopy > maxLen) toCopy = maxLen;
                memcpy(buffer, fb->buf + alreadySent, toCopy);
                if (alreadySent + toCopy >= fb->len) esp_camera_fb_return(fb);
                return toCopy;
            });
        request->send(response);
    });

    s_webServer.on("/admin/owner", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAdminAuth(request)) return;
        request->send(200, "text/html; charset=utf-8", buildOwnerPage());
    });

    // 2026-09-05 (vartotojo pastaba: "svarbu... valdyti esp-32 flashinima")
    // — OTA firmware ikelimas, RAW binarinis POST body (.bin failas
    // tiesiogiai, ne multipart/form-data). Update.h biblioteka israso i
    // NEAKTYVIA OTA partiicija (app0/app1, jau numatytos default_16MB.csv),
    // Update.end(true) patikrina vientisuma ir pazymi ja kaip paleidziama.
    // s_otaAuthorized (static, per-callback) apsaugo, kad neautentifikuotas
    // POST negalėtu net PRADETI rasyti i flash.
    s_webServer.on(
        "/admin/owner/ota", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!checkAdminAuth(request)) return;
            bool ok = !Update.hasError();
            Serial.printf("[OTA] Baigimo handleris: ok=%s\n", ok ? "true" : "false");
            Serial.flush();
            AsyncWebServerResponse *response = request->beginResponse(
                ok ? 200 : 500, "text/plain", ok ? "OK" : Update.errorString());
            response->addHeader("Connection", "close");
            request->send(response);
            if (ok) {
                // 2026-09-05: restart per atskira Task su nedideliu delay,
                // kad HTTP atsakymas spetu tikrai issisiusti klientui pries
                // persikraunant.
                xTaskCreate(
                    [](void *) {
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        ESP.restart();
                    },
                    "otaRestart", 2048, nullptr, 1, nullptr);
            }
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static bool s_otaAuthorized = false;
            static uint32_t s_otaLastLogMs = 0;
            if (index == 0) {
                s_otaAuthorized = checkAdminAuth(request);
                if (!s_otaAuthorized) return;
                Serial.printf("[OTA] Pradedama, dydis=%u baitu, laisva RAM=%u\n",
                              (unsigned)total, (unsigned)ESP.getFreeHeap());
                Serial.flush();
                if (!Update.begin(total)) {
                    Serial.printf("[OTA] KLAIDA: Update.begin(): %s\n", Update.errorString());
                    Serial.flush();
                    s_otaAuthorized = false;
                    return;
                }
                s_otaLastLogMs = millis();
            }
            if (!s_otaAuthorized) return;
            uint32_t writeStartMs = millis();
            size_t written = Update.write(data, len);
            uint32_t writeMs = millis() - writeStartMs;
            if (written != len) {
                Serial.printf("[OTA] KLAIDA rasant (%u/%u baitu, %u ms): %s\n",
                              (unsigned)written, (unsigned)len, (unsigned)writeMs, Update.errorString());
                Serial.flush();
            } else if (millis() - s_otaLastLogMs > 1000) {
                s_otaLastLogMs = millis();
                Serial.printf("[OTA] progresas: %u/%u baitu (%.0f%%), sis chunk'as %u ms, laisva RAM=%u\n",
                              (unsigned)(index + len), (unsigned)total,
                              100.0f * (index + len) / total, (unsigned)writeMs,
                              (unsigned)ESP.getFreeHeap());
                Serial.flush();
            }
            if (index + len == total) {
                if (Update.end(true)) {
                    Serial.println("[OTA] Sekmingai israsyta.");
                } else {
                    Serial.printf("[OTA] KLAIDA baigiant: %s\n", Update.errorString());
                }
                Serial.flush();
            }
        });

    // Balso zinutes ikelimas — RAW binarinis POST body (narsykles JS siuncia
    // WAV baitus tiesiogiai, ne multipart/form-data), issaugoma LittleFS
    // "/audio/<person>.wav". Statinis s_uploadFile — sitas admin irankis
    // vienu metu naudojamas VIENO zmogaus (seimos adminke), lygiagretumas
    // NEREIKALINGAS.
    s_webServer.on(
        "/admin/audio", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            request->send(200, "text/plain", "OK");
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static File s_uploadFile;
            if (index == 0) {
                int personIdx = request->hasParam("person") ? request->getParam("person")->value().toInt() : 0;
                if (personIdx <= 0 || personIdx >= PERSON_COUNT) {
                    Serial.println("[Audio] /admin/audio: neteisingas 'person' parametras.");
                    return;
                }
                // PASTABA 2026-09-05: "/audio/<n>.wav" (pakatalogyje) NEPAVYKO —
                // LittleFS.mkdir("/audio") tyliai neveike ("no permits for
                // creation"), o LittleFS.open() i neegzistuojanti katalogo
                // kelia irgi tyliai nepavykdavo (be klaidos kodo grazinimo).
                // FIX: failai saugomi PLOKSCIAI saknyje ("/audio_N.wav"),
                // be jokio pakatalogio — pasalina visa priezasti.
                String path = "/audio_" + String(personIdx) + ".wav";
                s_uploadFile = LittleFS.open(path, "w");
                Serial.printf("[Audio] Irasoma: %s\n", path.c_str());
            }
            if (s_uploadFile) {
                s_uploadFile.write(data, len);
            }
            if (index + len == total) {
                if (s_uploadFile) {
                    s_uploadFile.close();
                    Serial.printf("[Audio] Ikelta, %u baitu.\n", (unsigned)total);
                }
            }
        });

    // 2026-09-05 (vartotojo pastaba: "darom is adminkes garso irasyma per
    // esp-32 mikra") — irasymas per irenginio ES7210 mikrofona. NEBEKVIECIA
    // Audio_RecordToFile() cia tiesiogiai (as_tcp uzduoties task_wdt crash
    // rizika ilgesniam blokavimui, ta pati priezastis kaip /testsound) —
    // tik pazymi, TIKRAS irasymas vyksta loop() viduje.
    s_webServer.on("/admin/record_mic", HTTP_POST, [](AsyncWebServerRequest *request) {
        int personIdx = request->hasParam("person") ? request->getParam("person")->value().toInt() : -1;
        if (personIdx <= 0 || personIdx >= PERSON_COUNT) {
            request->send(400, "text/plain", "KLAIDA: neteisingas person");
            return;
        }
        RequestMicRecording(personIdx);
        request->send(200, "text/plain", "OK (irasoma fone)");
    });

    s_webServer.on("/admin/record_status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = String("{\"state\":\"") + MicRecordGetStateStr() + "\"}";
        request->send(200, "application/json", json);
    });

    // 2026-09-05 (vartotojo pastaba: "1 sek sustoja, nera stop. Gal
    // padaryti?") — leidzia baigti irasyma anksciau nei
    // MIC_RECORD_DURATION_MS. Veikia net kai loop() uzimtas irasinejant,
    // nes AsyncWebServer callback'ai vykdomi ATSKIROJE (as_tcp) uzduotyje.
    s_webServer.on("/admin/record_stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        RequestMicRecordStop();
        request->send(200, "text/plain", "OK");
    });

    // 2026-09-05 (vartotojo pastaba: "galime padaryti slapta skyriu...
    // reikia lenteleje dar vieno stulpelio") — "kind" parametras ("public"
    // arba "private") atskiria admin formos siusta zinute, zr.
    // family_messages.h MessageKind.
    s_webServer.on("/admin/message", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("person", true) && request->hasParam("text", true)) {
            int personIdx = request->getParam("person", true)->value().toInt();
            String text = request->getParam("text", true)->value();
            text.trim();
            String kindStr = request->hasParam("kind", true) ? request->getParam("kind", true)->value() : "public";
            MessageKind kind = (kindStr == "private") ? MessageKind::PRIVATE : MessageKind::PUBLIC;
            if (personIdx > 0 && personIdx < PERSON_COUNT) {
                if (text.length() == 0) {
                    FamilyMessages_Clear((RecognizedPerson)personIdx, kind);
                } else {
                    FamilyMessages_Set((RecognizedPerson)personIdx, kind, text.c_str());
                }
            }
        }
        request->redirect("/admin");
    });

    // KRITINE KLAIDA rasta 2026-09-05, kuriant /admin/owner — AsyncWebServer
    // NEATLIEKA tikslaus URL atitikimo: handler->canHandle() grazina true
    // NE TIK jei _uri==url, bet IR jei url.startsWith(_uri+"/") (zr.
    // WebHandlerImpl.h). Pirmas UZREGISTRUOTAS handler'is, kuris "canHandle",
    // LAIMI (WebServer.cpp _attachHandler). Todel plikas "/admin" (GET)
    // PRARYDAVO VISUS veliau uzregistruotus GET maршrutus po juo su tuo
    // paciu keliu prefiksu — /admin/owner, /admin/owner/snapshot IR
    // (jau SENIAI, tyliai) /admin/record_status! (POST marsrutai issivenge
    // sios klaidos vien todel, kad /admin registruotas TIK HTTP_GET, o
    // metodo neatitikimas atmeta canHandle() ANKSCIAU nei URI patikra.)
    // FIX: "/admin" (bendriausias GET kelias) registruojamas PASKUTINIS —
    // visi specifiskesni /admin/* GET marsrutai jau uzregistruoti ANKSCIAU
    // ir laimi pirmumo tvarka.
    s_webServer.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html; charset=utf-8", buildAdminPage());
    });

    s_webServer.begin();
    Serial.printf("[WebServer] Adminke pasiekiama: http://%s/admin\n",
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

    FamilyMessages_Init();

    // 2026-09-05: "Garsas" mygtuko funkcijai — balso zinutes issaugomos
    // LittleFS (SD kortele neveikianti, zr. initSDCard() klaida virs).
    // `true` = suformatuoti, jei pirma karta/klaida (saugu — TIK sis
    // failu saugykla, atskira nuo NVS kur laikomos tekstines zinutes).
    if (!LittleFS.begin(true)) {
        Serial.println("[LittleFS] KLAIDA: nepavyko inicijuoti/formatuoti.");
    }
    // ES8311 (garsiakalbis, atkurimas) + ES7210 (mikrofonas, irasymas per
    // admin panele "Irasyti prie saldytuvo" mygtuka). Klaida cia NESUSTABDO
    // likusios sistemos — garsas tik papildoma funkcija, ne kritinis kelias.
    Audio_Init();

    // 2026-09-05: KVIECIAMA VISADA (ne tik DEVELOPMENT_MODE) — adminke
    // (http://<ip>/admin, seimos zinuciu redagavimas) yra reali produkto
    // funkcija, ne dev irankis. Tik /snapshot (kadravimo pagalba) lieka
    // uzrakintas uz DEVELOPMENT_MODE viduje pacios initWebServer() (zr.
    // funkcijos apibrezima) — PASALINTI kartu su flag'u pries gamybini
    // flash'inima.
    initWebServer();

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

// Realus "pazadinimo" mygtukas (vartotojo pastaba 2026-09-05: "dabar Reset
// nieko mums neduoda, sutvarkom, kad viskas vyktu kaip is tikruju") — TAS
// PATS fizinis PWR_BUTTON_PIN (IO15, jau sumontuotas korpuse, zr.
// deep_sleep.h), kuris DEVELOPMENT_MODE metu (deep sleep isjungtas) NIEKADA
// nebuvo skaitomas awake loop() metu — nuspaudimas tiesiog nieko nedare.
// Krastas (HIGH->LOW), ne lygis, kad ilgas laikymas nesukeltu pakartotinio
// trigerio kas loop() iteracija.
static bool readWakeButtonEdge() {
    static bool s_wasPressed = false;
    bool isPressed = (digitalRead(PWR_BUTTON_PIN) == LOW);
    bool edge = isPressed && !s_wasPressed;
    s_wasPressed = isPressed;
    return edge;
}

// KLAIDA rasta 2026-09-05 — /testsound (curl diagnostika) kviete
// Audio_PlayFile() TIESIOGIAI is AsyncWebServer callback'o, t.y. as_tcp
// uzduoties kontekste. Ilgesniam (~16s) narsykleje irasytam failui
// blokuojantis i2s_write ciklas neleido tai uzduociai atstatyti savo task
// watchdog laiku -> "task_wdt... Aborting." -> irenginys persikrove.
// Fizinis "Garsas" mygtukas (ui_screens.cpp) sios problemos NETURI, nes
// kviecia Audio_PlayFile() is pagrindinio loop() uzduoties (be task_wdt
// registracijos ten). FIX: /testsound tik PAZYMI norima grojima, o TIKRAS
// Audio_PlayFile() kvietimas vyksta cia, loop() viduje.
static volatile int s_pendingTestSoundPerson = -1;

void RequestTestSoundPlayback(int personIdx) {
    s_pendingTestSoundPerson = personIdx;
}

// 2026-09-05 (vartotojo pastaba: "darom is adminkes garso irasyma per
// esp-32 mikra") — ta pati "atidek i loop()" schema kaip
// RequestTestSoundPlayback virs (Audio_RecordToFile() irgi blokuoja kelias
// sekundes, negalima kviesti is AsyncWebServer as_tcp uzduoties). Busena
// (idle/recording/done/error) leidzia admin puslapio JS poll'inti progresa.
enum class MicRecordState { IDLE, RECORDING, DONE, ERROR };
static volatile MicRecordState s_micRecordState = MicRecordState::IDLE;
static volatile int s_pendingMicRecordPerson = -1;

// 2026-09-05 (vartotojo pastaba: "Veikia, 1 sek sustoja, nera stop. Gal
// padaryti?") — leidzia adminkes "Stop" mygtukui baigti irasyma anksciau
// nei MIC_RECORD_DURATION_MS (zr. Audio_RecordToFile() stopRequested parametra).
static volatile bool s_micRecordStopRequested = false;

void RequestMicRecording(int personIdx) {
    s_micRecordState = MicRecordState::RECORDING;
    s_micRecordStopRequested = false;
    s_pendingMicRecordPerson = personIdx;
}

void RequestMicRecordStop() {
    s_micRecordStopRequested = true;
}

const char *MicRecordGetStateStr() {
    switch (s_micRecordState) {
        case MicRecordState::RECORDING: return "recording";
        case MicRecordState::DONE: return "done";
        case MicRecordState::ERROR: return "error";
        default: return "idle";
    }
}

void loop() {
    if (s_pendingTestSoundPerson >= 0) {
        int personIdx = s_pendingTestSoundPerson;
        s_pendingTestSoundPerson = -1;
        char path[32];
        snprintf(path, sizeof(path), "/audio_%d.wav", personIdx);
        Audio_PlayFile(path);
    }
    if (s_pendingMicRecordPerson >= 0) {
        int personIdx = s_pendingMicRecordPerson;
        s_pendingMicRecordPerson = -1;
        char path[32];
        snprintf(path, sizeof(path), "/audio_%d.wav", personIdx);
        bool ok = Audio_RecordToFile(path, MIC_RECORD_DURATION_MS, &s_micRecordStopRequested);
        s_micRecordState = ok ? MicRecordState::DONE : MicRecordState::ERROR;
    }
    // Radaras pasalintas — v1 nebeturi atskiro "judesio" jutiklio; realus
    // pazadinimo saltinis dabar — fizinis PWR mygtukas (zr. readWakeButtonEdge
    // virsuje). AppStateMachine_Update() pati ignoruoja sia reiksme, jei jau
    // ne STANDBY busenoje (zr. app_state_machine.cpp).
    bool wakeButtonPressed = readWakeButtonEdge();
    AppStateMachine_Update(wakeButtonPressed);
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
