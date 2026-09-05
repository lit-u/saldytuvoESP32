#include "family_messages.h"
#include <string.h>
#include <Preferences.h>

// 2026-09-05: admin panele (main.cpp POST /admin/message) leidzia keisti
// zinutes per WiFi be USB flash'inimo — TURI islikti po perkrovimo/deep
// sleep, tad NVS (Preferences), ne vien RAM. Raktai "mpub0".."mpub5" ir
// "mpriv0".."mpriv5" (NVS raktai riboti iki 15 simboliu — saugu).
static Preferences s_prefs;
static FamilyMessage s_publicMessages[PERSON_COUNT];
static FamilyMessage s_privateMessages[PERSON_COUNT];

static FamilyMessage *arrayFor(MessageKind kind) {
    return (kind == MessageKind::PUBLIC) ? s_publicMessages : s_privateMessages;
}

static String prefKey(RecognizedPerson target, MessageKind kind) {
    const char *prefix = (kind == MessageKind::PUBLIC) ? "mpub" : "mpriv";
    return String(prefix) + (int)target;
}

void FamilyMessages_Init() {
    memset(s_publicMessages, 0, sizeof(s_publicMessages));
    memset(s_privateMessages, 0, sizeof(s_privateMessages));
    s_prefs.begin("fammsg", false);

    // 2026-09-05: MIGRACIJA — senas vieno-lauko formatas ("m0".."m5", zr.
    // git istorija) semantiskai atitiko dabartine PRIVATE (rodyta TIK
    // realiam atpazinimui) — perkeliama, kad vartotojo jau irasytos
    // zinutes (pvz. "Nusipirk alaus ir zvenk laimingas") neprapultu.
    for (int i = 1; i < PERSON_COUNT; i++) {
        String oldKey = String("m") + i;
        String oldVal = s_prefs.getString(oldKey.c_str(), "");
        if (oldVal.length() > 0) {
            String newKey = prefKey((RecognizedPerson)i, MessageKind::PRIVATE);
            if (s_prefs.getString(newKey.c_str(), "").length() == 0) {
                s_prefs.putString(newKey.c_str(), oldVal);
            }
            s_prefs.remove(oldKey.c_str());
        }
    }

    for (int kindIdx = 0; kindIdx < 2; kindIdx++) {
        MessageKind kind = (kindIdx == 0) ? MessageKind::PUBLIC : MessageKind::PRIVATE;
        FamilyMessage *arr = arrayFor(kind);
        for (int i = 1; i < PERSON_COUNT; i++) {
            String saved = s_prefs.getString(prefKey((RecognizedPerson)i, kind).c_str(), "");
            if (saved.length() > 0) {
                strncpy(arr[i].text, saved.c_str(), FAMILY_MESSAGE_MAX_LEN - 1);
                arr[i].text[FAMILY_MESSAGE_MAX_LEN - 1] = '\0';
                arr[i].hasMessage = true;
            }
        }
    }
}

void FamilyMessages_Set(RecognizedPerson target, MessageKind kind, const char *text) {
    if (target < 0 || target >= PERSON_COUNT || text == nullptr) return;
    FamilyMessage *arr = arrayFor(kind);
    strncpy(arr[target].text, text, FAMILY_MESSAGE_MAX_LEN - 1);
    arr[target].text[FAMILY_MESSAGE_MAX_LEN - 1] = '\0';
    arr[target].hasMessage = true;
    arr[target].setAtMillis = millis();
    s_prefs.putString(prefKey(target, kind).c_str(), arr[target].text);
}

const FamilyMessage &FamilyMessages_Get(RecognizedPerson target, MessageKind kind) {
    if (target < 0 || target >= PERSON_COUNT) target = PERSON_UNKNOWN;
    return arrayFor(kind)[target];
}

void FamilyMessages_Clear(RecognizedPerson target, MessageKind kind) {
    if (target < 0 || target >= PERSON_COUNT) return;
    FamilyMessage *arr = arrayFor(kind);
    arr[target].hasMessage = false;
    arr[target].text[0] = '\0';
    s_prefs.remove(prefKey(target, kind).c_str());
}
