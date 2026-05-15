#pragma once
/**
 * Animation callback wrappers for lv_anim_exec_xcb_t compatibility.
 *
 * lv_anim_exec_xcb_t is defined as  void (*)(void *, int32_t).
 * Many LVGL geometry helpers have incompatible signatures:
 *   - lv_obj_set_x/y/width/height: (lv_obj_t*, lv_coord_t)  — lv_coord_t is
 *     'short int' on this platform, not int32_t.
 *   - lv_obj_opa_cb: (lv_obj_t*, uint8_t)
 *   - lv_bar_set_value: (lv_obj_t*, int32_t, lv_anim_enable_t)  — extra arg
 *
 * Casting them directly with (lv_anim_exec_xcb_t) triggers
 * -Wcast-function-type.  These static inline wrappers adapt each helper to
 * the expected prototype without changing behaviour.
 */

#include "lvgl.h"

static inline void anim_set_x_cb(void *obj, int32_t v)
    { lv_obj_set_x((lv_obj_t *)obj, (lv_coord_t)v); }

static inline void anim_set_y_cb(void *obj, int32_t v)
    { lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)v); }

static inline void anim_set_width_cb(void *obj, int32_t v)
    { lv_obj_set_width((lv_obj_t *)obj, (lv_coord_t)v); }

static inline void anim_set_height_cb(void *obj, int32_t v)
    { lv_obj_set_height((lv_obj_t *)obj, (lv_coord_t)v); }

static inline void anim_opa_cb(void *obj, int32_t v)
    { lv_obj_opa_cb((lv_obj_t *)obj, (uint8_t)v); }

static inline void anim_bar_value_cb(void *obj, int32_t v)
    { lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF); }
