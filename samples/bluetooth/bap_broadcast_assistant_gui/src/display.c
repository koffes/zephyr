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
#include "lvgl_zephyr.h"

#include <zephyr/sys/min_heap.h>
#include <zephyr/sys/util.h>
#include <lvgl.h>

#include "sr_info.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(display);

#define DEVICES_MAX         6
#define V_OFFSET_PIXELS     35
#define TOP_BUTTON_WIDTH    100

static lv_obj_t *screen_snks;
static lv_obj_t *screen_srcs;
static lv_style_t style_common;

static lv_obj_t *screen_snks_name[DEVICES_MAX];
static lv_obj_t *screen_snks_since_seen[DEVICES_MAX];
static lv_obj_t *screen_snks_btn[DEVICES_MAX];
static lv_obj_t *screen_srcs_name[DEVICES_MAX];
static lv_obj_t *screen_srcs_since_seen[DEVICES_MAX];
static lv_obj_t *screen_srcs_btn[DEVICES_MAX];

static lv_obj_t *screen_snks_status_label;
static lv_obj_t *screen_srcs_status_label;
static lv_obj_t *screen_all_status_label;

static lv_style_t style_btn_trans;
static lv_style_t style_btn_default;
static struct display_callbacks app_callbacks;
static const struct device *display_dev_ref;

static int name_cmp(const void *a, const void *b)
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

static bool brcast_src_name_match(const void *node, const void *other)
{
	const struct brcast_src_info *src = node;
	const char *name = other;

	return strcmp(src->name, name) == 0;
}

MIN_HEAP_DEFINE_STATIC(brcast_snk_heap, DEVICES_MAX, sizeof(struct brcast_snk_info),
		       __alignof__(struct brcast_snk_info), name_cmp);

MIN_HEAP_DEFINE_STATIC(brcast_src_heap, DEVICES_MAX, sizeof(struct brcast_src_info),
		       __alignof__(struct brcast_src_info), name_cmp);

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

static void page_snks_draw(void)
{
	struct brcast_snk_info *sink;
	int row = 0;
	char buf[25] = {'\0'};

	MIN_HEAP_FOREACH(&brcast_snk_heap, sink) {

		// LOG_WRN_RATELIMIT("name: %s, last_seen: %lld", sink->name, sink->last_seen);

		since_seen_string_gen(buf, (uint32_t)(k_uptime_get() - sink->last_seen) / 1000);
		lv_label_set_text(screen_snks_since_seen[row], buf);
		lv_label_set_text(screen_snks_name[row], sink->name);
		row++;
	}

	for (; row < DEVICES_MAX; row++) {
		sprintf(buf, "----------------");
		lv_label_set_text(screen_snks_name[row], buf);
		sprintf(buf, "------");
		lv_label_set_text(screen_snks_since_seen[row], buf);
	}
}

static void page_srcs_draw(void)
{
	struct brcast_src_info *source;
	int row = 0;
	char buf[25] = {'\0'};

	MIN_HEAP_FOREACH(&brcast_src_heap, source) {

		LOG_WRN_RATELIMIT("name: %s, last_seen: %lld", source->name, source->last_seen);

		since_seen_string_gen(buf, (uint32_t)(k_uptime_get() - source->last_seen) / 1000);
		lv_label_set_text(screen_srcs_since_seen[row], buf);
		lv_label_set_text(screen_srcs_name[row], source->name);
		row++;
	}

	for (; row < DEVICES_MAX; row++) {
		sprintf(buf, "***************");
		lv_label_set_text(screen_srcs_name[row], buf);
		sprintf(buf, "*******");
		lv_label_set_text(screen_srcs_since_seen[row], buf);
	}
}

static void timer_worker(struct k_work *work)
{
	lvgl_lock();
	if (lv_scr_act() == screen_snks) {
		page_snks_draw();
		lv_obj_invalidate(screen_all_status_label);

	} else if (lv_scr_act() == screen_srcs) {
		page_srcs_draw();
		lv_obj_invalidate(screen_all_status_label);
	} else {
		LOG_ERR("Unknown screen active.");
	}

	lv_timer_handler();
	lvgl_unlock();
}

K_WORK_DEFINE(timer_work, timer_worker);

static void gui_update_timer_handler(struct k_timer *dummy)
{
	k_work_submit(&timer_work);
};

K_TIMER_DEFINE(gui_update_timer, gui_update_timer_handler, NULL);

