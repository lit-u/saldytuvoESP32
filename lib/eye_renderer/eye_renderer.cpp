#include "eye_renderer.h"

#define EYE_W 60
#define EYE_H_OPEN 60
#define EYE_H_LOOKING 42
#define EYE_H_CLOSED 6
#define EYE_GAP 90     // atstumas tarp aki centru
#define EYE_Y_OFFSET (-100)  // akys virsutineje ekrano dalyje, vieta tekstui apacioje

#define BROW_W 46
#define BROW_H 8
#define BROW_Y_OFFSET (EYE_Y_OFFSET - 42)  // virs akiu
#define BROW_ANGLE_NEUTRAL 0
#define BROW_ANGLE_LOOKING_TILT 150   // 15.0 laipsniu — "susikaupusios" (V formos)
#define BROW_ANGLE_HAPPY_TILT 150     // 15.0 laipsniu — "linksmos" (^ ^ formos, priesinga kryptimi)

// Zvilgsnio taskas (pupil) — mazas balta apskritimas KIEKVIENOS akies VIDUJE
// (LVGL vaikas), zr. 2026-09-04 timeline/sequencer pastaba eye_renderer.h.
// Kadangi tai vaikas, o ne atskiras ekrano objektas, jis AUTOMATISKAI
// keliauja kartu su akimi per EyeRenderer_MoveToParent() (nereikia atskiro
// tvarkymo) IR automatiskai apkerpamas (clip) i akies stacatakampi — kai akis
// uzmerkta (EYE_H_CLOSED=6px), taskas paprasciausiai issijungia is matomumo,
// tiksliai taip, kaip realaus vokas uzdengtu voku.
#define PUPIL_SIZE 14
#define GAZE_RANGE_X 16   // px, i kaire/desine nuo akies centro
#define GAZE_RANGE_Y 9    // px, i virsu/apacia (mazesnis, nes akis plonesne)

typedef struct {
    lv_obj_t *obj;
    int32_t centerX;
} EyeHandle;

static EyeHandle s_leftEye = {nullptr, -EYE_GAP / 2};
static EyeHandle s_rightEye = {nullptr, EYE_GAP / 2};
static EyeHandle s_leftBrow = {nullptr, -EYE_GAP / 2};
static EyeHandle s_rightBrow = {nullptr, EYE_GAP / 2};
static lv_obj_t *s_leftPupil = nullptr;
static lv_obj_t *s_rightPupil = nullptr;
static EyeState s_state = EYE_STATE_SLEEP;
static int32_t s_currentH = EYE_H_CLOSED;  // busenos aukstis (be mirksejimo overlay)
static int32_t s_gazeX = 0, s_gazeY = 0;   // dabartinis pupiliu poslinkis (px), sequencer'io busena

static void setEyeHeight(EyeHandle *eye, int32_t h) {
    if (h < 2) h = 2;
    lv_obj_set_size(eye->obj, EYE_W, h);
    lv_obj_align(eye->obj, LV_ALIGN_CENTER, eye->centerX, EYE_Y_OFFSET);
}

static void animHeightCb(void *var, int32_t v) {
    setEyeHeight((EyeHandle *)var, v);
}

static void animateEyeHeight(EyeHandle *eye, int32_t toH, uint32_t durMs) {
    int32_t fromH = lv_obj_get_height(eye->obj);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, eye);
    lv_anim_set_values(&a, fromH, toH);
    lv_anim_set_duration(&a, durMs);
    lv_anim_set_exec_cb(&a, animHeightCb);
    lv_anim_start(&a);
}

static void setBothEyesHeight(int32_t h, uint32_t durMs) {
    animateEyeHeight(&s_leftEye, h, durMs);
    animateEyeHeight(&s_rightEye, h, durMs);
}

