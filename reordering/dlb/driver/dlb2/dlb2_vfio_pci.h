// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2024 Intel Corporation 

#include <linux/vfio_pci_core.h>

struct dlb2_vfio_migration {
	/* Page aligned migration info size */
	int size;
	void *mstate_mgr;
	struct vfio_device_migration_info *minfo;
	int mdata_size;
	int allocated_cmd_size;

	/* lock for migration state */
	struct mutex state_mutex;
	struct file *filp;
	/* lock for migration data file */
	struct mutex f_lock;
	bool f_activated;
};

struct dlb2_vfio_core_device {
	struct vfio_pci_core_device core_device;
	int id;
	struct pci_dev *parent;

	struct list_head next;
	/* protects migration state */
	struct dlb2_vfio_migration migration;
	struct dlb2_migrate_t mig_state;
};
