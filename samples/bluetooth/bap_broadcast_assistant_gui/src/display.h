/*
 * Copyright (c) 2024-2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stdint.h>
#include <zephyr/sys/slist.h>
#include "sr_info.h"

#define NAME_SIZE_MAX 30

struct display_callbacks {
	void (*sink_selected)(struct brcast_snk_info *sink_info);
	void (*scan_pressed)(void);
	void (*clear_pressed)(void);
};

void display_state_set(enum ba_states new_state);

int display_scan_result_submit(struct brcast_snk_info sink_info);

/**
 * @brief Initialize the ILI9341 display with LVGL

 * @param callbacks Optional display button callbacks
 *
 * @return 0 on success, negative error code on failure
 */
int display_init(const struct display_callbacks *callbacks);


#endif /* DISPLAY_H_ */
