/**
 * @file lv_disp.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_disp.h"
#include "../misc/lv_math.h"
#include "../core/lv_refr.h"
#include "../core/lv_disp.h"
#include "../core/lv_disp_priv.h"
#include "../misc/lv_gc.h"

#if LV_USE_DRAW_SW
    #include "../draw/sw/lv_draw_sw.h"
#endif

#if LV_USE_GPU_STM32_DMA2D
    #include "../draw/stm32_dma2d/lv_gpu_stm32_dma2d.h"
#endif

#if LV_USE_GPU_SWM341_DMA2D
    #include "../draw/swm341_dma2d/lv_gpu_swm341_dma2d.h"
#endif

#if LV_USE_GPU_ARM2D
    #include "../draw/arm2d/lv_gpu_arm2d.h"
#endif

#if LV_USE_GPU_NXP_PXP || LV_USE_GPU_NXP_VG_LITE
    #include "../draw/nxp/lv_gpu_nxp.h"
#endif

#if LV_USE_THEME_DEFAULT
    #include "../themes/default/lv_theme_default.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_obj_tree_walk_res_t invalidate_layout_cb(lv_obj_t * obj, void * user_data);
static void update_resolution(lv_disp_t * disp);
static void scr_load_internal(lv_obj_t * scr);
static void scr_load_anim_start(lv_anim_t * a);
static void opa_scale_anim(void * obj, int32_t v);
static void set_x_anim(void * obj, int32_t v);
static void set_y_anim(void * obj, int32_t v);
static void scr_anim_ready(lv_anim_t * a);
static bool is_out_anim(lv_scr_load_anim_t a);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_disp_t * disp_def;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_disp_t * lv_disp_create(lv_coord_t hor_res, lv_coord_t ver_res)
{
    lv_disp_t * disp = _lv_ll_ins_head(&LV_GC_ROOT(_lv_disp_ll));
    LV_ASSERT_MALLOC(disp);
    if(!disp) return NULL;

    lv_disp_init(disp, hor_res, ver_res);

    return disp;
}

void lv_disp_init(lv_disp_t * disp, lv_coord_t hor_res, lv_coord_t ver_res)
{
    lv_memzero(disp, sizeof(lv_disp_t));

    disp->hor_res          = hor_res;
    disp->ver_res          = ver_res;
    disp->physical_hor_res = -1;
    disp->physical_ver_res = -1;
    disp->offset_x         = 0;
    disp->offset_y         = 0;
    disp->antialiasing     = LV_COLOR_DEPTH > 8 ? 1 : 0;
    disp->screen_transp    = 0;
    disp->dpi              = LV_DPI_DEF;
    disp->color_chroma_key = LV_COLOR_CHROMA_KEY;

#if LV_COLOR_DEPTH == 1
    disp->color_format = LV_COLOR_FORMAT_L1;
#elif LV_COLOR_DEPTH == 8
    disp->color_format = LV_COLOR_FORMAT_L8;
#else
    disp->color_format = LV_COLOR_FORMAT_NATIVE;
#endif

#if LV_USE_GPU_STM32_DMA2D
    lv_disp_set_draw_ctx(disp, lv_draw_stm32_dma2d_ctx_init, lv_draw_stm32_dma2d_ctx_deinit,
                         sizeof(lv_draw_stm32_dma2d_ctx_t));
#elif LV_USE_GPU_SWM341_DMA2D
    lv_disp_set_draw_ctx(disp, lv_draw_swm341_dma2d_ctx_init, lv_draw_swm341_dma2d_ctx_deinit,
                         sizeof(lv_draw_swm341_dma2d_ctx_t));
#elif LV_USE_GPU_NXP_PXP || LV_USE_GPU_NXP_VG_LITE
    lv_disp_set_draw_ctx(disp, lv_draw_nxp_ctx_init, lv_draw_nxp_ctx_deinit, sizeof(lv_draw_nxp_ctx_t));
#elif LV_USE_DRAW_SDL
    lv_disp_set_draw_ctx(disp, lv_draw_sdl_init_ctx, lv_draw_sdl_deinit_ctx, sizeof(lv_draw_sdl_ctx_t));
#elif LV_USE_GPU_ARM2D
    lv_disp_set_draw_ctx(disp, lv_draw_arm2d_ctx_init, lv_draw_arm2d_ctx_deinit, sizeof(lv_draw_arm2d_ctx_t));
#else
    lv_disp_set_draw_ctx(disp, lv_draw_sw_init_ctx, lv_draw_sw_deinit_ctx, sizeof(lv_draw_sw_ctx_t));
#endif

    disp->draw_ctx->color_format = disp->color_format;
    disp->draw_ctx->render_with_alpha = disp->screen_transp;

    disp->inv_en_cnt = 1;

    lv_disp_t * disp_def_tmp = disp_def;
    disp_def                 = disp; /*Temporarily change the default screen to create the default screens on the
                                        new display*/
    /*Create a refresh timer*/
    disp->refr_timer = lv_timer_create(_lv_disp_refr_timer, LV_DEF_REFR_PERIOD, disp);
    LV_ASSERT_MALLOC(disp->refr_timer);
    if(disp->refr_timer == NULL) {
        lv_free(disp);
        return;
    }

    disp->bg_color = lv_color_white();
    disp->bg_opa = LV_OPA_COVER;

