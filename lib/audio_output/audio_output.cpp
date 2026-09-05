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
static const int I2S_PIN_DIN = 13;   // BSP_I2S_DSIN — ES7210 mikrofonas
static const uint32_t AUDIO_SAMPLE_RATE = 16000;
static const i2s_port_t I2S_PORT = I2S_NUM_0;

static bool s_audioReady = false;

static DriverDeviceInfo s_pins;
// 2026-09-05 (vartotojo pastaba: "darom is adminkes garso irasyma per
// esp-32 mikra") — pereita nuo vien-ES8311 (tik atkurimas) i kombinuota
// ES8311+ES7210 draiveri (biblioteka JAU turi sita kombinacija paruosta,
// zr. AudioDriver.h "AudioDriverES8311_ES7210" — naudojama tiksliai to
// paties tipo plokstese kaip ESP32S3AISmartSpeaker pavyzdys).
static AudioBoard s_board(AudioDriverES8311_ES7210, s_pins);

// 2026-09-05 (vartotojo pastaba: "PCM tas pats wav, rask kita lengvesni
// formata") — IMA ADPCM koderis/dekoderis (standartinis algoritmas, 4
// bitai/samplui). Naudojamas ABIEM kryptimis: dekodavimui grojant (failas
// atkeliavo is narsykles JS encodeIma()) IR kodavimui irasant per ES7210
// (Audio_RecordToFile). Vienas tesinis srautas visam failui (be blokiniu
// antrasciu kas N samplu) — paprastesnis, nes tai VISISKAI privatus,
// tik musu paciu koduotojo/dekoderio formatas.
static const int16_t IMA_STEP_TABLE[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,
    17,    19,    21,    23,    25,    28,    31,    34,    37,
    41,    45,    50,    55,    60,    66,    73,    80,    88,
    97,    107,   118,   130,   143,   157,   173,   190,   209,
    230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,
    1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,
    3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_INDEX_TABLE[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                            -1, -1, -1, -1, 2, 4, 6, 8};

static inline int16_t imaDecodeNibble(uint8_t nibble, int32_t &predictor, int8_t &index) {
    int step = IMA_STEP_TABLE[index];
    int diff = step >> 3;
    if (nibble & 4) diff += step;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 8) predictor -= diff; else predictor += diff;
    if (predictor > 32767) predictor = 32767;
    if (predictor < -32768) predictor = -32768;
    index += IMA_INDEX_TABLE[nibble];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    return (int16_t)predictor;
}

// Koduoja viena 16-bit sample i 4-bit nibble (0-15), atnaujindamas
// predictor/index busena — atvirkscias imaDecodeNibble veiksmas, TIKSLIAI
// atitinka main.cpp JS encodeIma() logika (kad ESP32 irasyti failai butu
// grojami TOKIU PAT dekoderiu kaip narsykles irasyti failai).
static inline uint8_t imaEncodeSample(int16_t sample, int32_t &predictor, int8_t &index) {
    int32_t diff = (int32_t)sample - predictor;
    int sign = 0;
    if (diff < 0) { sign = 8; diff = -diff; }
    int step = IMA_STEP_TABLE[index];
    int delta = 0;
    int vpdiff = step >> 3;
    if (diff >= step) { delta |= 4; diff -= step; vpdiff += step; }
    step >>= 1;
    if (diff >= step) { delta |= 2; diff -= step; vpdiff += step; }
    step >>= 1;
    if (diff >= step) { delta |= 1; vpdiff += step; }
    if (sign) predictor -= vpdiff; else predictor += vpdiff;
    if (predictor > 32767) predictor = 32767;
    if (predictor < -32768) predictor = -32768;
    uint8_t code = (uint8_t)(delta | sign);
    index += IMA_INDEX_TABLE[code];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    return code;
}

