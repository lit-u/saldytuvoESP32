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

typedef struct {
    lv_obj_t *obj;
    int32_t centerX;
} EyeHandle;

static EyeHandle s_leftEye = {nullptr, -EYE_GAP / 2};
static EyeHandle s_rightEye = {nullptr, EYE_GAP / 2};
static EyeHandle s_leftBrow = {nullptr, -EYE_GAP / 2};
static EyeHandle s_rightBrow = {nullptr, EYE_GAP / 2};
static EyeState s_state = EYE_STATE_SLEEP;
static int32_t s_currentH = EYE_H_CLOSED;  // busenos aukstis (be mirksejimo overlay)

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

void EyeRenderer_Create(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);

    s_leftEye.obj = createEyeShape(parent);
    s_rightEye.obj = createEyeShape(parent);
    setEyeHeight(&s_leftEye, EYE_H_CLOSED);
    setEyeHeight(&s_rightEye, EYE_H_CLOSED);

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
