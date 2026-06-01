#include <zephyr/kernel.h>
#include <nrfx_clock.h>

#include "display.h"
#include "bt_ba.h"
#include "sr_info.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

K_FIFO_DEFINE(fifo_brcast_snk);
K_FIFO_DEFINE(fifo_brcast_src);

static void on_snk_button_pressed(struct brcast_snk_info *sink_info)
{
	if (sink_info == NULL) {
		LOG_WRN("Sink button pressed for empty row");
		return;
	}

	k_fifo_put(&fifo_brcast_snk, sink_info);

	LOG_INF("Sink selected: %s", sink_info->name);
}

static void on_src_button_pressed(struct brcast_src_info *src_info)
{
	if (src_info == NULL) {
		LOG_WRN("Source button pressed for empty row");
		return;
	}

	k_fifo_put(&fifo_brcast_src, src_info);

	LOG_INF("Source selected: %s", src_info->name);
}

static void on_scan_result_sink(struct brcast_snk_info snk_inf)
{
	int ret;
	LOG_INF("Sink scan result: %s", snk_inf.name);
	ret = display_scan_result_snk_submit(snk_inf);
	if (ret != 0) {
		LOG_ERR("Failed to submit scan result (err %d)\n", ret);
	}
}

static void on_scan_result_source(struct brcast_src_info src_info)
{
	int ret;
	LOG_INF("Source scan result: bt='%s' broadcast='%s' id=0x%06x", src_info.name,
		src_info.name, src_info.broadcast_id);

	ret = display_scan_result_src_submit(src_info);
	if (ret != 0) {
		LOG_ERR("Failed to submit scan result (err %d)\n", ret);
	}
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
	const struct display_callbacks display_callbacks = {
		.snk_selected = on_snk_button_pressed,
		.src_selected = on_src_button_pressed,
	};

	set_cpu_to_128mhz();

	/* Initialize display */
	err = display_init(&display_callbacks);
	if (err != 0) {
		LOG_ERR("Display init failed (err %d)\n", err);
		return 0;
	}

	err = bt_ba_init(&bt_callbacks);
	if (err != 0) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	LOG_INF("Broadcast Assistant started");

	err = bt_ba_scan_for_sink_start();
	if (err != 0) {
		LOG_ERR("Failed to start scan (err %d)\n", err);
	} else {
		display_state_set(STATE_SCANNING_FOR_SINK);
	}

	LOG_INF("Scan for sink started");

	while (1) {
		k_sleep(K_MSEC(10));

		struct brcast_snk_info *sink_info = k_fifo_get(&fifo_brcast_snk, K_NO_WAIT);

		if (sink_info != NULL) {
			LOG_INF("Processing selected sink: %s", sink_info->name);
			err = bt_ba_sink_connect(sink_info);
			if (err != 0) {
				LOG_ERR("Failed to connect to sink (err %d)\n", err);
			}

			display_state_set(STATE_SCANNING_FOR_SOURCE);
			err = bt_ba_scan_for_source_start();
			if (err != 0) {
				LOG_ERR("Failed to start scan for source (err %d)\n", err);
			}
		}

		struct brcast_src_info *src_info = k_fifo_get(&fifo_brcast_src, K_NO_WAIT);
		if (src_info != NULL) {
			LOG_INF("Processing selected source: %s", src_info->name);
			err = bt_ba_source_sync_and_transfer(src_info);
			if (err != 0) {
				LOG_ERR("Failed to sync and transfer from source (err %d)\n", err);
			}
			LOG_INF("Source sync and transfer done");
		}
	}
}