// PASTABA 2026-09-05: sis framework'o arduino-esp32 paketas turi TIK sena
// (ESP-IDF 4.4 stiliaus) "driver/i2s.h" API — naujesnis "driver/i2s_std.h"
// (kanalais paremtas) SIAME PAKETE NEEGZISTUOJA (patikrinta paieska visame
// framework aplanke). Naudojamas SENAS API.
//
// KLAIDA rasta 2026-09-05, pridedant ES7210 mikrofona: ES7210
// (setMicsForChannels()) palaiko TIK CHANNELS2 arba CHANNELS4, niekada
// mono — o legacy i2s_config_t.channel_format yra VIENAS bendras
// nustatymas VISAM I2S periferijos kadrui (TX ir RX kartu). Todel cia
// PRIVALOME naudoti I2S_CHANNEL_FMT_RIGHT_LEFT (2 kanalai) VISADA — TX
// pusej (Audio_PlayFile) mono samplas duplikuojamas i L+R, RX pusej
// (Audio_RecordToFile) paimamas tik KAIRYSIS kanalas (MIC1).
bool Audio_Init() {
    Serial.println("[Audio] Init: pradzia");
    Serial.flush();

    s_pins.addI2C(PinFunction::CODEC, Wire);
    Serial.println("[Audio] Init: addI2C atlikta");
    Serial.flush();

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;  // ES7210 MIC1 kanalas ijungtas
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_16K;
    // cfg.i2s.channels lieka default CHANNELS2 — ES7210 to reikalauja
    // (zr. setMicsForChannels() klaidos pastaba virs).

    Serial.println("[Audio] Init: pries board.begin (ES8311+ES7210 I2C)...");
    Serial.flush();
    bool codecOk = s_board.begin(cfg);
    Serial.printf("[Audio] Init: board.begin grazino %s\n", codecOk ? "true" : "false");
    Serial.flush();
    if (!codecOk) {
        Serial.println("[Audio] KLAIDA: kodekas(-ai) neatsake (I2C).");
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

    // I2S TX+RX (sena API) — biblioteka virs TIK sukonfigūruoja pacius
    // kodekus per I2C, PATI I2S duomenu magistrale reikia sukonfigūruoti
    // tiesiogiai.
    i2s_config_t i2sConfig = {};
    i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
    i2sConfig.sample_rate = AUDIO_SAMPLE_RATE;
    i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    // 2026-09-05: RIGHT_LEFT (2 kanalai), NE ONLY_LEFT — but tin ES7210
    // reikalavimo (zr. pastaba virs funkcijos). TX pusej duplikuojame
    // mono i abu kanalus.
    i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.dma_buf_count = 4;
    i2sConfig.dma_buf_len = 256;
    i2sConfig.use_apll = false;
    i2sConfig.tx_desc_auto_clear = true;
    // KLAIDA rasta 2026-09-05 (vartotojo pastaba: "nieko nesigirdi", nors
    // I2C init + I2S write viskas raportuoja sekme) — SENA "driver/i2s.h"
    // API NEGENERUOJA MCLK signalo ant mck_io_num pin'o, jei "fixed_mclk"
    // NENURODYTAS (paliktas 0 default). Standartinis MCLK santykis
    // daugeliui kodeku (tarp ju ES8311/ES7210) — 256x sample rate.
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
    pinConfig.data_in_num = I2S_PIN_DIN;

    err = i2s_set_pin(I2S_PORT, &pinConfig);
    Serial.printf("[Audio] Init: i2s_set_pin = %d (%s)\n", err, esp_err_to_name(err));
    Serial.flush();
    if (err != ESP_OK) {
        Serial.println("[Audio] KLAIDA: i2s_set_pin nepavyko.");
        Serial.flush();
        return false;
    }

    s_audioReady = true;
    Serial.println("[Audio] Init: ES8311+ES7210 + I2S PARUOSTA (galutine sekme).");
    Serial.flush();
    return true;
}

// Rasomas/skaitomas WAV-panasus antgalvis (44 baitai) — TIK musu paciu
// formatas (IMA ADPCM, ne standartinis WAV), bet laikomas RIFF/WAVE
// struktura nuoseklumo delei tarp narsykles JS ir ESP32 kodo.
static void writeAdpcmHeader(File &f, uint32_t dataBytes) {
    f.seek(0);
    uint32_t chunkSize = 36 + dataBytes;
    uint32_t fmtChunkSize = 16;
    uint16_t fmtTag = 17;  // "IMA ADPCM" zymeklis (musu paciu konvencija)
    uint16_t numCh = 1;
    uint32_t sampleRate = 16000;
    uint32_t byteRate = 8000;
    uint16_t blockAlign = 1;
    uint16_t bitsPerSample = 4;
    f.write((const uint8_t *)"RIFF", 4);
    f.write((const uint8_t *)&chunkSize, 4);
    f.write((const uint8_t *)"WAVE", 4);
    f.write((const uint8_t *)"fmt ", 4);
    f.write((const uint8_t *)&fmtChunkSize, 4);
    f.write((const uint8_t *)&fmtTag, 2);
    f.write((const uint8_t *)&numCh, 2);
    f.write((const uint8_t *)&sampleRate, 4);
    f.write((const uint8_t *)&byteRate, 4);
    f.write((const uint8_t *)&blockAlign, 2);
    f.write((const uint8_t *)&bitsPerSample, 2);
    f.write((const uint8_t *)"data", 4);
    f.write((const uint8_t *)&dataBytes, 4);
}

// Paprastas 44 baitu WAV antrastes praleidimas — PASITIKIMA, kad failas
// TIKSLIAI atitinka musu pacia IMA ADPCM formata (JS encodeIma() arba
// Audio_RecordToFile() ESP32 puseje), nes tai VIENINTELIAI saltiniai, ne
// bendros paskirties WAV grotuvas.
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
    f.seek(44);  // musu WAV-panasi antraste

    // KRITINE KLAIDA rasta 2026-09-05 (patikrinta TIESIOGIAI oficialiame
    // Waveshare source kode) — garsiakalbio STIPRINTUVAS (NS4150B) turi
    // ATSKIRA IJUNGIMO pin'a (CH32V003 EXIO P4), NEPRIKLAUSOMA nuo ES8311
    // I2C/I2S. Be sio garsas negirdimas, nors DAC generuoja teisinga
    // signala.
    IO_EXTENSION_Output(IO_EXTENSION_AUDIO_PA_PIN, 1);

    // 2026-09-05: kiekvienas failo baitas = 2 IMA ADPCM nibble = 2 16-bit
    // samplai, DUPLIKUOJAMI i L+R (zr. pastaba virs Audio_Init() del
    // I2S_CHANNEL_FMT_RIGHT_LEFT reikalavimo).
    const size_t CHUNK_BYTES = 256;
    uint8_t rawBuf[CHUNK_BYTES];
    int16_t expandBuf[CHUNK_BYTES * 2 * 2];  // 2 samplai/baite * 2 (L+R)
    size_t totalWritten = 0;
    int chunkCount = 0;
    int32_t predictor = 0;
    int8_t index = 0;
    // 2026-09-05: diagnostika irasytiems balso pranesimams — patikrinti, ar
    // faile apskritai YRA signalas (ne tyla).
    int16_t peakAbs = 0;

    while (f.available()) {
        size_t bytesRead = f.read(rawBuf, sizeof(rawBuf));
        if (bytesRead == 0) break;
        size_t outPos = 0;
        for (size_t i = 0; i < bytesRead; i++) {
            uint8_t byteVal = rawBuf[i];
            int16_t s0 = imaDecodeNibble(byteVal & 0x0F, predictor, index);
            int16_t s1 = imaDecodeNibble((byteVal >> 4) & 0x0F, predictor, index);
            expandBuf[outPos++] = s0;  // L
            expandBuf[outPos++] = s0;  // R (duplikuota)
            expandBuf[outPos++] = s1;  // L
            expandBuf[outPos++] = s1;  // R (duplikuota)
            int16_t a0 = (s0 < 0) ? -s0 : s0;
            int16_t a1 = (s1 < 0) ? -s1 : s1;
            if (a0 > peakAbs) peakAbs = a0;
            if (a1 > peakAbs) peakAbs = a1;
        }
        size_t bytesToWrite = outPos * sizeof(int16_t);
        size_t bytesWritten = 0;
        esp_err_t werr = i2s_write(I2S_PORT, expandBuf, bytesToWrite, &bytesWritten, portMAX_DELAY);
        totalWritten += bytesWritten;
        chunkCount++;
        if (werr != ESP_OK) {
            Serial.printf("[Audio] Play: i2s_write KLAIDA chunk #%d: %d (%s)\n", chunkCount, werr, esp_err_to_name(werr));
            Serial.flush();
        }
    }
    f.close();
    IO_EXTENSION_Output(IO_EXTENSION_AUDIO_PA_PIN, 0);  // isjungti stiprintuva, kai negrojama
    Serial.printf("[Audio] Play: baigta — %d chunk'u, %u baitu israsyta i I2S, pikas=%d/32767 (%.1f%%)\n",
                  chunkCount, (unsigned)totalWritten, peakAbs, 100.0f * peakAbs / 32767.0f);
    Serial.flush();
    return true;
}

