#include "deep_sleep.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

void DeepSleep_Init() {
    pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);

    if (DeepSleep_WasWokenByButton()) {
        Serial.println("[DeepSleep] Pazadinta PWR mygtuku (IO15 / ext0).");
    } else {
        Serial.println("[DeepSleep] Normalus paleidimas (ne is deep sleep).");
    }
}

bool DeepSleep_WasWokenByButton() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
}

void DeepSleep_EnterSleep() {
    Serial.println("[DeepSleep] Uzmiegama. Pazadins PWR mygtukas (IO15).");
    Serial.flush();

    gpio_num_t pin = (gpio_num_t)PWR_BUTTON_PIN;
    // RTC pull-up butinas — po deep sleep GPIO pull nustatymai (pinMode)
    // is Arduino puses neislieka, RTC domenas turi savo atskira valdyma.
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
    esp_sleep_enable_ext0_wakeup(pin, 0 /* pabusti kai LOW — mygtukas nuspaustas */);

    esp_deep_sleep_start();
    // Sitas taskas niekada nepasiekiamas — pabudus MCU startuoja is naujo
    // (setup() vykdomas nuo pradzios, RAM turinys prarastas, RTC islieka).
}
