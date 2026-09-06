/**
 * @file lv_tjpgd.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#include "../../draw/lv_image_decoder_private.h"
#include "../../../lvgl.h"
#if LV_USE_TJPGD

#include "tjpgd.h"
#include "lv_tjpgd.h"
#include "../../misc/lv_fs_private.h"
#include <string.h>

/*********************
 *      DEFINES
 *********************/

#define DECODER_NAME    "TJPGD"

#define TJPGD_WORKBUFF_SIZE             4096    //Recommended by TJPGD library

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_result_t decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc, lv_image_header_t * header);
static lv_result_t decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc);

static lv_result_t decoder_get_area(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc,
                                    const lv_area_t * full_area, lv_area_t * decoded_area);
static void decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc);
static size_t input_func(JDEC * jd, uint8_t * buff, size_t ndata);
static int is_jpg(const uint8_t * raw_data, size_t len);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_tjpgd_init(void)
{
    lv_image_decoder_t * dec = lv_image_decoder_create();
    lv_image_decoder_set_info_cb(dec, decoder_info);
    lv_image_decoder_set_open_cb(dec, decoder_open);
    lv_image_decoder_set_get_area_cb(dec, decoder_get_area);
    lv_image_decoder_set_close_cb(dec, decoder_close);

    dec->name = DECODER_NAME;
}

