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

static String buildAdminPage() {
    String html;
    html.reserve(2048);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>Saldytuvo adminke</title>"
            "<style>body{font-family:sans-serif;max-width:480px;margin:20px auto;"
            "padding:0 16px;background:#f5f5f5;}"
            "h2{color:#2C3E50;}"
            "fieldset{margin-bottom:20px;border-radius:10px;border:1px solid #ccc;"
            "background:#fff;padding:14px;}"
            "legend{font-weight:bold;padding:0 6px;}"
            "textarea{width:100%;box-sizing:border-box;font-size:16px;font-family:inherit;"
            "border-radius:6px;border:1px solid #ccc;padding:8px;}"
            "button{padding:10px 20px;font-size:16px;border:none;border-radius:6px;"
            "background:#2C3E50;color:#fff;cursor:pointer;}"
            "button:active{background:#1a2530;}</style></head><body>"
            "<h2>Šeimos žinutės</h2>"
            "<p>Parašyk žinutę, kurią pamatys tas žmogus, kai dėžutė jį atpažins "
            "(arba pats save pasirinks meniu).</p>";

    for (int i = 1; i < PERSON_COUNT; i++) {
        RecognizedPerson person = (RecognizedPerson)i;
        const PersonProfile &p = FamilyProfiles_Get(person);
        const FamilyMessage &msg = FamilyMessages_Get(person);
        html += "<form method='POST' action='/admin/message'>";
        html += "<fieldset><legend>" + escapeHtml(String(p.publicName)) + "</legend>";
        html += "<input type='hidden' name='person' value='" + String((int)person) + "'>";
        html += "<textarea name='text' rows='3' placeholder='Žinutės nėra'>";
        if (msg.hasMessage) html += escapeHtml(String(msg.text));
        html += "</textarea><br><br>";
        html += "<button type='submit'>Išsaugoti</button></fieldset>";
        html += "</form>";

        // Balso žinutė (2026-09-05, vartotojo pastaba: "irasymas vyksta
        // html, mums reikia tik atkurimo") — irasoma NARSYKLEJE (Web Audio
        // API), NE ESP32 mikrofonu (ES7210 nenaudojamas). Paspaudus
        // "Garsas" prietaise, ESP32 atkuria SI faila per ES8311.
        html += "<fieldset><legend>" + escapeHtml(String(p.publicName)) + " — balso žinutė</legend>";
        html += "<button type='button' id='rec-start-" + String((int)person) +
                "' onclick='startRec(" + String((int)person) + ")'>🎤 Įrašyti</button> ";
        html += "<button type='button' id='rec-stop-" + String((int)person) +
                "' onclick='stopRec(" + String((int)person) + ")' disabled>⏹ Stop</button> ";
        html += "<span id='rec-status-" + String((int)person) + "'></span>";
        html += "</fieldset>";
    }

    // Bendra JS visiems irasymo mygtukams — irasoma narsykles mikrofonu,
    // downsample'inama i 16kHz/16bit/mono WAV (ta patį formatą, kuri
    // Audio_PlayFile() tikisi, zr. lib/audio_output/), siunciama tiesiai
    // kaip binarinis POST body (ne multipart/form-data — paprasciau ESP32
    // pusei, zr. initWebServer() /admin/audio onBody).
    html += "<script>"
            "let _actx,_src,_proc,_chunks,_stream;"
            "async function startRec(p){"
            "_chunks=[];"
            "_stream=await navigator.mediaDevices.getUserMedia({audio:true});"
            "_actx=new(window.AudioContext||window.webkitAudioContext)();"
            "_src=_actx.createMediaStreamSource(_stream);"
            "_proc=_actx.createScriptProcessor(4096,1,1);"
            "_proc.onaudioprocess=function(e){_chunks.push(new Float32Array(e.inputBuffer.getChannelData(0)));};"
            "_src.connect(_proc);_proc.connect(_actx.destination);"
            "document.getElementById('rec-start-'+p).disabled=true;"
            "document.getElementById('rec-stop-'+p).disabled=false;"
            "document.getElementById('rec-status-'+p).textContent='Įrašoma...';"
            "}"
            "function stopRec(p){"
            "_proc.disconnect();_src.disconnect();"
            "_stream.getTracks().forEach(t=>t.stop());"
            "let total=0;_chunks.forEach(a=>total+=a.length);"
            "let merged=new Float32Array(total),off=0;"
            "_chunks.forEach(a=>{merged.set(a,off);off+=a.length;});"
            "let ratio=_actx.sampleRate/16000;"
            "let newLen=Math.floor(merged.length/ratio);"
            "let rs=new Float32Array(newLen);"
            "for(let i=0;i<newLen;i++)rs[i]=merged[Math.floor(i*ratio)];"
            "let buf=new ArrayBuffer(44+rs.length*2),v=new DataView(buf);"
            "function ws(o,s){for(let i=0;i<s.length;i++)v.setUint8(o+i,s.charCodeAt(i));}"
            "ws(0,'RIFF');v.setUint32(4,36+rs.length*2,true);ws(8,'WAVE');ws(12,'fmt ');"
            "v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,1,true);"
            "v.setUint32(24,16000,true);v.setUint32(28,32000,true);"
            "v.setUint16(32,2,true);v.setUint16(34,16,true);ws(36,'data');"
            "v.setUint32(40,rs.length*2,true);"
            "let o=44;for(let i=0;i<rs.length;i++){let s=Math.max(-1,Math.min(1,rs[i]));"
            "v.setInt16(o,s<0?s*0x8000:s*0x7FFF,true);o+=2;}"
            "document.getElementById('rec-status-'+p).textContent='Siunčiama...';"
            "fetch('/admin/audio?person='+p,{method:'POST',body:buf})"
            ".then(r=>{document.getElementById('rec-status-'+p).textContent=r.ok?'Išsaugota!':'Klaida';})"
            ".catch(()=>{document.getElementById('rec-status-'+p).textContent='Klaida';});"
            "document.getElementById('rec-start-'+p).disabled=false;"
            "document.getElementById('rec-stop-'+p).disabled=true;"
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
        char path[32];
        snprintf(path, sizeof(path), "/audio_%d.wav", personIdx);
        bool ok = Audio_PlayFile(path);
        request->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "KLAIDA");
    });
#endif

    s_webServer.on("/admin", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html; charset=utf-8", buildAdminPage());
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

    s_webServer.on("/admin/message", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("person", true) && request->hasParam("text", true)) {
            int personIdx = request->getParam("person", true)->value().toInt();
            String text = request->getParam("text", true)->value();
            text.trim();
            if (personIdx > 0 && personIdx < PERSON_COUNT) {
                if (text.length() == 0) {
                    FamilyMessages_Clear((RecognizedPerson)personIdx);
                } else {
                    FamilyMessages_Set((RecognizedPerson)personIdx, text.c_str());
                }
            }
        }
        request->redirect("/admin");
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
    // ES8311 (garsiakalbis) — TIK atkurimas, mikrofono kodekas (ES7210)
    // nenaudojamas (irasymas vyksta narsykleje, zr. main.cpp /admin puslapio
    // JS). Klaida cia NESUSTABDO likusios sistemos — garsas tik papildoma
    // funkcija, ne kritinis kelias.
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

void loop() {
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
