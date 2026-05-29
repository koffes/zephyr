#include <zephyr/kernel.h>
#include <nrfx_clock.h>

#include "display.h"
#include "bt_ba.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

void set_cpu_to_128mhz(void)
{
#if NRFX_CLOCK_ENABLED && (defined(CLOCK_FEATURE_HFCLK_DIVIDE_PRESENT) || NRF_CLOCK_HAS_HFCLK192M)
	int err = nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);
	if (err == 0) {
		LOG_INF("CPU frequency set to 128 MHz\n");
	} else {
		LOG_ERR("Failed to set 128 MHz\n");
	}
#endif
}

int main(void)
{
	int err;

	set_cpu_to_128mhz();

	/* Initialize display */
	err = display_init();
	if (err != 0) {
		LOG_DBG("Display init failed (err %d)\n", err);
		return 0;
	}

	err = bt_ba_init();
	if (err != 0) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	LOG_INF("Broadcast Assistant started");

	err = display_scan_result_submit("TESSST", strlen("TESSST"));
	if (err != 0) {
		LOG_DBG("Failed to submit scan result (err %d)\n", err);
		return 0;
	}

	err = display_scan_result_submit("ABCDEFGHIJKLMONPQRS", strlen("ABCDEFGHIJKLMONPQRS"));
	if (err != 0) {
		LOG_DBG("Failed to submit scan result (err %d)\n", err);
		return 0;
	}

	err = bt_ba_scan_for_sink_start();
	if (err != 0) {
		LOG_ERR("Failed to start scan (err %d)\n", err);
	} else {
		display_state_set(STATE_SCANNING_FOR_SINK);
	}

	LOG_INF("Scan for sink started");

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}
