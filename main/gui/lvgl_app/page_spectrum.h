#ifndef __page_spectrum_H
#define __page_spectrum_H



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

    /**********************
    * GLOBAL PROTOTYPES
    **********************/
void page_spectrum_create(bool bandx_selection, uint8_t bandx_channel);
void page_spectrum_exit(void);

    /**********************
    *      MACROS
    **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif




#endif
