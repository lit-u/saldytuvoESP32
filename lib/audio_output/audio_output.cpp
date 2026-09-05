#include "audio_output.h"
#include "AudioBoard.h"
#include "io_extension.h"
#include <Wire.h>
#include <FS.h>
#include <LittleFS.h>
#include <driver/i2s.h>

using namespace audio_driver;

// I2S pinai PATIKRINTI pagal Waveshare BSP (bsp.h) — zr. audio_output.h.
static const int I2S_PIN_MCLK = 10;
static const int I2S_PIN_BCK = 11;   // BSP_I2S_SCLK
static const int I2S_PIN_WS = 12;    // BSP_I2S_LCLK
static const int I2S_PIN_DOUT = 14;
static const uint32_t AUDIO_SAMPLE_RATE = 16000;
static const i2s_port_t I2S_PORT = I2S_NUM_0;

static bool s_audioReady = false;

static DriverDeviceInfo s_pins;
static AudioBoard s_board(AudioDriverES8311, s_pins);

// PASTABA 2026-09-05: sis framework'o arduino-esp32 paketas turi TIK sena
// (ESP-IDF 4.4 stiliaus) "driver/i2s.h" API — naujesnis "driver/i2s_std.h"
// (kanalais paremtas) SIAME PAKETE NEEGZISTUOJA (patikrinta paieska visame
// framework aplanke). Naudojamas SENAS API — funkcionaliai lygiavertis
// musu TX-only atvejui.
//
// DIAGNOSTIKA 2026-09-05: ankstesne versija (be smulkiu Serial.print
// tarpiniu zymiu) NEPARODYDAVO NEI "paruosta", NEI "KLAIDA" pranesimo
// NEI VIENAME boot'e, nors likusi sistema veike toliau normaliai (ne
// hang, ne crash-loop). Pridetos zymes KIEKVIENAM zingsniui, kad
// tiksliai matytume, kur bloke isvedimas dingsta arba kur realiai klimpsta.
bool Audio_Init() {
    Serial.println("[Audio] Init: pradzia");
    Serial.flush();

    s_pins.addI2C(PinFunction::CODEC, Wire);
    Serial.println("[Audio] Init: addI2C atlikta");
    Serial.flush();

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_NONE;   // mikrofono kodekas NENAUDOJAMAS
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_16K;

    Serial.println("[Audio] Init: pries board.begin (ES8311 I2C)...");
    Serial.flush();
    bool codecOk = s_board.begin(cfg);
    Serial.printf("[Audio] Init: board.begin grazino %s\n", codecOk ? "true" : "false");
    Serial.flush();
    if (!codecOk) {
        Serial.println("[Audio] KLAIDA: ES8311 kodekas neatsake (I2C).");
        Serial.flush();
        return false;
    }
    s_board.setVolume(100);  // 2026-09-05: maksimalus, kol garso apskritai negirdeti
    Serial.println("[Audio] Init: setVolume(100) atlikta");
    Serial.flush();

    // KLAIDA rasta 2026-09-05 (vartotojo pastaba: "paspaudus nieko
    // nesigirdi") — ES8311.h setVoiceMute()/mute() NIEKADA nekviecianas
    // automatiskai begin() metu — daugelis DAC lustu pagal nutylejima
    // uzsileidzia NUTILDYTI (apsauga nuo "pop" triuksmo ijungiant), tad
    // reikia AISKIAI atsileisti garsa.
    s_board.setMute(false);
    Serial.println("[Audio] Init: setMute(false) atlikta");
    Serial.flush();

    // I2S TX (sena API) — biblioteka virs TIK sukonfigūruoja pati kodeka
    // per I2C, PATI I2S duomenu magistrale reikia sukonfigūruoti tiesiogiai.
    i2s_config_t i2sConfig = {};
    i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2sConfig.sample_rate = AUDIO_SAMPLE_RATE;
    i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    // 2026-09-05: pakeista is RIGHT_LEFT (rankiniu budu dubliuotas mono i
    // abu kanalus) i TIKRA mono rezima — ES8311 registrai (es8311.init())
    // sukonfigūruoti PER codec_cfg tikintis MONO slot isdestymo, tad
    // ONLY_LEFT geriau atitinka, negu priverstinis stereo su identiskais
    // kanalais. Bandoma isspresti "nieko nesigirdi" problema.
    i2sConfig.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.dma_buf_count = 4;
    i2sConfig.dma_buf_len = 256;
    i2sConfig.use_apll = false;
    i2sConfig.tx_desc_auto_clear = true;
    // KLAIDA rasta 2026-09-05 (vartotojo pastaba: "nieko nesigirdi", nors
    // I2C init + I2S write viskas raportuoja sekme) — SENA "driver/i2s.h"
    // API NEGENERUOJA MCLK signalo ant mck_io_num pin'o, jei "fixed_mclk"
    // NENURODYTAS (paliktas 0 default). ES8311 initas nustato
    // setMclkSrc(FROM_MCLK_PIN) — kodekas LAUKIA tikro isorinio MCLK savo
    // vidiniam PLL, be jo garsas lieka tylus (PLL neuzsirakina), NORS I2S
    // duomenu linija (BCLK/WS/DATA) veikia visiskai normaliai — TODEL visos
    // ankstesnes patikros (i2s_write sekme ir t.t.) NEPAGAVO sios klaidos.
    // Standartinis MCLK santykis daugeliui kodeku (tarp ju ES8311) — 256x
    // sample rate.
    i2sConfig.fixed_mclk = AUDIO_SAMPLE_RATE * 256;

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2sConfig, 0, nullptr);
    Serial.printf("[Audio] Init: i2s_driver_install = %d (%s)\n", err, esp_err_to_name(err));
    Serial.flush();
    if (err != ESP_OK) {
        Serial.println("[Audio] KLAIDA: i2s_driver_install nepavyko.");
        Serial.flush();
        return false;
    }

    i2s_pin_config_t pinConfig = {};
    pinConfig.mck_io_num = I2S_PIN_MCLK;
    pinConfig.bck_io_num = I2S_PIN_BCK;
    pinConfig.ws_io_num = I2S_PIN_WS;
    pinConfig.data_out_num = I2S_PIN_DOUT;
    pinConfig.data_in_num = I2S_PIN_NO_CHANGE;

    err = i2s_set_pin(I2S_PORT, &pinConfig);
    Serial.printf("[Audio] Init: i2s_set_pin = %d (%s)\n", err, esp_err_to_name(err));
    Serial.flush();
    if (err != ESP_OK) {
        Serial.println("[Audio] KLAIDA: i2s_set_pin nepavyko.");
        Serial.flush();
        return false;
    }

    s_audioReady = true;
    Serial.println("[Audio] Init: ES8311 + I2S PARUOSTA (galutine sekme).");
    Serial.flush();
    return true;
}

