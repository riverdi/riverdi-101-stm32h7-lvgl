/*********************
 *      INCLUDES
 *********************/

#include "lvgl_port_display.h"
#include "main.h"
#include "ltdc.h"
#include "dma2d.h"
#include "lvgl/src/osal/lv_os_private.h"

//#define DIRECT_MODE

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void disp_flush (lv_display_t *, const lv_area_t *, uint8_t *);

#ifndef DIRECT_MODE
static void disp_flush_complete (DMA2D_HandleTypeDef*);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_display_t * disp;
static lv_thread_sync_t sync;

#ifdef DIRECT_MODE
#define LVGL_BUFFER_1_ADDR_AT_SDRAM	(0xD0000000)
#define LVGL_BUFFER_2_ADDR_AT_SDRAM	(0xD0400000)
#else
#define LVGL_BUFFER_1_ADDR_AT_SDRAM	(0xD01F4000)
#define LVGL_BUFFER_2_ADDR_AT_SDRAM	(0xD03E8000)
#endif

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lvgl_display_init (void)
{
	/* display initialization */

	disp = lv_display_create(MY_DISP_HOR_RES, MY_DISP_VER_RES);
#ifdef DIRECT_MODE
	lv_display_set_buffers(disp, (void*) LVGL_BUFFER_1_ADDR_AT_SDRAM, (void*) LVGL_BUFFER_2_ADDR_AT_SDRAM, MY_DISP_HOR_RES * MY_DISP_VER_RES * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
	HAL_LTDC_SetAddress(&hltdc, (uint32_t)LVGL_BUFFER_2_ADDR_AT_SDRAM, 0);	// start with the second buffer: LVGL will render into the first buffer
#else
	__attribute__((section(".my_ram_d2_data"))) static uint8_t buf1[288*1024];

	lv_display_set_buffers(disp, (void*) buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

	/* interrupt callback for DMA2D transfer */
	hdma2d.XferCpltCallback = disp_flush_complete;
//	lv_display_set_flush_wait_cb(disp, disp_flush_wait);
//	lv_thread_sync_init(&sync);
#endif
	lv_display_set_flush_cb(disp, disp_flush);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void disp_flush (lv_display_t * display,
            const lv_area_t * area,
            uint8_t * px_map)
{
#ifdef DIRECT_MODE
	if (lv_display_flush_is_last(disp)) {
		SCB_CleanInvalidateDCache();
		// wait for VSYNC to avoid tearing
		while (!(LTDC->CDSR & LTDC_CDSR_VSYNCS));
		// swap framebuffers (NOTE: LVGL will swap the buffers in the background, so here we can set the LCD framebuffer to the current LVGL buffer, which has been just completed)
		HAL_LTDC_SetAddress(&hltdc, (uint32_t)(lv_display_get_buf_active(disp)->data), 0);
	}
	lv_display_flush_ready(disp);
#else
  lv_color_format_t cf = lv_display_get_color_format(display);
  uint32_t px_size = lv_color_format_get_size(cf);  // RGB565 = 2

  lv_area_t rot_area = *area;
  lv_display_rotate_area(display, &rot_area);

  int32_t src_w = lv_area_get_width(area);
  int32_t src_h = lv_area_get_height(area);

  uint32_t src_stride = lv_draw_buf_width_to_stride(src_w, cf);
  uint32_t fb_stride  = lv_draw_buf_width_to_stride(MY_DISP_HOR_RES, cf);

  uint8_t * fb = (uint8_t *)hltdc.LayerCfg[0].FBStartAdress;
  fb += (rot_area.y1 * fb_stride) + (rot_area.x1 * px_size);

  SCB_CleanInvalidateDCache();

  lv_display_rotation_t r = lv_display_get_rotation(display);

  if(r == LV_DISPLAY_ROTATION_0) {
      for(int32_t y = 0; y < src_h; y++) {
          lv_memcpy(fb, px_map, src_stride);
          px_map += src_stride;
          fb     += fb_stride;
      }
  } else {
      lv_draw_sw_rotate(px_map, fb, src_w, src_h, src_stride, fb_stride, r, cf);
  }

  lv_display_flush_ready(display);
#endif

}

#ifndef DIRECT_MODE

static void
disp_flush_complete (DMA2D_HandleTypeDef *hdma2d)
{
//	lv_thread_sync_signal_isr(&sync);

  lv_display_flush_ready(disp);
}

#endif
