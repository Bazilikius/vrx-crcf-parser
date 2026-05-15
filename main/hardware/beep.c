#include "lcd.h"
#include "beep.h"
#include "SPI.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "freertos/queue.h"
#include <string.h>
#include "hwvers.h"

volatile uint8_t beep_en = 1;
bool is_inited = false;

// ---------------------------------------------------------------------------
// Beep pattern queue
// Each item describes one burst: play the buzzer for on_ms, then silence for
// off_ms, repeated count times.  A count of 1 with off_ms = 0 is a single
// click; count = 2 with off_ms = 100 produces a double-click, etc.
// ---------------------------------------------------------------------------
typedef struct {
    uint16_t on_ms;
    uint16_t off_ms;
    uint8_t  count;
} beep_item_t;

#define BEEP_QUEUE_DEPTH 8
static QueueHandle_t beep_queue = NULL;

static void beep_task(void *param)
{
    beep_item_t item;
    while (1) {
        if (xQueueReceive(beep_queue, &item, portMAX_DELAY) != pdTRUE) continue;
        if (beep_en == 0) continue;  // respect mute setting
        for (uint8_t i = 0; i < item.count; i++) {
            beep_on_off(1);
            vTaskDelay(pdMS_TO_TICKS(item.on_ms));
            beep_on_off(0);
            // Only wait off_ms between repetitions, not after the last one
            if (item.off_ms > 0 && i < (uint8_t)(item.count - 1)) {
                vTaskDelay(pdMS_TO_TICKS(item.off_ms));
            }
        }
    }
}


void PWM_Enable() {
	if(!is_inited) {
		is_inited = true;
		/*
		* 设置LEDC的定时器的配置
		*/
		ledc_timer_config_t ledc_timer_ls = {
			.duty_resolution = LEDC_TIMER_13_BIT, // 设置分辨率,最大为2^13-1
			.freq_hz = 4000,                      // PWM信号频率
			.speed_mode = LEDC_LOW_SPEED_MODE,    // 定时器模式（“高速”或“低速”）
			.timer_num = LEDC_TIMER_0,            // 设置定时器源（0-3）
			.clk_cfg = LEDC_AUTO_CLK,             // 配置LEDC时钟源（这里是自动选择）
		};
		// 初始化ledc的定时器配置
		ledc_timer_config(&ledc_timer_ls);
		/*
		* 通过选择为 LEDC 控制器的每个通道准备单独的配置：
		* - 控制器的通道号
		* - 输出占空比，初始设置为 0
		* - LEDC 连接到的 GPIO 编号
		* - 速度模式，高或低
		* - 为LEDC通道指定定时器
		*   注意: 如果不同通道使用一个定时器，那么这些通道的频率和占空比分辨率将相同
		*/
		ledc_channel_config_t ledc_channel = {
			.channel    = LEDC_CHANNEL_1,
			.duty       = 4096,
			.gpio_num   = Beep_Pin_Num,
			.speed_mode = LEDC_LOW_SPEED_MODE,
			.hpoint     = 0,
			.timer_sel  = LEDC_TIMER_0
		};
		// 初始化ledc的通道
		ledc_channel_config(&ledc_channel);
	} else {
		// 设置占空比
		ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, 4096);
		ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
		ledc_timer_resume(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
	}
}
void PWM_Disable() {
	ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
	ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
}

void Beep_Init()
{

#if Beep_Is_Src == 1
	gpio_set_direction(Beep_Pin_Num, GPIO_MODE_OUTPUT);
#endif
// 	if(beep_en==1) {
// #if Beep_Is_Src == 1
// 		gpio_set_level(Beep_Pin_Num, 1);
// #else
// 		PWM_Enable();
// #endif
// 		//vTaskDelay(200 / portTICK_PERIOD_MS);
// #if Beep_Is_Src == 1
// 		gpio_set_level(Beep_Pin_Num, 0);
// #else
// 		PWM_Disable();
// #endif
//  	}
	beep_queue = xQueueCreate(BEEP_QUEUE_DEPTH, sizeof(beep_item_t));
	if (beep_queue == NULL) {
		assert(false);
		return;
	}
	xTaskCreatePinnedToCore((TaskFunction_t)beep_task,
			"beep_task",
			1024,
			NULL,
			1,
			NULL,
			1);
	beep_turn_on();
}

void beep_set_enable_disable(uint8_t en)
{
	if(en) {
		beep_en=1;
	}
	else {
		beep_en=0;
	}
}


uint16_t beep_get_status()
{
   return beep_en;
}

void beep_on_off(uint8_t on_off)
{
	//if(beep_en==0)
	//	return ;
	if(on_off){
#if Beep_Is_Src == 1
		gpio_set_level(Beep_Pin_Num, 1);
#else
		PWM_Enable();
#endif
	} else {
#if Beep_Is_Src == 1
		gpio_set_level(Beep_Pin_Num, 0);
#else
		PWM_Disable();
#endif
	}
}

// ---------------------------------------------------------------------------
// Enqueue helpers
// ---------------------------------------------------------------------------

static void beep_enqueue(uint16_t on_ms, uint16_t off_ms, uint8_t count)
{
    if (beep_queue == NULL) return;
    beep_item_t item = { .on_ms = on_ms, .off_ms = off_ms, .count = count };
    // Non-blocking send: drop the item if queue is full (avoids blocking callers)
    xQueueSendToBack(beep_queue, &item, 0);
}

/** Single short click — identical behaviour to the old semaphore-based beep. */
void beep_turn_on(void)
{
    beep_enqueue(100, 0, 1);
}

/** Two short clicks — used for diversity antenna switch events. */
void beep_play_double(void)
{
    beep_enqueue(80, 100, 2);
}

/** Three rapid clicks — used for success / bind confirmation. */
void beep_play_triple(void)
{
    beep_enqueue(60, 80, 3);
}

/** One long tone — used for errors and low-signal warnings. */
void beep_play_long(void)
{
    beep_enqueue(450, 0, 1);
}

/** General-purpose pattern for callers that need custom timing. */
void beep_play_pattern(uint16_t on_ms, uint16_t off_ms, uint8_t count)
{
    beep_enqueue(on_ms, off_ms, count);
}

/** Stub kept for API compatibility — active buzzer has fixed frequency. */
void beep_set_tone(uint16_t tone)
{
    (void)tone; // Active buzzer: frequency is determined by internal oscillator
}