int display_scan_result_snk_submit(struct brcast_snk_info snk_info)
{
	int ret;
	uint64_t time_now = k_uptime_get();
	struct brcast_snk_info *brcast_snk_info_loc;
	struct brcast_snk_info removed_item;
	size_t found_idx = 0U;

	/* Update existing device */
	brcast_snk_info_loc =
		min_heap_find(&brcast_snk_heap, brcast_snk_name_match, snk_info.name, &found_idx);
	if (brcast_snk_info_loc != NULL) {
		brcast_snk_info_loc->last_seen = time_now;
		brcast_snk_info_loc->update = true;
		LOG_DBG("Updated existing device: %s", snk_info.name);
		return 0;
	}

	ret = min_heap_push(&brcast_snk_heap, &snk_info);
	if (ret == 0) {
		LOG_INF("Added new device: %s", snk_info.name);
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
		    min_heap_push(&brcast_snk_heap, &snk_info) == 0) {
			LOG_INF("Replaced oldest device: %s", snk_info.name);
			return 0;
		}

		LOG_ERR("Failed to replace oldest device: %s", snk_info.name);
		return -ENOMEM;
	} else {
		LOG_ERR("No available slot for new device: %s", snk_info.name);
		return -ENOENT;
	}
}

int display_scan_result_src_submit(struct brcast_src_info src_info)
{
	int ret;
	uint64_t time_now = k_uptime_get();
	struct brcast_src_info *brcast_src_info_loc;
	struct brcast_src_info removed_item;
	size_t found_idx = 0U;

	/* Update existing device */
	brcast_src_info_loc =
		min_heap_find(&brcast_src_heap, brcast_src_name_match, src_info.name, &found_idx);
	if (brcast_src_info_loc != NULL) {
		brcast_src_info_loc->last_seen = time_now;
		brcast_src_info_loc->update = true;
		LOG_DBG("Updated existing device: %s", src_info.name);
		return 0;
	}

	ret = min_heap_push(&brcast_src_heap, &src_info);
	if (ret == 0) {
		LOG_INF("Added new device: %s", src_info.name);
		return 0;
	}

	/* Heap full: remove oldest seen source and insert the new one */
	if (brcast_src_heap.size > 0U) {
		size_t oldest_idx = 0U;
		uint64_t oldest_time = UINT64_MAX;

		for (size_t i = 0U; i < brcast_src_heap.size; i++) {
			brcast_src_info_loc = min_heap_get_element(&brcast_src_heap, i);
			if (brcast_src_info_loc->last_seen < oldest_time) {
				oldest_time = brcast_src_info_loc->last_seen;
				oldest_idx = i;
			}
		}

		if (min_heap_remove(&brcast_src_heap, oldest_idx, &removed_item) &&
		    min_heap_push(&brcast_src_heap, &src_info) == 0) {
			LOG_INF("Replaced oldest device: %s", src_info.name);
			return 0;
		}

		LOG_ERR("Failed to replace oldest device: %s", src_info.name);
		return -ENOMEM;
	} else {
		LOG_ERR("No available slot for new device: %s", src_info.name);
		return -ENOENT;
	}
}

static struct brcast_snk_info *sink_from_button_index(uint8_t sink_index)
{
	if (sink_index >= brcast_snk_heap.size) {
		return NULL;
	}

	return min_heap_get_element(&brcast_snk_heap, sink_index);
}

static struct brcast_src_info *src_from_button_index(uint8_t src_index)
{
	if (src_index >= brcast_src_heap.size) {
		return NULL;
	}

	return min_heap_get_element(&brcast_src_heap, src_index);
}

static void btn_sink_select(lv_event_t *event)
{
	__ASSERT(app_callbacks.snk_selected != NULL, "Sink selected callback is not set");
	int device = (int)(uintptr_t)lv_event_get_user_data(event);
	struct brcast_snk_info *sink_info = sink_from_button_index((uint8_t)device);
	LOG_INF("Button event: %d", device);

	app_callbacks.snk_selected(sink_info);
}

static void btn_src_select(lv_event_t *event)
{
	__ASSERT(app_callbacks.src_selected != NULL, "Source selected callback is not set");
	int device = (int)(uintptr_t)lv_event_get_user_data(event);
	struct brcast_src_info *src_info = src_from_button_index((uint8_t)device);
	LOG_INF("Button event: %d", device);

	app_callbacks.src_selected(src_info);
}

