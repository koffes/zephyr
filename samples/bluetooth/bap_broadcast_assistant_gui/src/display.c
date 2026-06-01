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

#include <zephyr/sys/min_heap.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>

#include "sr_info.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(display);

#define BROADCAST_SINKS_MAX 6
#define V_OFFSET_PIXELS     35
#define TOP_BUTTON_WIDTH    100

static lv_obj_t *screen_sinks;
static lv_style_t style_common;

static lv_obj_t *screen_sinks_name[BROADCAST_SINKS_MAX];
static lv_obj_t *screen_sinks_since_seen[BROADCAST_SINKS_MAX];
static lv_obj_t *screen_sinks_btn[BROADCAST_SINKS_MAX];
static lv_obj_t *screen_sinks_scan_run_btn;
static lv_obj_t *screen_sinks_clear_btn;
static lv_obj_t *screen_all_status_label;
;

static lv_style_t style_btn_trans;
static lv_style_t style_btn_default;

static int brcast_snk_name_cmp(const void *a, const void *b)
{
	const struct brcast_snk_info *lhs = a;
	const struct brcast_snk_info *rhs = b;

	return strcmp(lhs->name, rhs->name);
}

static bool brcast_snk_name_match(const void *node, const void *other)
{
	const struct brcast_snk_info *sink = node;
	const char *name = other;

	return strcmp(sink->name, name) == 0;
}

MIN_HEAP_DEFINE_STATIC(brcast_snk_heap, BROADCAST_SINKS_MAX, sizeof(struct brcast_snk_info),
		       __alignof__(struct brcast_snk_info), brcast_snk_name_cmp);

/* 320 pixels * 32 bpp (4 bytes/pixel) worst-case line buffer */
static uint8_t display_clear_line_buf[320 * 4];

static int display_hw_clear_white(const struct device *display_dev)
{
	struct display_capabilities caps;
	struct display_buffer_descriptor desc = {0};
	uint8_t bytes_per_pixel;
	size_t line_size;

	display_get_capabilities(display_dev, &caps);

	bytes_per_pixel = DISPLAY_BITS_PER_PIXEL(caps.current_pixel_format) / 8U;
	if (bytes_per_pixel == 0U || bytes_per_pixel > 4U) {
		LOG_ERR("Unsupported pixel format: 0x%x", caps.current_pixel_format);
		return -ENOTSUP;
	}

	line_size = caps.x_resolution * bytes_per_pixel;
	if (line_size > sizeof(display_clear_line_buf)) {
		LOG_ERR("Clear buffer too small for %ux%u", caps.x_resolution, caps.y_resolution);
		return -ENOMEM;
	}

	memset(display_clear_line_buf, 0xFF, line_size);

	desc.width = caps.x_resolution;
	desc.pitch = caps.x_resolution;
	desc.height = 1U;
	desc.buf_size = line_size;

	for (uint16_t y = 0U; y < caps.y_resolution; y++) {
		int err = display_write(display_dev, 0U, y, &desc, display_clear_line_buf);

		if (err != 0) {
			LOG_ERR("display_write failed on line %u (%d)", y, err);
			return err;
		}
	}

	return 0;
}

static uint8_t since_seen_string_gen(char *buf, uint32_t since_seen_s)
{
	if (since_seen_s < 5) {
		return sprintf(buf, "#00ff00 <5 s#");
	} else if (since_seen_s < 10) {
		return sprintf(buf, "#ff9900 <10 s#");
	} else if (since_seen_s < 30) {
		return sprintf(buf, "#ff9900 <30 s#");
	} else if (since_seen_s <= 60) {
		return sprintf(buf, "#ff0000 <60 s#");
	} else if (since_seen_s <= 3600) {
		return sprintf(buf, "#ff0000 ~%d m#", since_seen_s / 60);
	} else {
		return sprintf(buf, "#ff0000 ~%d h#", since_seen_s / 3600);
	}
}

static void page_sinks_draw(void)
{
	struct brcast_snk_info *sink;
	int row = 0;
	char buf[25] = {'\0'};

	MIN_HEAP_FOREACH(&brcast_snk_heap, sink) {

		// LOG_WRN_RATELIMIT("name: %s, last_seen: %lld", sink->name, sink->last_seen);

		since_seen_string_gen(buf, (uint32_t)(k_uptime_get() - sink->last_seen) / 1000);
		lv_label_set_text(screen_sinks_since_seen[row], buf);
		lv_label_set_text(screen_sinks_name[row], sink->name);
		row++;
	}

	for (; row < BROADCAST_SINKS_MAX; row++) {
		sprintf(buf, "%d -------.", row);
		lv_label_set_text(screen_sinks_name[row], buf);
		lv_label_set_text(screen_sinks_since_seen[row], buf);
	}
}

