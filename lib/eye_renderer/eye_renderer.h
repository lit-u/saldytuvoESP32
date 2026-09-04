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
