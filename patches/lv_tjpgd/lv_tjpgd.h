/**
 * @file lv_tjpgd.h
 *
 */

#ifndef LV_TJPGD_H
#define LV_TJPGD_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if LV_USE_TJPGD

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

void lv_tjpgd_init(void);

void lv_tjpgd_deinit(void);

/* 2026-09-06: dekoduoja VISA JPEG (bet kokio dydzio) i JAU sumazinta
 * (nearest-neighbor) RGB888 buferi, apeinant LVGL image decoder/scale
 * pipeline'a — zr. lv_tjpgd.c komentara del priezasties. `outBuf` turi
 * tureti bent targetW*targetH*3 baitu vietos. Grazina false, jei JPEG
 * neteisingas arba outBuf per mazas. */
bool lv_tjpgd_decode_thumbnail(const uint8_t * jpegData, size_t jpegLen,
                                uint16_t targetW, uint16_t targetH,
                                uint8_t * outBuf, size_t outBufSize);

/**********************
 *      MACROS
 **********************/

#endif /*LV_USE_TJPGD*/

#ifdef __cplusplus
}
#endif

#endif /* LV_TJPGD_H */
