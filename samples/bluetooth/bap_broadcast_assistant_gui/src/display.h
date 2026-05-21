/*
 * Copyright (c) 2024-2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stdint.h>


/**
 * @brief Initialize the ILI9341 display with LVGL
 *
 * @return 0 on success, negative error code on failure
 */
int display_init(void);


#endif /* DISPLAY_H_ */
