#include <zephyr/kernel.h>
#include <nrfx_clock.h>

#include "display.h"
#include "bt_ba.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

static void on_scan_result_sink(struct scan_recv_info sr_info)
{
	int ret;
	LOG_INF("Sink scan result: %s", sr_info.bt_name);
	ret = display_scan_result_submit(sr_info.bt_name, strlen(sr_info.bt_name));
	if (ret != 0) {
		LOG_ERR("Failed to submit scan result (err %d)\n", ret);
	}
}

static void on_scan_result_source(struct scan_recv_info sr_info)
{
	LOG_INF("Source scan result: bt='%s' broadcast='%s' id=0x%06x", sr_info.bt_name,
		sr_info.broadcast_name, sr_info.broadcast_id);
}

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
	const struct bt_ba_callbacks bt_callbacks = {
		.scan_result_sink = on_scan_result_sink,
		.scan_result_source = on_scan_result_source,
	};

	set_cpu_to_128mhz();

	/* Initialize display */
	err = display_init();
	if (err != 0) {
		LOG_DBG("Display init failed (err %d)\n", err);
		return 0;
	}

	err = bt_ba_init(&bt_callbacks);
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
