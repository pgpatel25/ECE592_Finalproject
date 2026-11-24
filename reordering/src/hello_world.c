/* SPDX-License-Identifier: BSD-3-Clause */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dlb.h"

#define CQ_DEPTH 8
#define LDB_EVENT_STATE_ENTRIES (2 * CQ_DEPTH)
#define NUM_LDB_EVENT_STATE_ENTRIES (2 * LDB_EVENT_STATE_ENTRIES)
#define CREDIT_COUNT 8
#define NUM_RECV_RETRIES 5000

static void report_error(const char *operation)
{
	fprintf(stderr, "%s: %s\n", operation, strerror(errno));
}

int main(int argc, char **argv)
{
	dlb_hdl_t dlb = NULL;
	dlb_domain_hdl_t domain = NULL;
	dlb_port_hdl_t tx_port = NULL;
	dlb_port_hdl_t rx_port = NULL;
	dlb_dev_cap_t capabilities = {0};
	dlb_resources_t resources = {0};
	dlb_create_sched_domain_t domain_args = {0};
	dlb_create_ldb_queue_t queue_args = {0};
	dlb_create_port_t port_args = {0};
	dlb_event_t event = {0};
	const char message[] = "Hello, DLB!";
	int device_id = 0;
	int domain_id = -1;
	int queue_id;
	int tx_port_id;
	int rx_port_id;
	int credit_pool_id = -1;
	int ldb_credit_pool_id = -1;
	int dir_credit_pool_id = -1;
	int received = 0;
	int status = 1;
	int i;

	if (argc > 2 || (argc == 2 && sscanf(argv[1], "%d", &device_id) != 1)) {
		fprintf(stderr, "Usage: %s [device-id]\n", argv[0]);
		return 2;
	}

	if (dlb_open(device_id, &dlb) == -1) {
		report_error("dlb_open");
		goto cleanup;
	}
	if (dlb_get_dev_capabilities(dlb, &capabilities) == -1) {
		report_error("dlb_get_dev_capabilities");
		goto cleanup;
	}
	if (dlb_get_num_resources(dlb, &resources) == -1) {
		report_error("dlb_get_num_resources");
		goto cleanup;
	}

	if (resources.num_ldb_queues < 1 || resources.num_ldb_ports < 2 ||
	    resources.max_contiguous_ldb_event_state_entries <
		    NUM_LDB_EVENT_STATE_ENTRIES) {
		fprintf(stderr, "Device %d does not have enough free LDB resources\n",
			device_id);
		goto cleanup;
	}
	if (capabilities.combined_credits) {
		if (resources.num_credits < CREDIT_COUNT ||
		    resources.num_credit_pools < 1) {
			fprintf(stderr, "Device %d does not have enough free credits\n",
				device_id);
			goto cleanup;
		}
	} else if (resources.max_contiguous_ldb_credits < CREDIT_COUNT ||
		   resources.max_contiguous_dir_credits < CREDIT_COUNT ||
		   resources.num_ldb_credit_pools < 1 ||
		   resources.num_dir_credit_pools < 1) {
		fprintf(stderr, "Device %d does not have enough free credit resources\n",
			device_id);
		goto cleanup;
	}

	domain_args.num_ldb_queues = 1;
	domain_args.num_ldb_ports = 2;
	domain_args.num_ldb_event_state_entries = NUM_LDB_EVENT_STATE_ENTRIES;
	if (capabilities.combined_credits) {
		domain_args.num_credits = CREDIT_COUNT;
		domain_args.num_credit_pools = 1;
	} else {
		domain_args.num_ldb_credits = CREDIT_COUNT;
		domain_args.num_dir_credits = CREDIT_COUNT;
		domain_args.num_ldb_credit_pools = 1;
		domain_args.num_dir_credit_pools = 1;
	}

	domain_id = dlb_create_sched_domain(dlb, &domain_args);
	if (domain_id == -1) {
		report_error("dlb_create_sched_domain");
		goto cleanup;
	}
	domain = dlb_attach_sched_domain(dlb, domain_id);
	if (domain == NULL) {
		report_error("dlb_attach_sched_domain");
		goto cleanup;
	}

	if (capabilities.combined_credits) {
		credit_pool_id = dlb_create_credit_pool(domain, CREDIT_COUNT);
		if (credit_pool_id == -1) {
			report_error("dlb_create_credit_pool");
			goto cleanup;
		}
	} else {
		ldb_credit_pool_id =
			dlb_create_ldb_credit_pool(domain, CREDIT_COUNT);
		if (ldb_credit_pool_id == -1) {
			report_error("dlb_create_ldb_credit_pool");
			goto cleanup;
		}
		dir_credit_pool_id =
			dlb_create_dir_credit_pool(domain, CREDIT_COUNT);
		if (dir_credit_pool_id == -1) {
			report_error("dlb_create_dir_credit_pool");
			goto cleanup;
		}
	}

	queue_id = dlb_create_ldb_queue(domain, &queue_args);
	if (queue_id == -1) {
		report_error("dlb_create_ldb_queue");
		goto cleanup;
	}

	port_args.cq_depth = CQ_DEPTH;
	port_args.num_ldb_event_state_entries = LDB_EVENT_STATE_ENTRIES;
	port_args.cos_id = DLB_PORT_COS_ID_ANY;
	if (capabilities.combined_credits) {
		port_args.credit_pool_id = (uint32_t)credit_pool_id;
	} else {
		port_args.ldb_credit_pool_id = (uint32_t)ldb_credit_pool_id;
		port_args.dir_credit_pool_id = (uint32_t)dir_credit_pool_id;
	}

	tx_port_id = dlb_create_ldb_port(domain, &port_args);
	if (tx_port_id == -1) {
		report_error("dlb_create_ldb_port (producer)");
		goto cleanup;
	}
	tx_port = dlb_attach_ldb_port(domain, tx_port_id);
	if (tx_port == NULL) {
		report_error("dlb_attach_ldb_port (producer)");
		goto cleanup;
	}

	rx_port_id = dlb_create_ldb_port(domain, &port_args);
	if (rx_port_id == -1) {
		report_error("dlb_create_ldb_port (consumer)");
		goto cleanup;
	}
	rx_port = dlb_attach_ldb_port(domain, rx_port_id);
	if (rx_port == NULL) {
		report_error("dlb_attach_ldb_port (consumer)");
		goto cleanup;
	}
	if (dlb_link_queue(rx_port, queue_id, 0) == -1) {
		report_error("dlb_link_queue");
		goto cleanup;
	}
	if (dlb_launch_domain_alert_thread(domain, NULL, NULL) == -1) {
		report_error("dlb_launch_domain_alert_thread");
		goto cleanup;
	}
	if (dlb_start_sched_domain(domain) == -1) {
		report_error("dlb_start_sched_domain");
		goto cleanup;
	}

	event.send.queue_id = (uint8_t)queue_id;
	event.send.sched_type = SCHED_UNORDERED;
	event.send.udata64 = (uint64_t)(uintptr_t)message;
	if (dlb_send(tx_port, 1, &event) != 1) {
		report_error("dlb_send");
		goto cleanup;
	}

	for (i = 0; i < NUM_RECV_RETRIES; i++) {
		received = dlb_recv(rx_port, 1, false, &event);
		if (received == -1) {
			report_error("dlb_recv");
			goto cleanup;
		}
		if (received > 0)
			break;
		usleep(1000);
	}
	if (received != 1) {
		fprintf(stderr, "Timed out waiting for the DLB event\n");
		goto cleanup;
	}

	printf("DLB delivered: %s\n",
	       (const char *)(uintptr_t)event.recv.udata64);
	if (dlb_release(rx_port, 1) != 1) {
		report_error("dlb_release");
		goto cleanup;
	}
	status = 0;

cleanup:
	if (rx_port != NULL && dlb_detach_port(rx_port) == -1) {
		report_error("dlb_detach_port (consumer)");
		status = 1;
	}
	if (tx_port != NULL && dlb_detach_port(tx_port) == -1) {
		report_error("dlb_detach_port (producer)");
		status = 1;
	}
	if (domain != NULL && dlb_detach_sched_domain(domain) == -1) {
		report_error("dlb_detach_sched_domain");
		status = 1;
	}
	if (dlb != NULL && domain_id != -1 &&
	    dlb_reset_sched_domain(dlb, domain_id) == -1) {
		report_error("dlb_reset_sched_domain");
		status = 1;
	}
	if (dlb != NULL && dlb_close(dlb) == -1) {
		report_error("dlb_close");
		status = 1;
	}

	return status;
}