// Paprastas 44 baitu WAV antrastes praleidimas — PASITIKIMA, kad failas
// TIKSLIAI atitinka mūsų pacių JS irasytuvo formata (16kHz/16bit/mono PCM),
// nes tai VIENINTELIS saltinis (admin panele), ne bendros paskirties WAV
// grotuvas. Kiekvienas mono sample DUBLIUOJAMAS i L+R (stereo formatas
// suderinamumui — saugesnis pasirinkimas nei tikėtis MONO rezimo veikimo
// su ES8311, dar netestuota sioje plokstej).
bool Audio_PlayFile(const char *path) {
    Serial.printf("[Audio] Play: kviesta su %s (s_audioReady=%s)\n", path, s_audioReady ? "true" : "false");
    Serial.flush();
    if (!s_audioReady) {
        Serial.println("[Audio] Play: PRALEISTA — Audio_Init() nepavyko/nekviestas.");
        Serial.flush();
        return false;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("[Audio] Play: KLAIDA — failo nera: %s\n", path);
        Serial.flush();
        return false;
    }
    size_t fileSize = f.size();
    Serial.printf("[Audio] Play: failas atidarytas, dydis=%u baitu\n", (unsigned)fileSize);
    Serial.flush();
    f.seek(44);  // standartine WAV antraste

    // KRITINE KLAIDA rasta 2026-09-05 (patikrinta TIESIOGIAI oficialiame
    // Waveshare source kode — waveshareteam/ESP32-S3-CAM-OVxxxx,
    // examples/ESP-IDF-v5.5.1/03_audio_play/.../bsp_board_extra.c) —
    // garsiakalbio STIPRINTUVAS (NS4150B) turi ATSKIRA IJUNGIMO pin'a
    // (CH32V003 EXIO P4), NEPRIKLAUSOMA nuo ES8311 I2C/I2S. Be sio, ES8311
    // DAC gali generuoti TEISINGA analogini signala, bet jis niekada
    // nepasiekia garsiakalbio girdimu lygiu — TIKSLIAI atitiko musu
    // simptoma (visi programiniai sluoksniai raportavo sekme, bet garso
    // NEBUVO). Oficialus kodas ijungia PRIES grojant, isjungia po/pries.
    IO_EXTENSION_Output(IO_EXTENSION_AUDIO_PA_PIN, 1);

    // 2026-09-05: TIKRAS mono (I2S_CHANNEL_FMT_ONLY_LEFT) — nebereikia
    // dubliuoti i L+R, monoBuf raso tiesiai.
    const size_t CHUNK_SAMPLES = 256;
    int16_t monoBuf[CHUNK_SAMPLES];
    size_t totalWritten = 0;
    int chunkCount = 0;

    while (f.available()) {
        size_t bytesRead = f.read((uint8_t *)monoBuf, sizeof(monoBuf));
        if (bytesRead == 0) break;
        size_t bytesWritten = 0;
        esp_err_t werr = i2s_write(I2S_PORT, monoBuf, bytesRead, &bytesWritten, portMAX_DELAY);
        totalWritten += bytesWritten;
        chunkCount++;
        if (werr != ESP_OK) {
            Serial.printf("[Audio] Play: i2s_write KLAIDA chunk #%d: %d (%s)\n", chunkCount, werr, esp_err_to_name(werr));
            Serial.flush();
        }
    }
    f.close();
    IO_EXTENSION_Output(IO_EXTENSION_AUDIO_PA_PIN, 0);  // isjungti stiprintuva, kai negrojama
    Serial.printf("[Audio] Play: baigta — %d chunk'u, %u baitu israsyta i I2S\n", chunkCount, (unsigned)totalWritten);
    Serial.flush();
    return true;
}