#if LV_USE_THEME_DEFAULT
    if(lv_theme_default_is_inited() == false) {
        disp->theme = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                            LV_THEME_DEFAULT_DARK, LV_FONT_DEFAULT);
    }
    else {
        disp->theme = lv_theme_default_get();
    }
#endif

    disp->act_scr   = lv_obj_create(NULL); /*Create a default screen on the display*/
    disp->top_layer = lv_obj_create(NULL); /*Create top layer on the display*/
    disp->sys_layer = lv_obj_create(NULL); /*Create sys layer on the display*/
    lv_obj_remove_style_all(disp->top_layer);
    lv_obj_remove_style_all(disp->sys_layer);
    lv_obj_clear_flag(disp->top_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(disp->sys_layer, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_scrollbar_mode(disp->top_layer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollbar_mode(disp->sys_layer, LV_SCROLLBAR_MODE_OFF);

    lv_obj_invalidate(disp->act_scr);

    disp_def = disp_def_tmp; /*Revert the default display*/
    if(disp_def == NULL) disp_def = disp; /*Initialize the default display*/

    lv_timer_ready(disp->refr_timer); /*Be sure the screen will be refreshed immediately on start up*/

    return;
}


void lv_disp_remove(lv_disp_t * disp)
{
    bool was_default = false;
    if(disp == lv_disp_get_default()) was_default = true;

    /*Detach the input devices*/
    lv_indev_t * indev;
    indev = lv_indev_get_next(NULL);
    while(indev) {
        if(lv_indev_get_disp(indev) == disp) {
            lv_indev_set_disp(indev, NULL);
        }
        indev = lv_indev_get_next(indev);
    }

    /* Delete screen and other obj */
    if(disp->sys_layer) {
        lv_obj_del(disp->sys_layer);
        disp->sys_layer = NULL;
    }
    if(disp->top_layer) {
        lv_obj_del(disp->top_layer);
        disp->top_layer = NULL;
    }
    while(disp->screen_cnt != 0) {
        /*Delete the screenst*/
        lv_obj_del(disp->screens[0]);
    }

    _lv_ll_remove(&LV_GC_ROOT(_lv_disp_ll), disp);
    if(disp->refr_timer) lv_timer_del(disp->refr_timer);
    lv_free(disp);

    if(was_default) lv_disp_set_default(_lv_ll_get_head(&LV_GC_ROOT(_lv_disp_ll)));
}

/**
 * Set a default display. The new screens will be created on it by default.
 * @param disp pointer to a display
 */
void lv_disp_set_default(lv_disp_t * disp)
{
    disp_def = disp;
}

/**
 * Get the default display
 * @return pointer to the default display
 */
lv_disp_t * lv_disp_get_default(void)
{
    return disp_def;
}

lv_disp_t * lv_disp_get_next(lv_disp_t * disp)
{
    if(disp == NULL)
        return _lv_ll_get_head(&LV_GC_ROOT(_lv_disp_ll));
    else
        return _lv_ll_get_next(&LV_GC_ROOT(_lv_disp_ll), disp);
}

/*---------------------
 * RESOLUTION
 *--------------------*/

void lv_disp_set_resolution(lv_disp_t * disp, lv_coord_t hor_res, lv_coord_t ver_res)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    if(disp->hor_res == hor_res && disp->ver_res == ver_res) return;


    disp->hor_res = hor_res;
    disp->ver_res = ver_res;

    update_resolution(disp);
}

void lv_disp_set_physical_resolution(lv_disp_t * disp, lv_coord_t hor_res, lv_coord_t ver_res)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    disp->physical_hor_res = hor_res;
    disp->physical_ver_res = ver_res;

    lv_obj_invalidate(disp->sys_layer);

}