static lv_obj_t *createEyeShape(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    // Tuscios viduje (tik kontūras) — vartotojo pastaba 2026-09-04.
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, lv_color_white(), 0);
    lv_obj_set_style_border_width(o, 4, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *createBrowShape(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, BROW_W, BROW_H);
    // Sukimosi asis — paties objekto centras (kad pasukus liktu vietoje).
    lv_obj_set_style_transform_pivot_x(o, BROW_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(o, BROW_H / 2, 0);
    return o;
}

static void positionBrow(EyeHandle *brow) {
    lv_obj_align(brow->obj, LV_ALIGN_CENTER, brow->centerX, BROW_Y_OFFSET);
}

static void setBrowAngles(int32_t leftAngle, int32_t rightAngle) {
    if (!s_leftBrow.obj || !s_rightBrow.obj) return;
    lv_obj_set_style_transform_rotation(s_leftBrow.obj, leftAngle, 0);
    lv_obj_set_style_transform_rotation(s_rightBrow.obj, rightAngle, 0);
}

static lv_obj_t *createPupil(lv_obj_t *eyeParent) {
    lv_obj_t *o = lv_obj_create(eyeParent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, PUPIL_SIZE, PUPIL_SIZE);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, 0);
    return o;
}

static void pupilTranslateXCb(void *var, int32_t v) {
    lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void pupilTranslateYCb(void *var, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void animPupilAxis(lv_obj_t *pupil, int32_t fromVal, int32_t toVal, uint32_t durMs, bool isX) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, pupil);
    lv_anim_set_values(&a, fromVal, toVal);
    lv_anim_set_duration(&a, durMs);
    lv_anim_set_exec_cb(&a, isX ? pupilTranslateXCb : pupilTranslateYCb);
    lv_anim_start(&a);
}

// Nuveda zvilgsni i nurodyta kryptu (xPct/yPct: -100..100) per durMs.
// Abi akys visada ziuri ta pacia kryptimi (natūralu), tad viena bendra
// s_gazeX/s_gazeY busena abiem.
static void setGaze(int8_t xPct, int8_t yPct, uint16_t durMs) {
    if (!s_leftPupil || !s_rightPupil) return;
    int32_t toX = (GAZE_RANGE_X * (int32_t)xPct) / 100;
    int32_t toY = (GAZE_RANGE_Y * (int32_t)yPct) / 100;
    animPupilAxis(s_leftPupil, s_gazeX, toX, durMs, true);
    animPupilAxis(s_rightPupil, s_gazeX, toX, durMs, true);
    animPupilAxis(s_leftPupil, s_gazeY, toY, durMs, false);
    animPupilAxis(s_rightPupil, s_gazeY, toY, durMs, false);
    s_gazeX = toX;
    s_gazeY = toY;
}

// Nulina zvilgsni I KARTA (be animacijos) — naudojama busenos keitimo metu,
// kad kiekviena nauja busena visada pradetu nuo centro.
static void resetGazeInstant() {
    if (!s_leftPupil || !s_rightPupil) return;
    lv_obj_set_style_translate_x(s_leftPupil, 0, 0);
    lv_obj_set_style_translate_x(s_rightPupil, 0, 0);
    lv_obj_set_style_translate_y(s_leftPupil, 0, 0);
    lv_obj_set_style_translate_y(s_rightPupil, 0, 0);
    s_gazeX = 0;
    s_gazeY = 0;
}

void EyeRenderer_Create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);

    s_leftEye.obj = createEyeShape(parent);
    s_rightEye.obj = createEyeShape(parent);
    setEyeHeight(&s_leftEye, EYE_H_CLOSED);
    setEyeHeight(&s_rightEye, EYE_H_CLOSED);

    s_leftPupil = createPupil(s_leftEye.obj);
    s_rightPupil = createPupil(s_rightEye.obj);

    s_leftBrow.obj = createBrowShape(parent);
    s_rightBrow.obj = createBrowShape(parent);
    positionBrow(&s_leftBrow);
    positionBrow(&s_rightBrow);
    setBrowAngles(BROW_ANGLE_NEUTRAL, BROW_ANGLE_NEUTRAL);

    s_state = EYE_STATE_SLEEP;
    s_currentH = EYE_H_CLOSED;

    lv_timer_handler();  // priverstinis pirmas issdestymo/piesimo ciklas
}