static void timer_worker(struct k_work *work)
{
	if (lv_scr_act() == screen_sinks) {
		page_sinks_draw();
		/* Keep top action buttons refreshed even when only row labels change. */
		if (screen_sinks_scan_run_btn != NULL) {
			lv_obj_invalidate(screen_sinks_scan_run_btn);
		}
		if (screen_sinks_clear_btn != NULL) {
			lv_obj_invalidate(screen_sinks_clear_btn);
		}
		if (screen_all_status_label != NULL) {
			lv_obj_invalidate(screen_all_status_label);
		}
	} else {
		LOG_ERR("Unknown screen active.");
	}

	lv_timer_handler();
}

K_WORK_DEFINE(timer_work, timer_worker);

static void gui_update_timer_handler(struct k_timer *dummy)
{
	k_work_submit(&timer_work);
};

K_TIMER_DEFINE(gui_update_timer, gui_update_timer_handler, NULL);

int display_scan_result_submit(struct brcast_snk_info sink_info)
{
	int ret;
	uint64_t time_now = k_uptime_get();
	struct brcast_snk_info *brcast_snk_info_loc;
	struct brcast_snk_info removed_item;
	size_t found_idx = 0U;

	/* Update existing device */
	brcast_snk_info_loc =
		min_heap_find(&brcast_snk_heap, brcast_snk_name_match, sink_info.name, &found_idx);
	if (brcast_snk_info_loc != NULL) {
		brcast_snk_info_loc->last_seen = time_now;
		brcast_snk_info_loc->update = true;
		LOG_INF("Updated existing device: %s", sink_info.name);
		return 0;
	}

	ret = min_heap_push(&brcast_snk_heap, &sink_info);
	if (ret == 0) {
		LOG_INF("Added new device: %s", sink_info.name);
		return 0;
	}

	/* Heap full: remove oldest seen sink and insert the new one */
	if (brcast_snk_heap.size > 0U) {
		size_t oldest_idx = 0U;
		uint64_t oldest_time = UINT64_MAX;

		for (size_t i = 0U; i < brcast_snk_heap.size; i++) {
			brcast_snk_info_loc = min_heap_get_element(&brcast_snk_heap, i);
			if (brcast_snk_info_loc->last_seen < oldest_time) {
				oldest_time = brcast_snk_info_loc->last_seen;
				oldest_idx = i;
			}
		}

		if (min_heap_remove(&brcast_snk_heap, oldest_idx, &removed_item) &&
		    min_heap_push(&brcast_snk_heap, &sink_info) == 0) {
			LOG_INF("Replaced oldest device: %s", sink_info.name);
			return 0;
		}

		LOG_ERR("Failed to replace oldest device: %s", sink_info.name);
		return -ENOMEM;
	} else {
		LOG_ERR("No available slot for new device: %s", sink_info.name);
		return -ENOENT;
	}
}

static void btn_sink_select(lv_event_t *event)
{
	// uint32_t device = (uint32_t)event->user_data;
	LOG_INF("Button event:");
}

static void btn_clear(lv_event_t *event)
{
	struct brcast_snk_info removed_item;

	while (min_heap_pop(&brcast_snk_heap, &removed_item)) {
	}

	if (screen_all_status_label != NULL) {
		lv_label_set_text(screen_all_status_label, "Cleared");
	}

	LOG_INF("Clear event: all items removed");
}

static void btn_scan(lv_event_t *event)
{
	if (screen_all_status_label != NULL) {
		lv_label_set_text(screen_all_status_label, "Scanning");
	}

	LOG_INF("Scan start/stop event:");
}

void display_state_set(enum ba_states new_state)
{

	switch (new_state) {
	case STATE_IDLE:
		break;
	case STATE_SCANNING_FOR_SINK:
		lv_label_set_text(screen_all_status_label, "Scan: snk");
		break;
	case STATE_CONNECTING_TO_SINK:
		break;
	case STATE_CONNECTED_TO_SINK:
		break;
	default:
		LOG_ERR("Unknown state: %d", new_state);
	}
	return;
}

