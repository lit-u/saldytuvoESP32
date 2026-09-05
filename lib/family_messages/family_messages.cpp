#include "family_messages.h"
#include <string.h>
#include <Preferences.h>

// 2026-09-05: admin panele (main.cpp POST /admin/message) leidzia keisti
// zinutes per WiFi be USB flash'inimo — TURI islikti po perkrovimo/deep
// sleep, tad NVS (Preferences), ne vien RAM. Raktai "m0".."m5" (NVS raktai
// riboti iki 15 simboliu, PERSON_COUNT << 100, tad saugu).
static Preferences s_prefs;
static FamilyMessage s_messages[PERSON_COUNT];

static String prefKey(RecognizedPerson target) {
    return String("m") + (int)target;
}

void FamilyMessages_Init() {
    memset(s_messages, 0, sizeof(s_messages));
    s_prefs.begin("fammsg", false);
    for (int i = 1; i < PERSON_COUNT; i++) {
        String saved = s_prefs.getString(prefKey((RecognizedPerson)i).c_str(), "");
        if (saved.length() > 0) {
            strncpy(s_messages[i].text, saved.c_str(), FAMILY_MESSAGE_MAX_LEN - 1);
            s_messages[i].text[FAMILY_MESSAGE_MAX_LEN - 1] = '\0';
            s_messages[i].hasMessage = true;
        }
    }
}

void FamilyMessages_Set(RecognizedPerson target, const char *text) {
    if (target < 0 || target >= PERSON_COUNT || text == nullptr) return;
    strncpy(s_messages[target].text, text, FAMILY_MESSAGE_MAX_LEN - 1);
    s_messages[target].text[FAMILY_MESSAGE_MAX_LEN - 1] = '\0';
    s_messages[target].hasMessage = true;
    s_messages[target].setAtMillis = millis();
    s_prefs.putString(prefKey(target).c_str(), s_messages[target].text);
}

const FamilyMessage &FamilyMessages_Get(RecognizedPerson target) {
    if (target < 0 || target >= PERSON_COUNT) target = PERSON_UNKNOWN;
    return s_messages[target];
}

void FamilyMessages_Clear(RecognizedPerson target) {
    if (target < 0 || target >= PERSON_COUNT) return;
    s_messages[target].hasMessage = false;
    s_messages[target].text[0] = '\0';
    s_prefs.remove(prefKey(target).c_str());
}
