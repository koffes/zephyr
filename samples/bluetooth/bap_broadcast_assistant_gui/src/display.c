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

#define BROADCAST_SINKS_MAX 5
#define V_OFFSET_PIXELS     30

static lv_obj_t *screen_sinks;
static lv_style_t style_common;

static lv_obj_t *screen_sinks_name[BROADCAST_SINKS_MAX];
static lv_obj_t *screen_sinks_since_seen[BROADCAST_SINKS_MAX];
static lv_obj_t *screen_sinks_btn[BROADCAST_SINKS_MAX];
static lv_obj_t *screen_sinks_scan_run_btn;
static lv_obj_t *screen_sinks_clear_btn;

static lv_style_t style_btn_trans;
static lv_style_t style_btn_default;

static struct brcast_snk_info brcast_snk_info_array[BROADCAST_SINKS_MAX];
#define TIMEOUT_S 5

static uint8_t since_seen_string_gen(char *buf, uint32_t since_seen_s)
{
	if (since_seen_s <= TIMEOUT_S) {
		return sprintf(buf, "#00ff00 ~%d s#", since_seen_s);
	} else if (since_seen_s <= 60) {
		return sprintf(buf, "#ff0000 ~%d s#", since_seen_s);
	} else if (since_seen_s <= 3600) {
		return sprintf(buf, "#ff0000 ~%d m#", since_seen_s / 60);
	} else {
		return sprintf(buf, "#ff0000 ~%d h#", since_seen_s / 3600);
	}
}

static void page_sinks_draw(void)
{
	struct brcast_snk_info *bc_snk_info_loc;

	for (int i = 0; i < BROADCAST_SINKS_MAX; i++) {
		bc_snk_info_loc = &brcast_snk_info_array[i];
		if (!bc_snk_info_loc->update) {
			return;
		}
		if (bc_snk_info_loc->name[0] != '\0') {
			lv_label_set_text(screen_sinks_name[i], bc_snk_info_loc->name);
			char last_seen_buf[32];
			since_seen_string_gen(
				last_seen_buf,
				(uint32_t)(k_uptime_get() - bc_snk_info_loc->last_seen) / 1000);
			lv_label_set_text(screen_sinks_since_seen[i], last_seen_buf);

		} else {
			lv_label_set_text(screen_sinks_name[i], "--");
			lv_label_set_text(screen_sinks_since_seen[i], "--");
		}
	}
}