void lv_tjpgd_deinit(void)
{
    lv_image_decoder_t * dec = NULL;
    while((dec = lv_image_decoder_get_next(dec)) != NULL) {
        if(dec->info_cb == decoder_info) {
            lv_image_decoder_delete(dec);
            break;
        }
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_result_t decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc, lv_image_header_t * header)
{
    LV_UNUSED(decoder);

    const void * src = dsc->src;
    lv_image_src_t src_type = dsc->src_type;

    if(src_type == LV_IMAGE_SRC_VARIABLE) {
        const lv_image_dsc_t * img_dsc = src;
        uint8_t * raw_data = (uint8_t *)img_dsc->data;
        const uint32_t raw_data_size = img_dsc->data_size;

        if(is_jpg(raw_data, raw_data_size) == true) {
#if LV_USE_FS_MEMFS
            header->cf = LV_COLOR_FORMAT_RAW;
            header->w = img_dsc->header.w;
            header->h = img_dsc->header.h;
            header->stride = img_dsc->header.w * 3;
            return LV_RESULT_OK;
#else
            LV_LOG_WARN("LV_USE_FS_MEMFS needs to enabled to decode from data");
            return LV_RESULT_INVALID;
#endif
        }
    }
    else if(src_type == LV_IMAGE_SRC_FILE) {
        const char * fn = src;
        const char * ext = lv_fs_get_ext(fn);
        if((lv_strcmp(ext, "jpg") == 0) || (lv_strcmp(ext, "jpeg") == 0)) {
            uint8_t workb[TJPGD_WORKBUFF_SIZE];
            JDEC jd;
            JRESULT rc = jd_prepare(&jd, input_func, workb, TJPGD_WORKBUFF_SIZE, &dsc->file);
            if(rc) {
                LV_LOG_WARN("jd_prepare error: %d", rc);
                return LV_RESULT_INVALID;
            }
            header->cf = LV_COLOR_FORMAT_RAW;
            header->w = jd.width;
            header->h = jd.height;
            header->stride = jd.width * 3;

            return LV_RESULT_OK;
        }
    }
    return LV_RESULT_INVALID;
}

static size_t input_func(JDEC * jd, uint8_t * buff, size_t ndata)
{
    lv_fs_file_t * f = jd->device;
    if(!f) return 0;

    if(buff) {
        uint32_t rn = 0;
        lv_fs_read(f, buff, (uint32_t)ndata, &rn);
        return rn;
    }
    else {
        uint32_t pos;
        lv_fs_tell(f, &pos);
        lv_fs_seek(f, (uint32_t)(ndata + pos),  LV_FS_SEEK_SET);
        return ndata;
    }
    return 0;
}

/**
 * Decode a JPG image and return the decoded data.
 * @param decoder pointer to the decoder
 * @param dsc     pointer to the decoder descriptor
 * @return LV_RESULT_OK: no error; LV_RESULT_INVALID: can't open the image
 */
static lv_result_t decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);
    lv_fs_file_t * f = NULL;
    if(dsc->src_type == LV_IMAGE_SRC_VARIABLE) {
#if LV_USE_FS_MEMFS
        const lv_image_dsc_t * img_dsc = dsc->src;
        if(is_jpg(img_dsc->data, img_dsc->data_size) == true) {
            f = lv_malloc(sizeof(lv_fs_file_t));
            if(f == NULL) return LV_RESULT_INVALID;
            lv_fs_path_ex_t path;
            lv_fs_make_path_from_buffer(&path, LV_FS_MEMFS_LETTER, img_dsc->data, img_dsc->data_size, "bin");
            lv_fs_res_t res;
            res = lv_fs_open(f, (const char *)&path, LV_FS_MODE_RD);
            if(res != LV_FS_RES_OK) {
                lv_free(f);
                return LV_RESULT_INVALID;
            }
        }
#else
        LV_LOG_WARN("LV_USE_FS_MEMFS needs to enabled to decode from data");
#endif
    }
    else if(dsc->src_type == LV_IMAGE_SRC_FILE) {
        const char * fn = dsc->src;
        if((lv_strcmp(lv_fs_get_ext(fn), "jpg") == 0) || (lv_strcmp(lv_fs_get_ext(fn), "jpeg") == 0)) {
            f = lv_malloc(sizeof(lv_fs_file_t));
            if(f == NULL) return LV_RESULT_INVALID;
            lv_fs_res_t res;
            res = lv_fs_open(f, fn, LV_FS_MODE_RD);
            if(res != LV_FS_RES_OK) {
                lv_free(f);
                return LV_RESULT_INVALID;
            }
        }
    }
    if(f == NULL) return LV_RESULT_INVALID;

    uint8_t * workb_temp = lv_malloc(TJPGD_WORKBUFF_SIZE);
    JDEC * jd = lv_malloc(sizeof(JDEC));
    JRESULT rc = JDR_MEM1;

    if(workb_temp != NULL && jd != NULL)
        rc = jd_prepare(jd, input_func, workb_temp, (size_t)TJPGD_WORKBUFF_SIZE, f);

    if(rc != JDR_OK) {
        lv_fs_close(f);
        lv_free(f);
        lv_free(workb_temp);
        lv_free(jd);
        return LV_RESULT_INVALID;
    }

    dsc->user_data = jd;
    dsc->header.cf = LV_COLOR_FORMAT_RGB888;
    dsc->header.w = jd->width;
    dsc->header.h = jd->height;
    dsc->header.stride = jd->width * 3;

    return LV_RESULT_OK;
}