void EyeRenderer_MoveToParent(lv_obj_t *newParent) {
    if (!s_leftEye.obj || !s_rightEye.obj) return;
    lv_obj_set_parent(s_leftEye.obj, newParent);
    lv_obj_set_parent(s_rightEye.obj, newParent);
    lv_obj_set_parent(s_leftBrow.obj, newParent);
    lv_obj_set_parent(s_rightBrow.obj, newParent);
    // Saugumo tinklas: uztikrina, kad akys visada turi zinoma dydi is karto
    // po perkelimo (pries kviecianciai pusei iskvieciant SetState), pigu.
    setEyeHeight(&s_leftEye, s_currentH);
    setEyeHeight(&s_rightEye, s_currentH);
    positionBrow(&s_leftBrow);
    positionBrow(&s_rightBrow);
}

void EyeRenderer_SetState(EyeState state) {
    s_state = state;
    // Kiekviena nauja busena visada pradeda nuo centrinio zvilgsnio — bet
    // kokia anksciau vykusi sequencer seka jau sustabdyta (zr. SCANNING
    // pereigas app_state_machine.cpp, kur StopSequence() kviecianas PRIES
    // SetState).
    resetGazeInstant();

    switch (state) {
        case EYE_STATE_SLEEP:
        case EYE_STATE_GOODBYE:
            s_currentH = EYE_H_CLOSED;
            setBothEyesHeight(s_currentH, 400);
            setBrowAngles(BROW_ANGLE_NEUTRAL, BROW_ANGLE_NEUTRAL);
            break;
        case EYE_STATE_WAKE:
        case EYE_STATE_IDLE:
            s_currentH = EYE_H_OPEN;
            setBothEyesHeight(s_currentH, 500);
            setBrowAngles(BROW_ANGLE_NEUTRAL, BROW_ANGLE_NEUTRAL);
            break;
        case EYE_STATE_LOOKING:
            s_currentH = EYE_H_LOOKING;
            setBothEyesHeight(s_currentH, 300);
            // "Susikaupusios" — vidiniai antakiu galai zemyn (V forma).
            setBrowAngles(BROW_ANGLE_LOOKING_TILT, -BROW_ANGLE_LOOKING_TILT);
            break;
        case EYE_STATE_HAPPY:
            // Pilnos/apvalios akys (silta, atviras zvilgsnis atpazinus) +
            // pakelti antakiai (^ ^ forma) — vartotojo pastaba 2026-09-04.
            s_currentH = EYE_H_OPEN;
            setBothEyesHeight(s_currentH, 250);
            setBrowAngles(-BROW_ANGLE_HAPPY_TILT, BROW_ANGLE_HAPPY_TILT);
            break;
    }
}

void EyeRenderer_Blink() {
    if (!s_leftEye.obj) return;
    // Greitas uzmerkimas-atmerkimas i DABARTINE busenos aukstį, ne SLEEP —
    // naudoti tik IDLE/LOOKING/HAPPY metu "gyvumo" jausmui.
    animateEyeHeight(&s_leftEye, EYE_H_CLOSED, 90);
    animateEyeHeight(&s_rightEye, EYE_H_CLOSED, 90);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_leftEye);
    lv_anim_set_values(&a, EYE_H_CLOSED, s_currentH);
    lv_anim_set_duration(&a, 120);
    lv_anim_set_delay(&a, 100);
    lv_anim_set_exec_cb(&a, animHeightCb);
    lv_anim_start(&a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, &s_rightEye);
    lv_anim_set_values(&b, EYE_H_CLOSED, s_currentH);
    lv_anim_set_duration(&b, 120);
    lv_anim_set_delay(&b, 100);
    lv_anim_set_exec_cb(&b, animHeightCb);
    lv_anim_start(&b);
}

