#ifndef BT_BA_H_
#define BT_BA_H_

#include <stdint.h>
#include <zephyr/bluetooth/addr.h>

#include "sr_info.h"

struct bt_ba_callbacks {
	void (*scan_result_sink)(const struct brcast_snk_info info);
	void (*scan_result_source)(const struct brcast_src_info info);
	void (*state_update)(enum ba_states new_state);
};

int bt_ba_sink_connect(const struct brcast_snk_info *info);

int bt_ba_scan_stop(void);

int bt_ba_scan_for_sink_start(void);

int bt_ba_scan_for_source_start(void);

int bt_ba_source_sync_and_transfer(const struct brcast_src_info *info);

int bt_ba_init(const struct bt_ba_callbacks *callbacks);

#endif /* BT_BA_H_ */