static void sinks_screen_set(void)
{

	display_hw_clear_white(display_dev_ref);

	lv_scr_load(screen_snks);
	page_snks_draw();
	lv_obj_invalidate(screen_snks);
	for (int i = 0; i < DEVICES_MAX; i++) {
		lv_obj_invalidate(screen_snks_name[i]);
		lv_obj_invalidate(screen_snks_since_seen[i]);
		lv_obj_invalidate(screen_snks_btn[i]);

		lv_refr_now(NULL);
	}
}

static void sources_screen_set(void)
{

	display_hw_clear_white(display_dev_ref);

	lv_scr_load(screen_srcs);
	page_srcs_draw();
	lv_obj_invalidate(screen_srcs);
	for (int i = 0; i < DEVICES_MAX; i++) {
		lv_obj_invalidate(screen_srcs_name[i]);
		lv_obj_invalidate(screen_srcs_since_seen[i]);
		lv_obj_invalidate(screen_srcs_btn[i]);
	}

	lv_refr_now(NULL);
}

void display_state_set(enum ba_states new_state)
{
	lvgl_lock();

	switch (new_state) {
	case STATE_IDLE:
		sinks_screen_set();
		screen_all_status_label = screen_snks_status_label;
		LOG_INF("Switched to sink screen");
		break;
	case STATE_SCANNING_FOR_SINK:

		lv_label_set_text(screen_all_status_label, "Scan: snk");

		break;
	case STATE_SCANNING_FOR_SOURCE:
		sources_screen_set();
		screen_all_status_label = screen_srcs_status_label;

		lv_label_set_text(screen_all_status_label, "Scan: src");

		LOG_INF("Switched to source screen");
		break;
	default:
		LOG_ERR("Unknown state: %d", new_state);
	}

	lvgl_unlock();
	return;
}

