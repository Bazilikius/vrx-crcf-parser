#ifndef __PAGE_DIVERSITY_CALIB_H
#define __PAGE_DIVERSITY_CALIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../lvgl/lvgl.h"

extern lv_indev_t* indev_keypad;

void page_diversity_calib_create(void);

#ifdef __cplusplus
}
#endif

#endif // __PAGE_DIVERSITY_CALIB_H