int display_init(void)
{
	const struct device *display_dev;
	int ret;

	brcast_snk_heap.size = 0U;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	ret = display_blanking_off(display_dev);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("Failed to turn blanking off (error %d)", ret);
		return 0;
	}

	ret = display_hw_clear_white(display_dev);
	if (ret != 0) {
		LOG_WRN("Hardware clear failed (%d), continuing", ret);
	}

	k_sleep(K_MSEC(100));

	screen_sinks = lv_obj_create(NULL);
	lv_scr_load(screen_sinks);

	lv_style_init(&style_common);
	lv_style_set_bg_color(&style_common, lv_color_white());
	lv_style_set_bg_opa(&style_common, LV_OPA_COVER);
	lv_obj_t *act_scr = lv_scr_act();
	lv_obj_add_style(act_scr, &style_common, LV_STATE_DEFAULT);

	lv_style_set_text_font(&style_common, &lv_font_montserrat_24);
	lv_disp_t *disp = lv_disp_get_default();
	int width = lv_disp_get_hor_res(disp);
	// int height = lv_disp_get_ver_res(disp);

	/* sinks page */

	lv_style_init(&style_btn_default);
	lv_style_set_bg_color(&style_btn_default, lv_color_hex(0x6bbf59));
	lv_style_set_bg_opa(&style_btn_default, LV_OPA_COVER);
	lv_style_set_border_color(&style_btn_default, lv_color_hex(0x1f3a1f));
	lv_style_set_border_width(&style_btn_default, 2);
	lv_style_set_radius(&style_btn_default, 4);

	lv_style_init(&style_btn_trans);
	lv_style_set_bg_opa(&style_btn_trans, LV_OPA_TRANSP);

	for (int i = 0; i < BROADCAST_SINKS_MAX; i++) {
		screen_sinks_btn[i] = lv_btn_create(screen_sinks);
		lv_obj_set_pos(screen_sinks_btn[i], 0, (i * V_OFFSET_PIXELS) + V_OFFSET_PIXELS);
		lv_obj_set_size(screen_sinks_btn[i], 320, V_OFFSET_PIXELS);
		lv_obj_add_style(screen_sinks_btn[i], &style_btn_trans,
				 LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_add_event_cb(screen_sinks_btn[i], btn_sink_select, LV_EVENT_PRESSED,
				    (void *)i);

		screen_sinks_name[i] = lv_label_create(screen_sinks);
		lv_label_set_recolor(screen_sinks_name[i], true);
		lv_obj_align(screen_sinks_name[i], LV_ALIGN_TOP_LEFT, 0,
			     (i * V_OFFSET_PIXELS) + V_OFFSET_PIXELS);

		lv_obj_add_style(screen_sinks_name[i], &style_common, 0);
		lv_label_set_text(screen_sinks_name[i], "-");

		screen_sinks_since_seen[i] = lv_label_create(screen_sinks);
		lv_label_set_recolor(screen_sinks_since_seen[i], true);
		lv_obj_align(screen_sinks_since_seen[i], LV_ALIGN_TOP_RIGHT, 0,
			     (i * V_OFFSET_PIXELS) + V_OFFSET_PIXELS);
		lv_obj_add_style(screen_sinks_since_seen[i], &style_common, 0);
		lv_label_set_text(screen_sinks_since_seen[i], "-");
	}

	screen_sinks_scan_run_btn = lv_btn_create(screen_sinks);
	lv_obj_set_size(screen_sinks_scan_run_btn, TOP_BUTTON_WIDTH, V_OFFSET_PIXELS);
	lv_obj_align(screen_sinks_scan_run_btn, LV_ALIGN_TOP_LEFT, 1, 1);
	lv_obj_add_style(screen_sinks_scan_run_btn, &style_btn_default,
			 LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_move_foreground(screen_sinks_scan_run_btn);
	lv_obj_t *scan_label = lv_label_create(screen_sinks_scan_run_btn);
	lv_label_set_text(scan_label, "Idle");
	lv_obj_set_style_text_color(scan_label, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(scan_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(screen_sinks_scan_run_btn, btn_scan, LV_EVENT_PRESSED, NULL);

	screen_all_status_label = lv_label_create(screen_sinks);
	lv_obj_set_size(screen_all_status_label, width - (2 * TOP_BUTTON_WIDTH), V_OFFSET_PIXELS);
	lv_obj_align(screen_all_status_label, LV_ALIGN_TOP_MID, 0, 0);
	lv_obj_set_style_text_font(screen_all_status_label, &lv_font_montserrat_24,
				   LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_align(screen_all_status_label, LV_TEXT_ALIGN_CENTER,
				    LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(screen_all_status_label, lv_color_black(),
				    LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(screen_all_status_label, LV_OPA_TRANSP,
				LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_move_foreground(screen_all_status_label);
	lv_label_set_text(screen_all_status_label, "Idle");
	lv_obj_invalidate(screen_all_status_label);

	screen_sinks_clear_btn = lv_btn_create(screen_sinks);
	lv_obj_set_size(screen_sinks_clear_btn, TOP_BUTTON_WIDTH, V_OFFSET_PIXELS);
	lv_obj_align(screen_sinks_clear_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
	lv_obj_add_style(screen_sinks_clear_btn, &style_btn_default,
			 LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_move_foreground(screen_sinks_clear_btn);
	lv_obj_t *clear_label = lv_label_create(screen_sinks_clear_btn);
	lv_label_set_text(clear_label, "Clear");
	lv_obj_set_style_text_color(clear_label, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(clear_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_event_cb(screen_sinks_clear_btn, btn_clear, LV_EVENT_PRESSED, NULL);

	lv_obj_move_foreground(screen_all_status_label);

	/* Force a full initial draw so static widgets are visible before any touch input. */
	lv_obj_invalidate(screen_sinks);
	lv_refr_now(NULL);
	lv_timer_handler();
	k_timer_start(&gui_update_timer, K_MSEC(100), K_MSEC(100));

	LOG_INF("Display initialized");

	return 0;
}
