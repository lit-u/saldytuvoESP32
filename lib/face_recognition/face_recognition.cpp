#include "face_recognition.h"

static RecognizedPerson s_debugForced = PERSON_UNKNOWN;

void FaceRecognition_Init() {
    // TODO: esp-who / ESP-DL modelio ikrovimas.
    s_debugForced = PERSON_UNKNOWN;
}

RecognizedPerson FaceRecognition_Identify() {
    // TODO: pakeisti tikru OV5640 kadro atpazinimu. Kol kas grazina
    // rankiniu budu nustatyta reiksme (numatytoji: PERSON_UNKNOWN),
    // kad state machine galetu buti pilnai istestuota be kameros modelio.
    return s_debugForced;
}

void FaceRecognition_DebugForce(RecognizedPerson person) {
    s_debugForced = person;
}