void lv_disp_set_offset(lv_disp_t * disp, lv_coord_t x, lv_coord_t y)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    disp->offset_x = x;
    disp->offset_y = y;

    lv_obj_invalidate(disp->sys_layer);

}

void lv_disp_set_dpi(lv_disp_t * disp, lv_coord_t dpi)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    disp->dpi = dpi;
}

lv_coord_t lv_disp_get_hor_res(const lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();

    if(disp == NULL) {
        return 0;
    }
    else {
        switch(disp->rotated) {
            case LV_DISP_ROTATION_90:
            case LV_DISP_ROTATION_270:
                return disp->ver_res;
            default:
                return disp->hor_res;
        }
    }
}

lv_coord_t lv_disp_get_ver_res(const lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();

    if(disp == NULL) {
        return 0;
    }
    else {
        switch(disp->rotated) {
            case LV_DISP_ROTATION_90:
            case LV_DISP_ROTATION_270:
                return disp->hor_res;
            default:
                return disp->ver_res;
        }
    }
}

lv_coord_t lv_disp_get_physical_horizontal_resolution(const lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();

    if(disp == NULL) {
        return 0;
    }
    else {
        switch(disp->rotated) {
            case LV_DISP_ROTATION_90:
            case LV_DISP_ROTATION_270:
                return disp->physical_ver_res > 0 ? disp->physical_ver_res : disp->ver_res;
            default:
                return disp->physical_hor_res > 0 ? disp->physical_hor_res : disp->hor_res;
        }
    }
}

lv_coord_t lv_disp_get_physical_ver_res(const lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();

    if(disp == NULL) {
        return 0;
    }
    else {
        switch(disp->rotated) {
            case LV_DISP_ROTATION_90:
            case LV_DISP_ROTATION_270:
                return disp->physical_hor_res > 0 ? disp->physical_hor_res : disp->hor_res;
            default:
                return disp->physical_ver_res > 0 ? disp->physical_ver_res : disp->ver_res;
        }
    }
}

lv_coord_t lv_disp_get_offset_x(const lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();

    if(disp == NULL) {
        return 0;
    }
    else {
        switch(disp->rotated) {
            case LV_DISP_ROTATION_90:
                return disp->offset_y;
            case LV_DISP_ROTATION_180:
                return lv_disp_get_physical_horizontal_resolution(disp) - disp->offset_x;
            case LV_DISP_ROTATION_270:
                return lv_disp_get_physical_horizontal_resolution(disp) - disp->offset_y;
            default:
                return disp->offset_x;
        }
    }
}

lv_coord_t lv_disp_get_offset_y(const lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();

    if(disp == NULL) {
        return 0;
    }
    else {
        switch(disp->rotated) {
            case LV_DISP_ROTATION_90:
                return disp->offset_x;
            case LV_DISP_ROTATION_180:
                return lv_disp_get_physical_ver_res(disp) - disp->offset_y;
            case LV_DISP_ROTATION_270:
                return lv_disp_get_physical_ver_res(disp) - disp->offset_x;
            default:
                return disp->offset_y;
        }
    }
}

lv_coord_t lv_disp_get_dpi(const lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return LV_DPI_DEF;  /*Do not return 0 because it might be a divider*/
    return disp->dpi;
}

/*---------------------
 * BUFFERING
 *--------------------*/

