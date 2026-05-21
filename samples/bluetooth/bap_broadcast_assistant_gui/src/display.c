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

static uint32_t count;

int display_init(void)
{
	char count_str[11] = {0};
	const struct device *display_dev;
	lv_obj_t *hello_world_label;
	lv_obj_t *count_label;
	int ret;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	lv_obj_clean(lv_scr_act());

	hello_world_label = lv_label_create(lv_screen_active());

	lv_label_set_text(hello_world_label, "Hello world!");
	lv_obj_align(hello_world_label, LV_ALIGN_CENTER, 0, 0);

	count_label = lv_label_create(lv_screen_active());
	lv_obj_align(count_label, LV_ALIGN_BOTTOM_MID, 0, 0);

	lv_timer_handler();
	ret = display_blanking_off(display_dev);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("Failed to turn blanking off (error %d)", ret);
		return 0;
	}

	while (1) {
		if ((count % 100) == 0U) {
			sprintf(count_str, "%d", count / 100U);
			lv_label_set_text(count_label, count_str);
		}
		lv_timer_handler();
		++count;
		k_sleep(K_MSEC(10));
	}
}