int display_init(const struct display_callbacks *callbacks)
{
	const struct device *display_dev;
	int ret;

	__ASSERT(callbacks != NULL, "Display callbacks must be provided");

	lvgl_lock();
	app_callbacks = *callbacks;

	brcast_snk_heap.size = 0U;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}
	display_dev_ref = display_dev;

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

	screen_snks = lv_obj_create(NULL);
	screen_srcs = lv_obj_create(NULL);
	lv_scr_load(screen_snks);

	lv_style_init(&style_common);
	lv_style_set_bg_color(&style_common, lv_color_white());
	lv_style_set_bg_opa(&style_common, LV_OPA_COVER);
	lv_obj_t *act_scr = lv_scr_act();
	lv_obj_add_style(act_scr, &style_common, LV_STATE_DEFAULT);

	lv_style_set_text_font(&style_common, &lv_font_montserrat_24);

	/* Give source screen its own opaque white background */
	lv_obj_set_style_bg_color(screen_srcs, lv_color_white(), LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(screen_srcs, LV_OPA_COVER, LV_STATE_DEFAULT);
	lv_disp_t *disp = lv_disp_get_default();
	int width = lv_disp_get_hor_res(disp);
	// int height = lv_disp_get_ver_res(disp);

	/* snks page */

	lv_style_init(&style_btn_default);
	lv_style_set_bg_color(&style_btn_default, lv_color_hex(0x6bbf59));
	lv_style_set_bg_opa(&style_btn_default, LV_OPA_COVER);
	lv_style_set_border_color(&style_btn_default, lv_color_hex(0x1f3a1f));
	lv_style_set_border_width(&style_btn_default, 2);
	lv_style_set_radius(&style_btn_default, 4);

	lv_style_init(&style_btn_trans);
	lv_style_set_bg_opa(&style_btn_trans, LV_OPA_TRANSP);

	for (int i = 0; i < DEVICES_MAX; i++) {
		screen_snks_btn[i] = lv_btn_create(screen_snks);
		lv_obj_set_pos(screen_snks_btn[i], 0, (i * V_OFFSET_PIXELS) + V_OFFSET_PIXELS);
		lv_obj_set_size(screen_snks_btn[i], 320, V_OFFSET_PIXELS);
		lv_obj_add_style(screen_snks_btn[i], &style_btn_trans,
				 LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_add_event_cb(screen_snks_btn[i], btn_sink_select, LV_EVENT_PRESSED,
				    (void *)(uintptr_t)i);

		screen_srcs_btn[i] = lv_btn_create(screen_srcs);
		lv_obj_set_pos(screen_srcs_btn[i], 0, (i * V_OFFSET_PIXELS) + V_OFFSET_PIXELS);
		lv_obj_set_size(screen_srcs_btn[i], 320, V_OFFSET_PIXELS);
		lv_obj_add_style(screen_srcs_btn[i], &style_btn_trans,
				 LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_add_event_cb(screen_srcs_btn[i], btn_src_select, LV_EVENT_PRESSED,
				    (void *)(uintptr_t)i);

		screen_snks_name[i] = lv_label_create(screen_snks);
		lv_label_set_recolor(screen_snks_name[i], true);
		lv_obj_align(screen_snks_name[i], LV_ALIGN_TOP_LEFT, 0,
			     (i * V_OFFSET_PIXELS) + V_OFFSET_PIXELS);

		screen_srcs_name[i] = lv_label_create(screen_srcs);
		lv_label_set_recolor(screen_srcs_name[i], true);
		lv_obj_align(screen_srcs_name[i], LV_ALIGN_TOP_LEFT, 0,
			     (i * V_OFFSET_PIXELS) + V_OFFSET_PIXELS);

		lv_obj_add_style(screen_srcs_name[i], &style_common, 0);
		lv_label_set_text(screen_srcs_name[i], "-");

		screen_srcs_since_seen[i] = lv_label_create(screen_srcs);
		lv_label_set_recolor(screen_srcs_since_seen[i], true);
		lv_obj_align(screen_srcs_since_seen[i], LV_ALIGN_TOP_RIGHT, 0,
			     (i * V_OFFSET_PIXELS) + V_OFFSET_PIXELS);
		lv_obj_add_style(screen_srcs_since_seen[i], &style_common, 0);
		lv_label_set_text(screen_srcs_since_seen[i], "-");

		screen_snks_since_seen[i] = lv_label_create(screen_snks);
		lv_label_set_recolor(screen_snks_since_seen[i], true);
		lv_obj_align(screen_snks_since_seen[i], LV_ALIGN_TOP_RIGHT, 0,
			     (i * V_OFFSET_PIXELS) + V_OFFSET_PIXELS);
		lv_obj_add_style(screen_snks_since_seen[i], &style_common, 0);
		lv_label_set_text(screen_snks_since_seen[i], "-");
	}

	screen_snks_status_label = lv_label_create(screen_snks);
	lv_obj_set_size(screen_snks_status_label, width - (2 * TOP_BUTTON_WIDTH), V_OFFSET_PIXELS);
	lv_obj_align(screen_snks_status_label, LV_ALIGN_TOP_MID, 3, 0);
	lv_obj_set_style_text_font(screen_snks_status_label, &lv_font_montserrat_24,
				   LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_align(screen_snks_status_label, LV_TEXT_ALIGN_CENTER,
				    LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(screen_snks_status_label, lv_color_black(),
				    LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(screen_snks_status_label, LV_OPA_TRANSP,
				LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_move_foreground(screen_snks_status_label);
	lv_label_set_text(screen_snks_status_label, "Idle");
	lv_obj_invalidate(screen_snks_status_label);

	screen_srcs_status_label = lv_label_create(screen_srcs);
	lv_obj_set_size(screen_srcs_status_label, width - (2 * TOP_BUTTON_WIDTH), V_OFFSET_PIXELS);
	lv_obj_align(screen_srcs_status_label, LV_ALIGN_TOP_MID, 3, 0);
	lv_obj_set_style_text_font(screen_srcs_status_label, &lv_font_montserrat_24,
				   LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_align(screen_srcs_status_label, LV_TEXT_ALIGN_CENTER,
				    LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(screen_srcs_status_label, lv_color_black(),
				    LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(screen_srcs_status_label, LV_OPA_TRANSP,
				LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_move_foreground(screen_srcs_status_label);
	lv_label_set_text(screen_srcs_status_label, "Idle");

	screen_all_status_label = screen_snks_status_label;

	lv_obj_move_foreground(screen_snks_status_label);

	/* Force a full initial draw so static widgets are visible before any touch input. */

	sinks_screen_set();

	lvgl_unlock();
	k_timer_start(&gui_update_timer, K_MSEC(100), K_MSEC(100));

	LOG_INF("Display initialized");

	return 0;
}