void lv_disp_set_draw_buffers(lv_disp_t * disp, void * buf1, void * buf2, uint32_t buf_size_px,
                              lv_disp_render_mode_t render_mode)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    disp->draw_buf_1 = buf1;
    disp->draw_buf_2 = buf2;
    disp->draw_buf_act = buf1;
    disp->draw_buf_size = buf_size_px;
    disp->render_mode = render_mode;
}

void lv_disp_set_flush_cb(lv_disp_t * disp, void (*flush_cb)(struct _lv_disp_t * disp, const lv_area_t * area,
                                                             lv_color_t * color_p))
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    disp->flush_cb = flush_cb;
}

void lv_disp_set_color_format(lv_disp_t * disp, lv_color_format_t color_format)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    disp->color_format = color_format;
}

void lv_disp_set_antialaising(lv_disp_t * disp, bool en)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    disp->antialiasing = en;
}

bool lv_disp_get_antialiasing(lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return false;

    return disp->antialiasing ? true : false;
}


LV_ATTRIBUTE_FLUSH_READY void lv_disp_flush_ready(lv_disp_t * disp)
{
    disp->flushing = 0;
    disp->flushing_last = 0;
}

LV_ATTRIBUTE_FLUSH_READY bool lv_disp_flush_is_last(lv_disp_t * disp)
{
    return disp->flushing_last;
}

/*---------------------
 * DRAW CONTEXT
 *--------------------*/

void lv_disp_set_draw_ctx(lv_disp_t * disp,
                          void (*draw_ctx_init)(lv_disp_t * disp, lv_draw_ctx_t * draw_ctx),
                          void (*draw_ctx_deinit)(lv_disp_t * disp, lv_draw_ctx_t * draw_ctx),
                          size_t draw_ctx_size)
{
    if(disp->draw_ctx) {
        if(disp->draw_ctx_deinit) disp->draw_ctx_deinit(disp, disp->draw_ctx);
        lv_free(disp->draw_ctx);
        disp->draw_ctx = NULL;
    }

    disp->draw_ctx_init = draw_ctx_init;
    disp->draw_ctx_deinit = draw_ctx_deinit;
    disp->draw_ctx_size = draw_ctx_size;

    lv_draw_ctx_t * draw_ctx = lv_malloc(disp->draw_ctx_size);
    LV_ASSERT_MALLOC(draw_ctx);
    if(draw_ctx == NULL) return;
    disp->draw_ctx_init(disp, draw_ctx);
    disp->draw_ctx = draw_ctx;
}

/*---------------------
  * SCREENS
  *--------------------*/

lv_obj_t * lv_disp_get_scr_act(lv_disp_t * disp)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("no display registered to get its active screen");
        return NULL;
    }

    return disp->act_scr;
}

lv_obj_t * lv_disp_get_scr_prev(lv_disp_t * disp)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("no display registered to get its previous screen");
        return NULL;
    }

    return disp->prev_scr;
}

void lv_disp_load_scr(lv_obj_t * scr)
{
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

lv_obj_t * lv_disp_get_layer_top(lv_disp_t * disp)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("lv_layer_top: no display registered to get its top layer");
        return NULL;
    }

    return disp->top_layer;
}

lv_obj_t * lv_disp_get_layer_sys(lv_disp_t * disp)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("lv_layer_sys: no display registered to get its sys. layer");
        return NULL;
    }

    return disp->sys_layer;
}

