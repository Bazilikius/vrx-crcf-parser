#include "lvgl_init.h"
#include "lv_mem_pool.h"
#include "lv_font_cache.h"
#include "page_start.h"

void lvgl_init()
{
	// Initialize memory pools before LVGL init (v1.7.1)
	lv_mem_pool_init();

	lv_init();

	// Initialize font cache after LVGL init (v1.7.1)
	lv_font_cache_init();

    lv_port_disp_init();
    lv_port_indev_init();
	//lv_port_fs_init();
	page_start_create();
}
