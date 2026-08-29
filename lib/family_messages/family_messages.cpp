#include "family_messages.h"
#include <string.h>

static FamilyMessage s_messages[PERSON_COUNT];

void FamilyMessages_Init() {
    memset(s_messages, 0, sizeof(s_messages));
}

void FamilyMessages_Set(RecognizedPerson target, const char *text) {
    if (target < 0 || target >= PERSON_COUNT || text == nullptr) return;
    strncpy(s_messages[target].text, text, FAMILY_MESSAGE_MAX_LEN - 1);
    s_messages[target].text[FAMILY_MESSAGE_MAX_LEN - 1] = '\0';
    s_messages[target].hasMessage = true;
    s_messages[target].setAtMillis = millis();
}

const FamilyMessage &FamilyMessages_Get(RecognizedPerson target) {
    if (target < 0 || target >= PERSON_COUNT) target = PERSON_UNKNOWN;
    return s_messages[target];
}

void FamilyMessages_Clear(RecognizedPerson target) {
    if (target < 0 || target >= PERSON_COUNT) return;
    s_messages[target].hasMessage = false;
    s_messages[target].text[0] = '\0';
}