// 2026-09-05 (vartotojo pastaba: "darom is adminkes garso irasyma per
// esp-32 mikra", "vis vien butu reikeje, nes norisi ir vaikams palikti
// galimybe irasyti") — irasymas TIESIOGIAI per irenginio ES7210
// mikrofona, alia narsykles ikelimo kelio (abu veikia lygiagreciai).
bool Audio_RecordToFile(const char *path, uint32_t durationMs, volatile bool *stopRequested) {
    Serial.printf("[Audio] Record: kviesta su %s, %u ms (s_audioReady=%s)\n",
                  path, (unsigned)durationMs, s_audioReady ? "true" : "false");
    Serial.flush();
    if (!s_audioReady) {
        Serial.println("[Audio] Record: PRALEISTA — Audio_Init() nepavyko/nekviestas.");
        Serial.flush();
        return false;
    }
    File f = LittleFS.open(path, "w");
    if (!f) {
        Serial.printf("[Audio] Record: KLAIDA — nepavyko sukurti failo: %s\n", path);
        Serial.flush();
        return false;
    }

    uint8_t placeholderHeader[44] = {0};
    f.write(placeholderHeader, sizeof(placeholderHeader));

    // 2026-09-05: I2S RX buferio "senos" duomenys (is prieš tai buvusio
    // grojimo ar tuscios busenos) isvalomi, kad irasymo pradzioje
    // neatsirastu triuksmo/artefaktu is DMA buferio liekanu.
    i2s_zero_dma_buffer(I2S_PORT);

    const size_t CHUNK_FRAMES = 256;  // stereo kadrai (L+R) per nuskaityma
    int16_t rxBuf[CHUNK_FRAMES * 2];
    uint8_t outBuf[CHUNK_FRAMES];  // 2 samplai/baite -> max CHUNK_FRAMES/2, su marža
    int32_t predictor = 0;
    int8_t index = 0;
    bool haveNib = false;
    uint8_t pendingNib = 0;
    size_t totalEncodedBytes = 0;
    int32_t peakAbs = 0;

    uint32_t startMs = millis();
    while (millis() - startMs < durationMs) {
        if (stopRequested && *stopRequested) {
            Serial.println("[Audio] Record: sustabdyta anksciau (Stop mygtukas).");
            break;
        }
        size_t bytesRead = 0;
        esp_err_t rerr = i2s_read(I2S_PORT, rxBuf, sizeof(rxBuf), &bytesRead, portMAX_DELAY);
        if (rerr != ESP_OK) {
            Serial.printf("[Audio] Record: i2s_read KLAIDA: %d (%s)\n", rerr, esp_err_to_name(rerr));
            Serial.flush();
            break;
        }
        size_t frames = bytesRead / (2 * sizeof(int16_t));  // L+R pora
        size_t outPos = 0;
        for (size_t i = 0; i < frames; i++) {
            int16_t micSample = rxBuf[i * 2];  // kairysis kanalas = MIC1
            int32_t a = (micSample < 0) ? -micSample : micSample;
            if (a > peakAbs) peakAbs = a;
            uint8_t code = imaEncodeSample(micSample, predictor, index);
            if (!haveNib) {
                pendingNib = code;
                haveNib = true;
            } else {
                outBuf[outPos++] = pendingNib | (code << 4);
                haveNib = false;
            }
        }
        if (outPos > 0) {
            f.write(outBuf, outPos);
            totalEncodedBytes += outPos;
        }
    }
    if (haveNib) {
        uint8_t lastByte = pendingNib;
        f.write(&lastByte, 1);
        totalEncodedBytes++;
    }

    writeAdpcmHeader(f, totalEncodedBytes);
    f.close();
    Serial.printf("[Audio] Record: baigta — %u baitu (ADPCM), pikas=%d/32767 (%.1f%%)\n",
                  (unsigned)totalEncodedBytes, (int)peakAbs, 100.0f * peakAbs / 32767.0f);
    Serial.flush();
    return true;
}
