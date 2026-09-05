/*
 * Paprastas "veido" (dvieju akiu) LVGL komponentas — dezute kaip veikejas,
 * ne "ekranas, kuriame kazka rodome" (zr. README/pokalbio istorija 2026-09-04
 * del produkto krypties). MONOCHROMINIS SAMONINGAI: balta ant juodo,
 * apeinamas zinomas LCD spalvu klausimas (R/B/W jau teisingi, bet G/B
 * nesvarbu, kai R=G=B).
 *
 * V1: 6 busenos, akys keicia AUKSTI (uzmerkta/atmerkta/prisimerkusi), be
 * atskiros "^" (happy) formos — paprasciausias veikiantis variantas pirmam
 * kartui, tobulinti veliau.
 */
#pragma once
#include <lvgl.h>

typedef enum {
    EYE_STATE_SLEEP,     // uzmerktos (plonos linijos), STANDBY
    EYE_STATE_WAKE,      // atsimerkia — naudoti KARTA po pabudimo
    EYE_STATE_IDLE,      // atmerktos, ramios, periodiskai sumirksi
    EYE_STATE_LOOKING,   // "iesko/koncentruojasi" (siek tiek prisimerkusios), SCANNING
    EYE_STATE_HAPPY,     // atpazinta — linksma/prisimerkusi forma
    EYE_STATE_GOODBYE,   // uzsimerkia, grizta i STANDBY
} EyeState;

// Sukuria dvi akis ant nurodyto tevinio ekrano (parent), centruotas.
// Kviesti TIK KARTA (setup() metu) — akys VIENINTELES, perkeliamos tarp
// ekranu per EyeRenderer_MoveToParent(), ne kuriamos is naujo.
void EyeRenderer_Create(lv_obj_t *parent);

// Perkelia JAU sukurtas akis i kita ekrana (lv_obj_set_parent). BUTINA
// iskviesti PRIES bet kokio ekrano lv_obj_clean(), jei akys tuo metu gali
// buti to ekrano vaikas — kitaip clean() jas sunaikins.
void EyeRenderer_MoveToParent(lv_obj_t *newParent);

// Pakeicia busena su animuotu perejimu (aukstis priklauso nuo busenos).
void EyeRenderer_SetState(EyeState state);

// Vienkartinis greitas mirksejimas (uzmerkia-atmerkia), negriaunant
// dabartines busenos — kviesti periodiskai is loop() IDLE metu "gyvumui".
void EyeRenderer_Blink();

/*
 * TIMELINE/SEQUENCER (2026-09-04) — vietoj to, kad kiekviena efekta
 * programuotume atskirai (kaip iki siol), scenarijus aprasomas kaip
 * duomenu masyvas (EyeStep[]), o sitas varikliukas ji tiesiog vykdo.
 * Idejos autorius — vartotojo pokalbis su ChatGPT 2026-09-04: "mes
 * programuojame atskirus efektus, o ne turime is anksto nupiesta rezisura".
 *
 * Kiekvienas EyeStep — viena "poza" (zvilgsnio kryptis ARBA mirksejimas) +
 * kiek laiko ja laikyti pries pereinant prie kito zingsnio.
 */
typedef struct {
    int8_t gazeXPct;       // -100..100 (kaire..desine), 0=centras. Ignoruojama, jei blink=true.
    int8_t gazeYPct;       // -100..100 (virsus..apacia), 0=centras.
    bool blink;            // jei true — sis zingsnis yra greitas mirksejimas (gaze* ignoruojami).
    uint16_t transitionMs; // per kiek laiko zvilgsnis pasiekia sia poza (anim trukme).
    uint16_t holdMs;       // kiek laukti PRIES pereinant i kita zingsni (visa zingsnio trukme).
} EyeStep;

// Choreografuota "pabudimo" seka (~2.3s, zr. eye_renderer.cpp konkrecius
// zingsnius) — zvilgsnis i sonus + zvilgsnis aukstyn + mirksejimas + grizimas
// i centra. Pakeicia buvusi "tuscia" laukima pries fotografuojant (vartotojo
// pastaba 2026-09-04: "fotografuoja per greitai... reikia pauzes ir kazkokio
// zenklo animacijos"). Kviesti KARTA, kai prasideda SCANNING. `onComplete`
// iskvieciamas, kai seka baigiasi — cia tinkamas momentas pradeti tikra
// fotografavima/atpazinima.
void EyeRenderer_PlayWakeSequence(void (*onComplete)());

// Besikartojanti "gyva" poza (zvilgsnis i sonus/aukstyn + mirksejimas,
// begalinis ciklas) — pakeicia buvusi vien periodini mirksejima per
// atpazinimo HTTP laukima (vartotojo pastaba 2026-09-04: "jei tuo metu
// skanuojamas veidas, tada turi begti taskeliai"). Kviesti KARTA, kai
// prasideda laukimas (FaceRecognition_IdentifyAsync); sustabdyti su
// EyeRenderer_StopSequence(), kai atsakymas gautas (nesvarbu, koks).
void EyeRenderer_PlayRecognizingLoop();

// Nutraukia siuo metu vykstancia seka (jei yra) IR grazina zvilgsni i centra
// be animacijos. Saugu kviesti visada, net jei jokia seka nevyksta.
void EyeRenderer_StopSequence();

bool EyeRenderer_IsSequencePlaying();
