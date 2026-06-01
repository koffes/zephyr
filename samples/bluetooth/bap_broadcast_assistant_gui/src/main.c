#include <zephyr/kernel.h>
#include <nrfx_clock.h>

#include "display.h"
#include "bt_ba.h"
#include "sr_info.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

static void on_sink_button_pressed(const struct brcast_snk_info *sink_info)
{
	int err;
	if (sink_info == NULL) {
		LOG_WRN("Sink button pressed for empty row");
		return;
	}

	LOG_INF("Sink selected: %s", sink_info->name);

	err = bt_ba_sink_connect(sink_info);
	if (err != 0) {
		LOG_ERR("Failed to connect to sink (err %d)\n", err);
	}
}

static void on_scan_button_pressed(void)
{
	LOG_INF("Scan button pressed");
}

static void on_clear_button_pressed(void)
{
	LOG_INF("Clear button pressed");
}

static void on_scan_result_sink(struct brcast_snk_info snk_inf)
{
	int ret;
	LOG_INF("Sink scan result: %s", snk_inf.name);
	ret = display_scan_result_submit(snk_inf);
	if (ret != 0) {
		LOG_ERR("Failed to submit scan result (err %d)\n", ret);
	}
}

static void on_scan_result_source(struct brcast_src_info src_info)
{
	LOG_INF("Source scan result: bt='%s' broadcast='%s' id=0x%06x", src_info.name,
		src_info.name, src_info.broadcast_id);
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
		.sink_selected = on_sink_button_pressed,
		.scan_pressed = on_scan_button_pressed,
		.clear_pressed = on_clear_button_pressed,
	};

	set_cpu_to_128mhz();

	/* Initialize display */
	err = display_init(&display_callbacks);
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

	struct brcast_snk_info dummy_1 = {0};
	memcpy(dummy_1.name, "Dummy1", sizeof("Dummy1"));

	err = display_scan_result_submit(dummy_1);
	if (err != 0) {
		LOG_DBG("Failed to submit scan result (err %d)\n", err);
		return 0;
	}

	struct brcast_snk_info dummy_2 = {0};
	memcpy(dummy_2.name, "Dummy2", sizeof("Dummy2"));

	err = display_scan_result_submit(dummy_2);
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
