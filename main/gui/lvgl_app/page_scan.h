#ifndef __page_menu_scan_H
#define __page_menu_scan_H



#ifdef __cplusplus
extern "C" {
#endif

    /*********************
    *      INCLUDES
    *********************/
#include "../lvgl/lvgl.h"
    /*********************
    *      DEFINES
    *********************/

    /**********************
    *      TYPEDEFS
    **********************/
    typedef enum
    {
        scan_menu_mode_quick = 0,
        scan_menu_mode_spectrum,
        scan_menu_mode_calib,
    } scan_menu_mode_t;
    /**********************
    * GLOBAL PROTOTYPES
    **********************/
    extern lv_indev_t* indev_keypad;
    void page_scan_create(void);
    void page_scan_create_mode(scan_menu_mode_t mode);
    /**********************
    *      MACROS
    **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif




#endif
