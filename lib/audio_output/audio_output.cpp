#include "audio_output.h"
#include "AudioBoard.h"
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
bool Audio_Init() {
    // ES8311 I2C valdymas — PAKARTOTINAI naudoja JAU veikiancia bendra Wire
    // magistrale (IO7/IO8, inicijuota main.cpp Wire.begin() PRIES sia
    // funkcija), NE atskira/nauja I2C initas. Adresas (0x18) hardcodintas
    // pacio ES8311 draiverio viduje (zr. audio_output.h komentara).
    s_pins.addI2C(PinFunction::CODEC, Wire);

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_NONE;   // mikrofono kodekas NENAUDOJAMAS
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_16K;
    if (!s_board.begin(cfg)) {
        Serial.println("[Audio] KLAIDA: ES8311 kodekas neatsake (I2C).");
        return false;
    }
    s_board.setVolume(80);

    // I2S TX (sena API) — biblioteka virs TIK sukonfigūruoja pati kodeka
    // per I2C, PATI I2S duomenu magistrale reikia sukonfigūruoti tiesiogiai.
    i2s_config_t i2sConfig = {};
    i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2sConfig.sample_rate = AUDIO_SAMPLE_RATE;
    i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;  // stereo (mono dubliuojamas L+R)
    i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.dma_buf_count = 4;
    i2sConfig.dma_buf_len = 256;
    i2sConfig.use_apll = false;
    i2sConfig.tx_desc_auto_clear = true;

    if (i2s_driver_install(I2S_PORT, &i2sConfig, 0, nullptr) != ESP_OK) {
        Serial.println("[Audio] KLAIDA: i2s_driver_install nepavyko.");
        return false;
    }

    i2s_pin_config_t pinConfig = {};
    pinConfig.mck_io_num = I2S_PIN_MCLK;
    pinConfig.bck_io_num = I2S_PIN_BCK;
    pinConfig.ws_io_num = I2S_PIN_WS;
    pinConfig.data_out_num = I2S_PIN_DOUT;
    pinConfig.data_in_num = I2S_PIN_NO_CHANGE;

    if (i2s_set_pin(I2S_PORT, &pinConfig) != ESP_OK) {
        Serial.println("[Audio] KLAIDA: i2s_set_pin nepavyko.");
        return false;
    }

    s_audioReady = true;
    Serial.println("[Audio] ES8311 + I2S paruosta.");
    return true;
}

// Paprastas 44 baitu WAV antrastes praleidimas — PASITIKIMA, kad failas
// TIKSLIAI atitinka mūsų pacių JS irasytuvo formata (16kHz/16bit/mono PCM),
// nes tai VIENINTELIS saltinis (admin panele), ne bendros paskirties WAV
// grotuvas. Kiekvienas mono sample DUBLIUOJAMAS i L+R (stereo formatas
// suderinamumui — saugesnis pasirinkimas nei tikėtis MONO rezimo veikimo
// su ES8311, dar netestuota sioje plokstej).
bool Audio_PlayFile(const char *path) {
    if (!s_audioReady) {
        Serial.println("[Audio] Praleista — Audio_Init() nepavyko/nekviestas.");
        return false;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("[Audio] Failo nera: %s\n", path);
        return false;
    }
    f.seek(44);  // standartine WAV antraste

    const size_t CHUNK_SAMPLES = 256;
    int16_t monoBuf[CHUNK_SAMPLES];
    int16_t stereoBuf[CHUNK_SAMPLES * 2];

    while (f.available()) {
        size_t bytesRead = f.read((uint8_t *)monoBuf, sizeof(monoBuf));
        size_t samplesRead = bytesRead / sizeof(int16_t);
        if (samplesRead == 0) break;
        for (size_t i = 0; i < samplesRead; i++) {
            stereoBuf[i * 2] = monoBuf[i];
            stereoBuf[i * 2 + 1] = monoBuf[i];
        }
        size_t bytesWritten = 0;
        i2s_write(I2S_PORT, stereoBuf, samplesRead * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    }
    f.close();
    return true;
}