static void timer_worker(struct k_work *work)
{
	if (lv_scr_act() == screen_sinks) {
		page_sinks_draw();
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

int display_scan_result_submit(char *name, uint32_t name_len)
{
	uint64_t time_now = k_uptime_get();
	struct brcast_snk_info *brcast_snk_info_loc;

	/* Update existing device */
	for (int i = 0; i < BROADCAST_SINKS_MAX; i++) {
		brcast_snk_info_loc = &brcast_snk_info_array[i];
		if (strcmp(name, brcast_snk_info_loc->name) == 0) {
			brcast_snk_info_loc->last_seen = time_now;
			brcast_snk_info_loc->update = true;
			LOG_INF("Updated existing device: %s", name);
			return 0;
		}
	}

	/* Add new device */
	for (int i = 0; i < BROADCAST_SINKS_MAX; i++) {
		brcast_snk_info_loc = &brcast_snk_info_array[i];
		if (brcast_snk_info_loc->name[0] == '\0') {
			memcpy(brcast_snk_info_loc->name, name, name_len);
			brcast_snk_info_loc->last_seen = time_now;
			brcast_snk_info_loc->update = true;
			LOG_INF("Added new device: %s", name);
			return 0;
		}
	}

	/* No available slot for new device. Remove oldest */
	uint64_t oldest_time = UINT64_MAX;
	int oldest_index = -1;
	for (int i = 0; i < BROADCAST_SINKS_MAX; i++) {
		brcast_snk_info_loc = &brcast_snk_info_array[i];
		if (brcast_snk_info_loc->last_seen < oldest_time) {
			oldest_time = brcast_snk_info_loc->last_seen;
			oldest_index = i;
		}
	}

	if (oldest_index >= 0) {
		brcast_snk_info_loc = &brcast_snk_info_array[oldest_index];
		memcpy(brcast_snk_info_loc->name, name, name_len);
		brcast_snk_info_loc->last_seen = time_now;
		brcast_snk_info_loc->update = true;
		LOG_INF("Replaced oldest device: %s", name);
	} else {
		LOG_ERR("No available slot for new device: %s", name);
	}

	return 0;
}

static void btn_sink_select(lv_event_t *event)
{
	// uint32_t device = (uint32_t)event->user_data;
	LOG_INF("Button event:");
}

static void btn_clear(lv_event_t *event)
{
	LOG_INF("Clear event:");
}

static void btn_scan(lv_event_t *event)
{
	LOG_INF("Scan start/stop event:");
}

int display_init(void)
{
	const struct device *display_dev;
	int ret;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	screen_sinks = lv_obj_create(NULL);
	lv_scr_load(screen_sinks);

	lv_style_init(&style_common);
	lv_style_set_bg_color(&style_common, lv_color_white());
	lv_style_set_bg_opa(&style_common, LV_OPA_COVER);
	lv_obj_t *act_scr = lv_scr_act();
	lv_obj_add_style(act_scr, &style_common, LV_STATE_DEFAULT);

	lv_task_handler();
	lv_timer_handler();

	lv_style_set_text_font(&style_common, &lv_font_montserrat_24);
	lv_disp_t *disp = lv_disp_get_default();
	int width = lv_disp_get_hor_res(disp);
	// int height = lv_disp_get_ver_res(disp);

	/* sinks page */

	lv_style_init(&style_btn_trans);
	lv_style_init(&style_btn_default);
	lv_style_set_bg_opa(&style_btn_trans, LV_OPA_TRANSP);
	lv_style_set_bg_color(&style_btn_default, lv_color_hex(0x123456));
	lv_style_set_bg_opa(&style_btn_default, LV_OPA_50);

	screen_sinks_scan_run_btn = lv_btn_create(screen_sinks);
	lv_obj_set_size(screen_sinks_scan_run_btn, 120, V_OFFSET_PIXELS);
	lv_obj_align(screen_sinks_scan_run_btn, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_t *scan_label = lv_label_create(screen_sinks_scan_run_btn);
	lv_label_set_text(scan_label, "Idle");
	lv_obj_align(scan_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(screen_sinks_scan_run_btn, btn_scan, LV_EVENT_PRESSED,
			    NULL); /*Assign a callback to the button, user data can be used to
				      identify the button in the callback*/

	screen_sinks_clear_btn = lv_btn_create(screen_sinks);
	lv_obj_set_size(screen_sinks_clear_btn, 120, V_OFFSET_PIXELS);
	lv_obj_align(screen_sinks_clear_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
	lv_obj_t *clear_label = lv_label_create(screen_sinks_clear_btn);
	lv_label_set_text(clear_label, "Clear");
	lv_obj_align(clear_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(screen_sinks_clear_btn, btn_clear, LV_EVENT_PRESSED,
			    NULL); /*Assign a callback to the button, user data can be used to
				      identify the button in the callback*/

	for (int i = 0; i < BROADCAST_SINKS_MAX; i++) {
		screen_sinks_name[i] = lv_label_create(screen_sinks);
		lv_obj_add_event_cb(screen_sinks_name[i], btn_sink_select, LV_EVENT_PRESSED,
				    NULL); /*Assign a callback to the button*/

		lv_obj_align(screen_sinks_name[i], LV_ALIGN_TOP_LEFT, 0,
			     i * V_OFFSET_PIXELS + V_OFFSET_PIXELS);
		lv_label_set_recolor(screen_sinks_name[i], true);
		lv_obj_add_style(screen_sinks_name[i], &style_common, 0);
		lv_obj_set_width(screen_sinks_name[i], 260);
		lv_label_set_long_mode(screen_sinks_name[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
		lv_label_set_text(screen_sinks_name[i], "-");

		screen_sinks_since_seen[i] = lv_label_create(screen_sinks);
		lv_label_set_recolor(screen_sinks_since_seen[i], true);
		lv_obj_align(screen_sinks_since_seen[i], LV_ALIGN_TOP_RIGHT, 0,
			     i * V_OFFSET_PIXELS + V_OFFSET_PIXELS);
		lv_obj_add_style(screen_sinks_since_seen[i], &style_common, 0);
		lv_label_set_text(screen_sinks_since_seen[i], "-");

		// screen_sinks_btn[i] = lv_btn_create(screen_sinks);
		// lv_obj_set_pos(screen_sinks_btn[i], 0, i * V_OFFSET_PIXELS + V_OFFSET_PIXELS);
		// lv_obj_set_size(screen_sinks_btn[i], width, V_OFFSET_PIXELS);
		// lv_obj_add_style(screen_sinks_btn[i], &style_btn_trans, LV_STATE_DEFAULT);
		// lv_obj_add_event_cb(screen_sinks_btn[i], btn_sink_select, LV_EVENT_PRESSED,
		//		    (void *)i);
	}

	lv_timer_handler();
	lv_task_handler();

	ret = display_blanking_off(display_dev);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("Failed to turn blanking off (error %d)", ret);
		return 0;
	}

	k_timer_start(&gui_update_timer, K_MSEC(25), K_MSEC(25));

	LOG_INF("Display initialized");

	return 0;
}