// --- Sequencer/timeline varikliukas (2026-09-04) ----------------------------
// Scenarijus (EyeStep masyvas) yra DUOMENYS, o sitas varikliukas juos tiesiog
// vykdo per lv_timer — zr. eye_renderer.h komentara del motyvacijos.

// gazeX, gazeY,  blink, transitionMs, holdMs
static const EyeStep EYE_SEQ_WAKE[] = {
    {   0,   0, false,   0, 250 },  // trumpa pauze centre (ekranas ka tik pasirode)
    { -70,   0, false, 250, 450 },  // zvilgsnis kaire
    {  70,   0, false, 300, 500 },  // zvilgsnis desine
    {   0,   0, true,    0, 350 },  // mirkt
    {   0, -60, false, 200, 400 },  // zvilgsnis aukstyn (susidomejimas)
    {   0,   0, false, 250, 350 },  // grizta i centra — pasiruose fotografuoti
};
static const size_t EYE_SEQ_WAKE_LEN = sizeof(EYE_SEQ_WAKE) / sizeof(EYE_SEQ_WAKE[0]);

static const EyeStep EYE_SEQ_RECOGNIZING[] = {
    { -60,   0, false, 300, 550 },
    {   0,   0, false, 300, 450 },
    {  60,   0, false, 300, 550 },
    {   0,   0, true,    0, 350 },
    {   0, -50, false, 250, 500 },
    {   0,   0, false, 250, 450 },
};
static const size_t EYE_SEQ_RECOGNIZING_LEN = sizeof(EYE_SEQ_RECOGNIZING) / sizeof(EYE_SEQ_RECOGNIZING[0]);

static const EyeStep *s_seq = nullptr;
static size_t s_seqLen = 0;
static size_t s_seqIdx = 0;
static bool s_seqLoop = false;
static lv_timer_t *s_seqTimer = nullptr;
static void (*s_seqOnComplete)() = nullptr;

static void playCurrentStep() {
    const EyeStep &st = s_seq[s_seqIdx];
    if (st.blink) {
        EyeRenderer_Blink();
    } else {
        setGaze(st.gazeXPct, st.gazeYPct, st.transitionMs);
    }
    s_seqTimer = lv_timer_create(
        [](lv_timer_t *t) {
            (void)t;
            s_seqTimer = nullptr;
            s_seqIdx++;
            if (s_seqIdx >= s_seqLen) {
                if (s_seqLoop) {
                    s_seqIdx = 0;
                    playCurrentStep();
                    return;
                }
                void (*cb)() = s_seqOnComplete;
                s_seq = nullptr;
                s_seqOnComplete = nullptr;
                if (cb) cb();
                return;
            }
            playCurrentStep();
        },
        st.holdMs, nullptr);
    lv_timer_set_repeat_count(s_seqTimer, 1);
}

static void playSequence(const EyeStep *steps, size_t count, bool loop, void (*onComplete)()) {
    EyeRenderer_StopSequence();
    if (count == 0) {
        if (onComplete) onComplete();
        return;
    }
    s_seq = steps;
    s_seqLen = count;
    s_seqIdx = 0;
    s_seqLoop = loop;
    s_seqOnComplete = onComplete;
    playCurrentStep();
}

void EyeRenderer_PlayWakeSequence(void (*onComplete)()) {
    playSequence(EYE_SEQ_WAKE, EYE_SEQ_WAKE_LEN, false, onComplete);
}

void EyeRenderer_PlayRecognizingLoop() {
    playSequence(EYE_SEQ_RECOGNIZING, EYE_SEQ_RECOGNIZING_LEN, true, nullptr);
}

void EyeRenderer_StopSequence() {
    if (s_seqTimer) {
        lv_timer_delete(s_seqTimer);
        s_seqTimer = nullptr;
    }
    s_seq = nullptr;
    s_seqLen = 0;
    s_seqIdx = 0;
    s_seqLoop = false;
    s_seqOnComplete = nullptr;
    resetGazeInstant();
}

bool EyeRenderer_IsSequencePlaying() {
    return s_seq != nullptr;
}