void lv_scr_load_anim(lv_obj_t * new_scr, lv_scr_load_anim_t anim_type, uint32_t time, uint32_t delay, bool auto_del)
{
    lv_disp_t * d = lv_obj_get_disp(new_scr);
    lv_obj_t * act_scr = lv_scr_act();

    /*If an other screen load animation is in progress
     *make target screen loaded immediately. */
    if(d->scr_to_load && act_scr != d->scr_to_load) {
        scr_load_internal(d->scr_to_load);
        lv_anim_del(d->scr_to_load, NULL);
        lv_obj_set_pos(d->scr_to_load, 0, 0);
        lv_obj_remove_local_style_prop(d->scr_to_load, LV_STYLE_OPA, 0);

        if(d->del_prev) {
            lv_obj_del(act_scr);
        }
        act_scr = d->scr_to_load;
    }

    d->scr_to_load = new_scr;

    if(d->prev_scr && d->del_prev) {
        lv_obj_del(d->prev_scr);
        d->prev_scr = NULL;
    }

    d->draw_prev_over_act = is_out_anim(anim_type);
    d->del_prev = auto_del;

    /*Be sure there is no other animation on the screens*/
    lv_anim_del(new_scr, NULL);
    lv_anim_del(lv_scr_act(), NULL);

    /*Be sure both screens are in a normal position*/
    lv_obj_set_pos(new_scr, 0, 0);
    lv_obj_set_pos(lv_scr_act(), 0, 0);
    lv_obj_remove_local_style_prop(new_scr, LV_STYLE_OPA, 0);
    lv_obj_remove_local_style_prop(lv_scr_act(), LV_STYLE_OPA, 0);


    /*Shortcut for immediate load*/
    if(time == 0 && delay == 0) {

        scr_load_internal(new_scr);
        if(auto_del) lv_obj_del(act_scr);
        return;
    }

    lv_anim_t a_new;
    lv_anim_init(&a_new);
    lv_anim_set_var(&a_new, new_scr);
    lv_anim_set_start_cb(&a_new, scr_load_anim_start);
    lv_anim_set_ready_cb(&a_new, scr_anim_ready);
    lv_anim_set_time(&a_new, time);
    lv_anim_set_delay(&a_new, delay);

    lv_anim_t a_old;
    lv_anim_init(&a_old);
    lv_anim_set_var(&a_old, d->act_scr);
    lv_anim_set_time(&a_old, time);
    lv_anim_set_delay(&a_old, delay);

    switch(anim_type) {
        case LV_SCR_LOAD_ANIM_NONE:
            /*Create a dummy animation to apply the delay*/
            lv_anim_set_exec_cb(&a_new, set_x_anim);
            lv_anim_set_values(&a_new, 0, 0);
            break;
        case LV_SCR_LOAD_ANIM_OVER_LEFT:
            lv_anim_set_exec_cb(&a_new, set_x_anim);
            lv_anim_set_values(&a_new, lv_disp_get_hor_res(d), 0);
            break;
        case LV_SCR_LOAD_ANIM_OVER_RIGHT:
            lv_anim_set_exec_cb(&a_new, set_x_anim);
            lv_anim_set_values(&a_new, -lv_disp_get_hor_res(d), 0);
            break;
        case LV_SCR_LOAD_ANIM_OVER_TOP:
            lv_anim_set_exec_cb(&a_new, set_y_anim);
            lv_anim_set_values(&a_new, lv_disp_get_ver_res(d), 0);
            break;
        case LV_SCR_LOAD_ANIM_OVER_BOTTOM:
            lv_anim_set_exec_cb(&a_new, set_y_anim);
            lv_anim_set_values(&a_new, -lv_disp_get_ver_res(d), 0);
            break;
        case LV_SCR_LOAD_ANIM_MOVE_LEFT:
            lv_anim_set_exec_cb(&a_new, set_x_anim);
            lv_anim_set_values(&a_new, lv_disp_get_hor_res(d), 0);

            lv_anim_set_exec_cb(&a_old, set_x_anim);
            lv_anim_set_values(&a_old, 0, -lv_disp_get_hor_res(d));
            break;
        case LV_SCR_LOAD_ANIM_MOVE_RIGHT:
            lv_anim_set_exec_cb(&a_new, set_x_anim);
            lv_anim_set_values(&a_new, -lv_disp_get_hor_res(d), 0);

            lv_anim_set_exec_cb(&a_old, set_x_anim);
            lv_anim_set_values(&a_old, 0, lv_disp_get_hor_res(d));
            break;
        case LV_SCR_LOAD_ANIM_MOVE_TOP:
            lv_anim_set_exec_cb(&a_new, set_y_anim);
            lv_anim_set_values(&a_new, lv_disp_get_ver_res(d), 0);

            lv_anim_set_exec_cb(&a_old, set_y_anim);
            lv_anim_set_values(&a_old, 0, -lv_disp_get_ver_res(d));
            break;
        case LV_SCR_LOAD_ANIM_MOVE_BOTTOM:
            lv_anim_set_exec_cb(&a_new, set_y_anim);
            lv_anim_set_values(&a_new, -lv_disp_get_ver_res(d), 0);

            lv_anim_set_exec_cb(&a_old, set_y_anim);
            lv_anim_set_values(&a_old, 0, lv_disp_get_ver_res(d));
            break;
        case LV_SCR_LOAD_ANIM_FADE_IN:
            lv_anim_set_exec_cb(&a_new, opa_scale_anim);
            lv_anim_set_values(&a_new, LV_OPA_TRANSP, LV_OPA_COVER);
            break;
        case LV_SCR_LOAD_ANIM_FADE_OUT:
            lv_anim_set_exec_cb(&a_old, opa_scale_anim);
            lv_anim_set_values(&a_old, LV_OPA_COVER, LV_OPA_TRANSP);
            break;
        case LV_SCR_LOAD_ANIM_OUT_LEFT:
            lv_anim_set_exec_cb(&a_old, set_x_anim);
            lv_anim_set_values(&a_old, 0, -lv_disp_get_hor_res(d));
            break;
        case LV_SCR_LOAD_ANIM_OUT_RIGHT:
            lv_anim_set_exec_cb(&a_old, set_x_anim);
            lv_anim_set_values(&a_old, 0, lv_disp_get_hor_res(d));
            break;
        case LV_SCR_LOAD_ANIM_OUT_TOP:
            lv_anim_set_exec_cb(&a_old, set_y_anim);
            lv_anim_set_values(&a_old, 0, -lv_disp_get_ver_res(d));
            break;
        case LV_SCR_LOAD_ANIM_OUT_BOTTOM:
            lv_anim_set_exec_cb(&a_old, set_y_anim);
            lv_anim_set_values(&a_old, 0, lv_disp_get_ver_res(d));
            break;
    }

    lv_obj_send_event(act_scr, LV_OBJ_EVENT_SCREEN_UNLOAD_START, NULL);

    lv_anim_start(&a_new);
    lv_anim_start(&a_old);
}

