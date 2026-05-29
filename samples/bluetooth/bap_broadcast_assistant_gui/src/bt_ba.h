#ifndef BT_BA_H_
#define BT_BA_H_

#include <stdint.h>
#include <zephyr/bluetooth/addr.h>

/* Struct to collect information from scanning
 * for Broadcast Source or Sink
 */
#define NAME_LEN 30

struct scan_recv_info {
	char bt_name[NAME_LEN];
	char broadcast_name[NAME_LEN];
	bt_addr_le_t addr;
	uint32_t broadcast_id;
	bool has_bass;
	bool has_pacs;
};

struct bt_ba_callbacks {
	void (*scan_result_sink)(const struct scan_recv_info info);
	void (*scan_result_source)(const struct scan_recv_info info);
};

int bt_ba_scan_for_sink_start(void);

int bt_ba_init(const struct bt_ba_callbacks *callbacks);

#endif /* BT_BA_H_ */
