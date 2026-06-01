#ifndef __SR_INFO_H__
#define __SR_INFO_H__

#include <zephyr/bluetooth/addr.h>

/* Struct to collect information from scanning
 * for Broadcast Source or Sink
 */
#define NAME_LEN 30

struct brcast_snk_info {
	void *fifo_reserved;
	sys_snode_t node;
	char name[NAME_LEN];
	bt_addr_le_t addr;
	uint64_t last_seen;
	bool update;
};

struct brcast_src_info {
	void *fifo_reserved;
	sys_snode_t node;
	char name[NAME_LEN];
	uint32_t broadcast_id;
	bt_addr_le_t addr;
	uint8_t sid;
	uint16_t pa_interval;
	uint64_t last_seen;
	bool update;
};

enum ba_states {
	STATE_IDLE,
	STATE_SCANNING_FOR_SINK,
	STATE_CONNECTING_TO_SINK,
	STATE_CONNECTED_TO_SINK,
	STATE_SECURITY_CHANGED,
	STATE_DISCOVERING_BASS,
	STATE_DISCOVERED_BASS,
	STATE_READING_RECV_STATES,
	STATE_SCANNING_FOR_SOURCE,
	STATE_PA_SYNCING,
	STATE_ADDING_SOURCE,
};

#endif