static lv_result_t decoder_get_area(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc,
                                    const lv_area_t * full_area, lv_area_t * decoded_area)
{
    LV_UNUSED(decoder);
    LV_UNUSED(full_area);

    JDEC * jd = dsc->user_data;
    lv_draw_buf_t * decoded = (void *)dsc->decoded;

    uint32_t  mx, my;
    mx = jd->msx * 8;
    my = jd->msy * 8;         /* Size of the MCU (pixel) */
    if(decoded_area->y1 == LV_COORD_MIN) {
        decoded_area->y1 = 0;
        decoded_area->y2 = my - 1;
        decoded_area->x1 = -((int32_t)mx);
        decoded_area->x2 = -1;
        jd->scale = 0;
        jd->dcv[2] = jd->dcv[1] = jd->dcv[0] = 0;   /* Initialize DC values */
        jd->rst = 0;
        jd->rsc = 0;
        if(decoded == NULL) {
            decoded = lv_malloc_zeroed(sizeof(lv_draw_buf_t));
            dsc->decoded = decoded;
        }
        else {
            lv_fs_seek(jd->device, 0, LV_FS_SEEK_SET);
            JRESULT rc = jd_prepare(jd, input_func, jd->pool_original, (size_t)TJPGD_WORKBUFF_SIZE, jd->device);
            if(rc) return LV_RESULT_INVALID;
        }
        decoded->data = jd->workbuf;
        decoded->header = dsc->header;
    }

    decoded_area->x1 += mx;
    decoded_area->x2 += mx;

    if(decoded_area->x1 >= jd->width) {
        decoded_area->x1 = 0;
        decoded_area->x2 = mx - 1;
        decoded_area->y1 += my;
        decoded_area->y2 += my;
    }

    if(decoded_area->x2 >= jd->width) decoded_area->x2 = jd->width - 1;
    if(decoded_area->y2 >= jd->height) decoded_area->y2 = jd->height - 1;

    decoded->header.w = lv_area_get_width(decoded_area);
    decoded->header.h = lv_area_get_height(decoded_area);
    decoded->header.stride = decoded->header.w * 3;
    decoded->data_size = decoded->header.stride * decoded->header.h;

    /* Process restart interval if enabled */
    JRESULT rc;
    if(jd->nrst && jd->rst++ == jd->nrst) {
        rc = jd_restart(jd, jd->rsc++);
        if(rc != JDR_OK) return LV_RESULT_INVALID;
        jd->rst = 1;
    }

    /* Load an MCU (decompress huffman coded stream, dequantize and apply IDCT) */
    rc = jd_mcu_load(jd);
    if(rc != JDR_OK) return LV_RESULT_INVALID;

    /* Output the MCU (YCbCr to RGB, scaling and output) */
    rc = jd_mcu_output(jd, NULL, decoded_area->x1, decoded_area->y1);
    if(rc != JDR_OK) return LV_RESULT_INVALID;

    return LV_RESULT_OK;
}

/**
 * Free the allocated resources
 * @param decoder pointer to the decoder where this function belongs
 * @param dsc pointer to a descriptor which describes this decoding session
 */
static void decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);
    JDEC * jd = dsc->user_data;
    lv_fs_close(jd->device);
    lv_free(jd->device);
    lv_free(jd->pool_original);
    lv_free(jd);
    lv_free((void *)dsc->decoded);
}

static int is_jpg(const uint8_t * raw_data, size_t len)
{
    const uint8_t jpg_signature[] = {0xFF, 0xD8, 0xFF,  0xE0,  0x00,  0x10, 0x4A,  0x46, 0x49, 0x46};
    if(len < sizeof(jpg_signature)) return false;
    return memcmp(jpg_signature, raw_data, sizeof(jpg_signature)) == 0;
}

/* 2026-09-06 (ChatGPT konsultacija, zr. scratchpad/lvgl_scanning_photo_
 * problem_summary.md): LV_USE_FS_MEMFS + LV_COLOR_FORMAT_RAW + lv_image_
 * set_scale() incremental (tile-po-tile) dekodavimo keliui NEVEIKIA teisingai
 * dideliems (SVGA 800x600) vaizdams — LVGL lv_draw_image.c perduoda decoder'iui
 * "full_area" REGIONA (paskaiciuota is objekto NEsutransformuotu coords),
 * bet SIS TJpgDec adapteris (decoder_get_area(), virs) IGNORUOJA full_area ir
 * dekoduoja VISA nativu vaizda, pasikliaudamas VIEN LVGL isoriniu apkirpimu
 * (intersect) atrinkti kurios plyteles patenka i matoma sriti — su zoom
 * transformacija tai issirenka NEATSITIKTINE, dazniausiai NEMATOMA maza
 * srities dali (patvirtinta REALIU diagnostikos duomenu). JD_USE_SCALE=0
 * (tjpgdcnf.h) reiskia, kad tikras TJpgDec decode-time downscaling irgi
 * NEPRIEINAMAS be papildomo (rizikingesnio) vendored failo keitimo.
 *
 * SPRENDIMAS: dekoduojame VISA JPEG PATYS (be LVGL decoder_* callback'u,
 * be incremental/scale komplikaciju) per JAU VIESA TJpgDec API (jd_prepare
 * + jd_decomp, scale=0 — visada leidziamas), o musu PACIU output callback
 * daro paprasta "nearest neighbor" sumazinima TIESIOGIAI i maza (jau
 * tinkamo dydzio) RGB888 buferi PER KIEKVIENA dekoduota MCU bloka — be
 * didelio (800x600) tarpinio buferio. Rezultatas — paprastas, STATINIS
 * RGB888 paveikslelis, kuri LVGL jau IRODYTAI teisingai rodo (patvirtinta
 * dviem sintetiniais testais siame projekte). */