/*---------------------
 * BACKGROUND
 *--------------------*/

void lv_disp_set_bg_color(lv_disp_t * disp, lv_color_t color)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("no display registered");
        return;
    }

    disp->bg_color = color;

    lv_area_t a;
    lv_area_set(&a, 0, 0, lv_disp_get_hor_res(disp) - 1, lv_disp_get_ver_res(disp) - 1);
    _lv_inv_area(disp, &a);

}

void lv_disp_set_bg_image(lv_disp_t * disp, const void  * img_src)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("no display registered");
        return;
    }

    disp->bg_img = img_src;

    lv_area_t a;
    lv_area_set(&a, 0, 0, lv_disp_get_hor_res(disp) - 1, lv_disp_get_ver_res(disp) - 1);
    _lv_inv_area(disp, &a);
}

void lv_disp_set_bg_opa(lv_disp_t * disp, lv_opa_t opa)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("no display registered");
        return;
    }

    disp->bg_opa = opa;

    lv_area_t a;
    lv_area_set(&a, 0, 0, lv_disp_get_hor_res(disp) - 1, lv_disp_get_ver_res(disp) - 1);
    _lv_inv_area(disp, &a);
}

/*---------------------
 * OTHERS
 *--------------------*/

void lv_disp_add_event_cb(lv_disp_t * disp, lv_event_cb_t event_cb, lv_event_code_t filter, void * user_data)
{
    LV_ASSERT_NULL(disp);

    lv_event_add_callback(&disp->event_list, event_cb, filter, user_data);
}

bool lv_disp_remove_event_cb(lv_disp_t * disp, lv_event_cb_t event_cb)
{
    LV_ASSERT_NULL(disp);

    return lv_event_remove_callback(&disp->event_list, event_cb);
}

bool lv_disp_remove_event_cb_with_user_data(lv_disp_t * disp, lv_event_cb_t event_cb, const void * user_data)
{
    LV_ASSERT_NULL(disp);

    return lv_event_remove_callback_with_user_data(&disp->event_list, event_cb, user_data);
}


void * lv_disp_get_event_user_data_of_cb(lv_disp_t * disp, lv_event_cb_t event_cb)
{
    LV_ASSERT_NULL(disp);

    return lv_event_get_user_data_of_callback(&disp->event_list, event_cb);
}


