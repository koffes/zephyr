/*
 * Copyright (c) 2024-2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stdint.h>
#include <zephyr/sys/slist.h>

#define NAME_SIZE_MAX 30

struct brcast_snk_info {
	sys_snode_t node;
	char name[NAME_SIZE_MAX];
	uint64_t last_seen;
	bool update;
};

int display_scan_result_submit(char *name, uint32_t name_len);

/**
 * @brief Initialize the ILI9341 display with LVGL
 *
 * @return 0 on success, negative error code on failure
 */
int display_init(void);


#endif /* DISPLAY_H_ */