typedef struct {
    const uint8_t * jpegData;
    size_t jpegLen;
    size_t pos;
    uint8_t * outBuf;
    uint16_t targetW, targetH;
    uint16_t srcW, srcH;
} lv_tjpgd_thumb_ctx_t;

static size_t thumb_input_func(JDEC * jd, uint8_t * buff, size_t ndata)
{
    lv_tjpgd_thumb_ctx_t * ctx = (lv_tjpgd_thumb_ctx_t *)jd->device;
    size_t remain = ctx->jpegLen - ctx->pos;
    size_t n = ndata < remain ? ndata : remain;
    if(buff) memcpy(buff, ctx->jpegData + ctx->pos, n);
    ctx->pos += n;
    return n;
}

static int thumb_output_func(JDEC * jd, void * bitmap, JRECT * rect)
{
    lv_tjpgd_thumb_ctx_t * ctx = (lv_tjpgd_thumb_ctx_t *)jd->device;
    const uint8_t * src = (const uint8_t *)bitmap;
    uint32_t blockW = (uint32_t)(rect->right - rect->left + 1);
    uint32_t blockH = (uint32_t)(rect->bottom - rect->top + 1);

    for(uint32_t sy = 0; sy < blockH; sy++) {
        uint32_t srcY = rect->top + sy;
        uint32_t targetY = (srcY * ctx->targetH) / ctx->srcH;
        if(targetY >= ctx->targetH) targetY = ctx->targetH - 1;
        for(uint32_t sx = 0; sx < blockW; sx++) {
            uint32_t srcX = rect->left + sx;
            uint32_t targetX = (srcX * ctx->targetW) / ctx->srcW;
            if(targetX >= ctx->targetW) targetX = ctx->targetW - 1;
            const uint8_t * s = src + (sy * blockW + sx) * 3;
            uint8_t * d = ctx->outBuf + (targetY * (uint32_t)ctx->targetW + targetX) * 3;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
        }
    }
    return 1;  /* testi dekodavima (0 reikstu nutraukti su JDR_INTR) */
}

bool lv_tjpgd_decode_thumbnail(const uint8_t * jpegData, size_t jpegLen,
                                uint16_t targetW, uint16_t targetH,
                                uint8_t * outBuf, size_t outBufSize)
{
    if((size_t)targetW * targetH * 3 > outBufSize) return false;

    lv_tjpgd_thumb_ctx_t ctx;
    ctx.jpegData = jpegData;
    ctx.jpegLen = jpegLen;
    ctx.pos = 0;
    ctx.outBuf = outBuf;
    ctx.targetW = targetW;
    ctx.targetH = targetH;

    uint8_t workb[TJPGD_WORKBUFF_SIZE];
    JDEC jd;
    JRESULT rc = jd_prepare(&jd, thumb_input_func, workb, sizeof(workb), &ctx);
    if(rc != JDR_OK) return false;

    ctx.srcW = jd.width;
    ctx.srcH = jd.height;

    rc = jd_decomp(&jd, thumb_output_func, 0);
    return rc == JDR_OK;
}

#endif /*LV_USE_TJPGD*/