lv_res_t lv_disp_send_event(lv_disp_t * disp, lv_event_code_t code, void * user_data)
{

    lv_event_t e;
    lv_memzero(&e, sizeof(e));
    e.code = code;
    e.target = disp;
    e.current_target = disp;
    e.param = user_data;
    lv_res_t res;
    res = lv_event_send(&disp->event_list, &e, true);
    if(res != LV_RES_OK) return res;

    res = lv_event_send(&disp->event_list, &e, false);
    if(res != LV_RES_OK) return res;

    return res;
}

void lv_disp_set_rotation(lv_disp_t * disp, lv_disp_rotation_t rotation)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    disp->rotated = rotation;
    update_resolution(disp);

}

lv_disp_rotation_t lv_disp_get_rotation(lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return LV_DISP_ROTATION_NONE;
    return disp->rotated;
}

void lv_disp_set_theme(lv_disp_t * disp, lv_theme_t * th)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("no display registered");
        return;
    }

    disp->theme = th;

    if(disp->screen_cnt == 3 &&
       lv_obj_get_child_cnt(disp->screens[0]) == 0 &&
       lv_obj_get_child_cnt(disp->screens[1]) == 0 &&
       lv_obj_get_child_cnt(disp->screens[2]) == 0) {
        lv_theme_apply(disp->screens[0]);
    }
}

lv_theme_t * lv_disp_get_theme(lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();
    return disp->theme;
}

uint32_t lv_disp_get_inactive_time(const lv_disp_t * disp)
{
    if(disp) return lv_tick_elaps(disp->last_activity_time);

    lv_disp_t * d;
    uint32_t t = UINT32_MAX;
    d          = lv_disp_get_next(NULL);
    while(d) {
        uint32_t elaps = lv_tick_elaps(d->last_activity_time);
        t = LV_MIN(t, elaps);
        d = lv_disp_get_next(d);
    }

    return t;
}

void lv_disp_trig_activity(lv_disp_t * disp)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("lv_disp_trig_activity: no display registered");
        return;
    }

    disp->last_activity_time = lv_tick_get();
}

void lv_disp_enable_invalidation(lv_disp_t * disp, bool en)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("no display registered");
        return;
    }

    disp->inv_en_cnt += en ? 1 : -1;
}

bool lv_disp_is_invalidation_enabled(lv_disp_t * disp)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("no display registered");
        return false;
    }

    return (disp->inv_en_cnt > 0);
}

lv_timer_t * _lv_disp_get_refr_timer(lv_disp_t * disp)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) return NULL;

    return disp->refr_timer;
}


lv_color_t lv_disp_get_chroma_key_color(lv_disp_t * disp)
{

    if(!disp) disp = lv_disp_get_default();
    if(!disp) {
        LV_LOG_WARN("no display registered");
        return lv_color_hex(0x00ff00);
    }

    return disp->color_chroma_key;
}


void lv_disp_set_user_data(lv_disp_t * disp, void * user_data)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) return;

#if LV_USE_USER_DATA
    disp->user_data = user_data;
#else
    LV_UNUSED(user_data);
    LV_LOG_WARN("LV_USE_USER_DATA is not enabled");
#endif
}

