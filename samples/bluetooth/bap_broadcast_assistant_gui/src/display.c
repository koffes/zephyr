/*
 * Copyright (c) 2024-2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display.h"
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

#include <zephyr/sys/slist.h>
#include <lvgl.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

static lv_obj_t *screen_overview;
static lv_style_t style_common;

static lv_obj_t *header_label_1;

static void page_overview_draw(void)
{

	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000);
	char line_buf[310] = {'\0'};

	sprintf(line_buf, LV_SYMBOL_BLUETOOTH "Uptime: %d", uptime_s);
	lv_label_set_text(header_label_1, line_buf);
}

static void timer_worker(struct k_work *work)
{
	if (lv_scr_act() == screen_overview) {
		page_overview_draw();
	} else {
		LOG_ERR("Unknown screen active.");
	}

	lv_task_handler();
	lv_timer_handler();
}

K_WORK_DEFINE(timer_work, timer_worker);

static void gui_update_timer_handler(struct k_timer *dummy)
{
	k_work_submit(&timer_work);
};

K_TIMER_DEFINE(gui_update_timer, gui_update_timer_handler, NULL);

int display_init(void)
{
	const struct device *display_dev;
	int ret;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	screen_overview = lv_obj_create(NULL);
	lv_scr_load(screen_overview);

	lv_style_init(&style_common);
	lv_style_set_text_font(&style_common, &lv_font_montserrat_24);

	/* Overview page */
	header_label_1 = lv_label_create(screen_overview);
	lv_obj_add_style(header_label_1, &style_common, 0);
	lv_obj_align(header_label_1, LV_ALIGN_TOP_LEFT, 0, 0);

	lv_timer_handler();
	ret = display_blanking_off(display_dev);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("Failed to turn blanking off (error %d)", ret);
		return 0;
	}

	k_timer_start(&gui_update_timer, K_MSEC(25), K_MSEC(25));

	return 0;
}