void * lv_disp_get_user_data(lv_disp_t * disp)
{
    if(!disp) disp = lv_disp_get_default();
    if(!disp) return NULL;

#if LV_USE_USER_DATA
    return disp->user_data;
#else
    LV_LOG_WARN("LV_USE_USER_DATA is no enabled");
    return NULL;
#endif

}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void update_resolution(lv_disp_t * disp)
{
    lv_coord_t hor_res = lv_disp_get_hor_res(disp);
    lv_coord_t ver_res = lv_disp_get_ver_res(disp);

    lv_area_t prev_coords;
    lv_obj_get_coords(disp->sys_layer, &prev_coords);
    uint32_t i;
    for(i = 0; i < disp->screen_cnt; i++) {
        lv_area_set_width(&disp->screens[i]->coords, hor_res);
        lv_area_set_height(&disp->screens[i]->coords, ver_res);
        lv_obj_send_event(disp->screens[i], LV_OBJ_EVENT_SIZE_CHANGED, &prev_coords);
    }

    lv_area_set_width(&disp->top_layer->coords, hor_res);
    lv_area_set_height(&disp->top_layer->coords, ver_res);
    lv_obj_send_event(disp->top_layer, LV_OBJ_EVENT_SIZE_CHANGED, &prev_coords);

    lv_area_set_width(&disp->sys_layer->coords, hor_res);
    lv_area_set_height(&disp->sys_layer->coords, ver_res);
    lv_obj_send_event(disp->sys_layer, LV_OBJ_EVENT_SIZE_CHANGED, &prev_coords);

    lv_memzero(disp->inv_areas, sizeof(disp->inv_areas));
    lv_memzero(disp->inv_area_joined, sizeof(disp->inv_area_joined));
    disp->inv_p = 0;
    lv_obj_invalidate(disp->sys_layer);

    lv_obj_tree_walk(NULL, invalidate_layout_cb, NULL);

    lv_disp_send_event(disp, LV_DISP_EVENT_RESOLUTION_CHANGED, NULL);
}

static lv_obj_tree_walk_res_t invalidate_layout_cb(lv_obj_t * obj, void * user_data)
{
    LV_UNUSED(user_data);
    lv_obj_mark_layout_as_dirty(obj);
    return LV_OBJ_TREE_WALK_NEXT;
}

static void scr_load_internal(lv_obj_t * scr)
{
    lv_disp_t * d = lv_obj_get_disp(scr);
    if(!d) return;  /*Shouldn't happen, just to be sure*/

    lv_obj_t * old_scr = d->act_scr;

    if(d->act_scr) lv_obj_send_event(old_scr, LV_OBJ_EVENT_SCREEN_UNLOAD_START, NULL);
    if(d->act_scr) lv_obj_send_event(scr, LV_OBJ_EVENT_SCREEN_LOAD_START, NULL);

    d->act_scr = scr;

    if(d->act_scr) lv_obj_send_event(scr, LV_OBJ_EVENT_SCREEN_LOADED, NULL);
    if(d->act_scr) lv_obj_send_event(old_scr, LV_OBJ_EVENT_SCREEN_UNLOADED, NULL);

    lv_obj_invalidate(scr);
}

static void scr_load_anim_start(lv_anim_t * a)
{
    lv_disp_t * d = lv_obj_get_disp(a->var);

    d->prev_scr = lv_scr_act();
    d->act_scr = a->var;

    lv_obj_send_event(d->act_scr, LV_OBJ_EVENT_SCREEN_LOAD_START, NULL);
}

static void opa_scale_anim(void * obj, int32_t v)
{
    lv_obj_set_style_opa(obj, v, 0);
}

static void set_x_anim(void * obj, int32_t v)
{
    lv_obj_set_x(obj, v);
}

static void set_y_anim(void * obj, int32_t v)
{
    lv_obj_set_y(obj, v);
}

static void scr_anim_ready(lv_anim_t * a)
{
    lv_disp_t * d = lv_obj_get_disp(a->var);

    lv_obj_send_event(d->act_scr, LV_OBJ_EVENT_SCREEN_LOADED, NULL);
    lv_obj_send_event(d->prev_scr, LV_OBJ_EVENT_SCREEN_UNLOADED, NULL);

    if(d->prev_scr && d->del_prev) lv_obj_del(d->prev_scr);
    d->prev_scr = NULL;
    d->draw_prev_over_act = false;
    d->scr_to_load = NULL;
    lv_obj_remove_local_style_prop(a->var, LV_STYLE_OPA, 0);
    lv_obj_invalidate(d->act_scr);
}

static bool is_out_anim(lv_scr_load_anim_t anim_type)
{
    return anim_type == LV_SCR_LOAD_ANIM_FADE_OUT  ||
           anim_type == LV_SCR_LOAD_ANIM_OUT_LEFT  ||
           anim_type == LV_SCR_LOAD_ANIM_OUT_RIGHT ||
           anim_type == LV_SCR_LOAD_ANIM_OUT_TOP   ||
           anim_type == LV_SCR_LOAD_ANIM_OUT_BOTTOM;
}
