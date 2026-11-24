// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024 Intel Corporation */

#include <linux/version.h>
#include <linux/vmalloc.h>

#if KERNEL_VERSION(6, 2, 0) <= LINUX_VERSION_CODE
#include <linux/anon_inodes.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vfio_pci_core.h>
#include "base/dlb2_hw_types.h"
#include "dlb2_main.h"
#include "dlb2_vdcm.h"
#include "base/dlb2_resource.h"
#include "base/dlb2_mbox.h"
#include "dlb2_vfio_pci.h"

static int dlb2_vfio_pci_migration_init(struct dlb2_vfio_core_device *vdev, int cmd_size)
{
	struct vfio_device_migration_info *minfo;
	struct dlb2_migration_state *m_state;
	u32 total_state_size;
	struct dlb2 *dlb2;

	dlb2 = pci_get_drvdata(vdev->parent);

	total_state_size = cmd_size + sizeof(*m_state);

	printk("%s, cmd_size = %d\n", __func__, cmd_size);

	vdev->migration.size = ALIGN(sizeof(*minfo) + total_state_size, PAGE_SIZE);
	minfo = vzalloc(vdev->migration.size);
	if (!minfo) {
		printk("%s: vzalloc failed\n", __func__);
		return -ENOMEM;
	}

	/* Set DLB migration state space */
	vdev->mig_state.src_vm_state  = (struct dlb2_migration_state *)((u8 *)minfo +
						sizeof(*minfo) + cmd_size);
	vdev->mig_state.dst_vm_state  = (struct dlb2_migration_state *)((u8 *)minfo +
						sizeof(*minfo) + cmd_size);

	minfo->data_offset = sizeof(*minfo);
	minfo->data_size = total_state_size;
	vdev->migration.minfo = minfo;
	vdev->migration.mstate_mgr = NULL;
	vdev->migration.mdata_size = 0;
	vdev->migration.allocated_cmd_size = cmd_size;

//	minfo->device_state = VFIO_DEVICE_STATE_RUNNING;

	return 0;
}

static int dlb2_vfio_pci_migration_destory(struct dlb2_vfio_core_device *vdev)
{
	struct vfio_device_migration_info *minfo;

	minfo = vdev->migration.minfo;
	if (!minfo)
		return 0;

	vfree(minfo);

	vdev->migration.size = 0;
	vdev->mig_state.src_vm_state  = NULL;
	vdev->mig_state.dst_vm_state  = NULL;

	vdev->migration.minfo = NULL;
	vdev->migration.mstate_mgr = NULL;
	vdev->migration.mdata_size = 0;
	vdev->migration.allocated_cmd_size = 0;

	return 0;
}

static int dlb2_vfio_pci_open_device(struct vfio_device *core_vdev)
{
	struct dlb2_vfio_core_device *dlb2_vdev =
		container_of(core_vdev, struct dlb2_vfio_core_device,
			     core_device.vdev);
	struct vfio_pci_core_device *vdev = &dlb2_vdev->core_device;
	int ret;

	ret = vfio_pci_core_enable(vdev);
	if (ret)
		return ret;

	dlb2_vdev->migration.mdata_size = 0;
	memset(dlb2_vdev->mig_state.src_vm_state, 0x0, sizeof(struct dlb2_migration_state));
	dlb2_vdev->migration.minfo->device_state = VFIO_DEVICE_STATE_RUNNING;

	vfio_pci_core_finish_enable(vdev);

	return 0;
}

static void dlb2_vfio_disable_fd(struct dlb2_vfio_migration *mig)
{
	mutex_lock(&mig->f_lock);
	mig->f_activated = false;
	if (mig->filp)
		mig->filp->f_pos = 0;
	mutex_unlock(&mig->f_lock);
}

static void dlb2_vfio_disable_fds(struct dlb2_vfio_core_device *dlb2_vdev)
{
	if (!dlb2_vdev->migration.filp)
		return;
	dlb2_vfio_disable_fd(&dlb2_vdev->migration);
	fput(dlb2_vdev->migration.filp);
	dlb2_vdev->migration.filp = NULL;
}

static void dlb2_vfio_pci_close_device(struct vfio_device *core_vdev)
{
	struct dlb2_vfio_core_device *dlb2_vdev;
       
	dlb2_vdev = container_of(core_vdev, struct dlb2_vfio_core_device, core_device.vdev);

	dlb2_vfio_disable_fds(dlb2_vdev);
	vfio_pci_core_close_device(core_vdev);
}

#if(0)
/* Reuse the structure from vfio.h in kernel 5.15.
 * It is still defined in vfio.h, but will be deprecated.
 * Once it happens, we need to define our own structure.
 */
struct vfio_device_migration_info {
	__u32 device_state;         /* VFIO device state */
	__u32 reserved;
	__u64 pending_bytes;
	__u64 data_offset;
	__u64 data_size;
};
#endif

static int dlb2_vfio_pci_mig_release(struct inode *inode,
				     struct file *filp)
{
	struct dlb2_vfio_core_device *dlb2_vdev = filp->private_data;
	struct dlb2_vfio_migration *mig = &dlb2_vdev->migration;

	mutex_lock(&mig->f_lock);
	mig->f_activated = 0;
	mutex_unlock(&mig->f_lock);
	return 0;
}

static ssize_t dlb2_vfio_pci_mig_write(struct file *filp,
				   const char __user *buf,
				   size_t len, loff_t *pos)
{
	struct dlb2_vfio_core_device *dlb2_vdev = filp->private_data;
	struct dlb2_vfio_migration *mig = &dlb2_vdev->migration;
	u8 *state_data = (u8 *)mig->minfo + mig->minfo->data_offset;
	int state_size = mig->minfo->data_size;
	struct device *dev;
	ssize_t ret = 0;
	loff_t end;

	dev = &dlb2_vdev->core_device.pdev->dev;

	if (pos)
		return -ESPIPE;
	pos = &filp->f_pos;

	if (*pos < 0 ||
	    check_add_overflow((loff_t)len, *pos, &end) ||
	    end > state_size) {
		dev_err(dev, "%s: write state pos %lld with len %lu out of range %d.\n",
			__func__, *pos, len, state_size);
		return -EINVAL;
	}
	mutex_lock(&mig->f_lock);
	if (!mig->f_activated) {
		dev_err(dev, "%s: mig file is not activated.\n",
			__func__);
		ret = -ENODEV;
		goto out;
	}

	if (copy_from_user(state_data + *pos, buf, len)) {
		ret = -EFAULT;
		goto out;
	}

	*pos += len;
	ret = len;
out:
	mutex_unlock(&mig->f_lock);
	return ret;
}

static ssize_t dlb2_vfio_pci_mig_read(struct file *filp, char __user *buf,
				   size_t len, loff_t *pos)
{
	struct dlb2_vfio_core_device *dlb2_vdev = filp->private_data;
	struct dlb2_vfio_migration *mig = &dlb2_vdev->migration;
	u8 *state_data = (u8 *)mig->minfo + mig->minfo->data_offset;
	int state_size = mig->minfo->data_size;
	struct device *dev;
	ssize_t ret = 0;

	dev = &dlb2_vdev->core_device.pdev->dev;

	if (pos)
		return -ESPIPE;

	pos = &filp->f_pos;

	mutex_lock(&mig->f_lock);

	if (!mig->f_activated) {
		dev_err(dev, "%s: mig file is not activated.\n",
			__func__);
		ret = -ENODEV;
		goto out;
	}

	if (*pos < 0 || *pos > state_size) {
		dev_err(dev, "%s: read state pos %lld out of range %d.\n",
			__func__, *pos, state_size);
		ret = -EINVAL;
		goto out;
	}

	len = min_t(size_t, state_size - *pos, len);
	if (!len)
		goto out;

	if (copy_to_user(buf, state_data + *pos, len)) {
		ret = -EFAULT;
		goto out;
	}
	*pos += len;
	ret = len;
out:
	mutex_unlock(&mig->f_lock);
	return ret;
}

static struct file *
dlb2_vfio_pci_get_mig_file(struct dlb2_vfio_migration *mig,
		       struct dlb2_vfio_core_device *dlb2_vdev,
		       const struct file_operations *fops,
		       int flags)
{
	struct device *dev;

	dev = &dlb2_vdev->core_device.pdev->dev;

	dev_dbg(dev, "%s: filp is %p\n",
		 __func__, mig->filp);
	mig->filp = anon_inode_getfile("dlb2_vfio__mig", fops, dlb2_vdev, flags);
	if (IS_ERR(mig->filp)) {
		dev_err(dev, "%s: failed to getfile\n",
			__func__);
		return NULL;
	}
	get_file(mig->filp);
	stream_open(mig->filp->f_inode, mig->filp);
	mig->f_activated = 1;

	return mig->filp;
}

static int
dlb2_vfio_pci_put_mig_file(struct dlb2_vfio_migration *mig,
		       struct dlb2_vfio_core_device *dlb2_vdev)
{
	struct device *dev;

	dev = &dlb2_vdev->core_device.pdev->dev;
	dev_dbg(dev, "%s: filp is %p\n",
		 __func__, mig->filp);
	if (mig->filp) {
		mig->f_activated = 0;
		fput(mig->filp);
		mig->filp = NULL;
	}
	return 0;
}

static const struct file_operations dlb2_vfio_pci_save_fops = {
	.owner = THIS_MODULE,
	.read = dlb2_vfio_pci_mig_read,
	.release = dlb2_vfio_pci_mig_release,
};

static const struct file_operations dlb2_vfio_pci_resume_fops = {
	.owner = THIS_MODULE,
	.write = dlb2_vfio_pci_mig_write,
	.release = dlb2_vfio_pci_mig_release,
};

static int dlb2_vdev_migration_stop(struct dlb2_vfio_core_device *dlb2_vdev, u8 *data, u32 data_size)
{
	int ret = 0;
	struct dlb2 *dlb2;

	dlb2 = pci_get_drvdata(dlb2_vdev->parent);

	if (data) {
		dlb2_lm_pause_device(&dlb2->hw, 1, dlb2_vdev->id, dlb2_vdev->mig_state.src_vm_state);

		ret = data_size;
	}

	return ret;
}

static int dlb2_vdev_migration_resume(struct dlb2_vfio_core_device *dlb2_vdev, u8 *data, u32 data_size)
{
	u8 mbox_data[DLB2_VF2PF_REQ_BYTES];
	uint32_t cmd_data_size;
	struct device *dev;
	struct dlb2 *dlb2;
	int i;

	dev = &dlb2_vdev->core_device.pdev->dev;

	dlb2 = pci_get_drvdata(dlb2_vdev->parent);

	if (data) {
		cmd_data_size = *((uint32_t *)data);
		dev_info(dev, "%s: total data size = %d, cmd_data_size = %d\n",
			 __func__, data_size, cmd_data_size);

		/* cmd_data_size comes from the untrusted migration blob; bound it against
		 * the actual buffer size before walking it.
		 */
		if (data_size < DLB2_LM_XMIT_CMD_SIZE_SIZE ||
		    cmd_data_size > data_size - DLB2_LM_XMIT_CMD_SIZE_SIZE) {
			dev_err(dev, "%s: invalid cmd_data_size %d for data_size %d\n",
				__func__, cmd_data_size, data_size);
			return -EINVAL;
		}

		data += DLB2_LM_XMIT_CMD_SIZE_SIZE;
		for (i = 0; i < cmd_data_size / DLB2_LM_CMD_SAVE_DATA_SIZE; i++) {
			u8 *ptr = data + i * DLB2_LM_CMD_SAVE_DATA_SIZE;
			u32 type = DLB2_MBOX_CMD_TYPE(ptr);

			dev_info(dev, "%s: resuming  cmd = %s, i= %d, data_size = %d \n",
				 __func__,
				 type < NUM_DLB2_MBOX_CMD_TYPES ?
					dlb2_mbox_cmd_type_strings[type] : "(invalid)",
				 i, data_size);
			memcpy(mbox_data, ptr, DLB2_LM_CMD_SAVE_DATA_SIZE);
			dlb2_handle_migration_cmds(dlb2, dlb2_vdev->id, mbox_data);
		}

		dlb2_lm_restore_device(&dlb2->hw, 1, dlb2_vdev->id, dlb2_vdev->mig_state.dst_vm_state);
	}

	return data_size;
}

static struct file *
dlb2_vfio_pci_step_device_state(struct dlb2_vfio_core_device *dlb2_vdev, u32 new)
{
	struct vfio_device_migration_info *minfo = dlb2_vdev->migration.minfo;
	struct dlb2_vfio_migration *mig = &dlb2_vdev->migration;
	u32 cur = mig->minfo->device_state;
	const struct file_operations *fops;
	struct device *dev;
	int ret = 0;
	u8 *data;

	dev = &dlb2_vdev->core_device.pdev->dev;
	dev_info(dev,
		 "%s: state (0x%x -> 0x%x)\n",
		 __func__, dlb2_vdev->migration.minfo->device_state, new);

	data = (u8 *)minfo + minfo->data_offset;

	if (cur == VFIO_DEVICE_STATE_STOP) {
		switch (new) {
		case VFIO_DEVICE_STATE_STOP_COPY:
			ret = dlb2_vdev_migration_stop(dlb2_vdev, data, minfo->data_size);
			if (ret < 0)
				return ERR_PTR(ret);
			fops = &dlb2_vfio_pci_save_fops;
			return dlb2_vfio_pci_get_mig_file(mig, dlb2_vdev,
							  fops, O_RDONLY);
		case VFIO_DEVICE_STATE_RESUMING:
			fops = &dlb2_vfio_pci_resume_fops;
			return dlb2_vfio_pci_get_mig_file(mig, dlb2_vdev,
							  fops, O_WRONLY);
		case VFIO_DEVICE_STATE_RUNNING:
		case VFIO_DEVICE_STATE_RUNNING_P2P:
			ret = dlb2_vdev_migration_resume(dlb2_vdev, data, minfo->data_size);
			if (ret < 0)
				return ERR_PTR(ret);
			return NULL;
		default:
			break;
		}

		return ERR_PTR(-EINVAL);
	}

	if (cur == VFIO_DEVICE_STATE_RUNNING_P2P &&
	     new == VFIO_DEVICE_STATE_RUNNING) {
		return NULL;
	}

	if ((cur == VFIO_DEVICE_STATE_RUNNING ||
	     cur == VFIO_DEVICE_STATE_RUNNING_P2P) &&
	    (new == VFIO_DEVICE_STATE_STOP ||
	     new == VFIO_DEVICE_STATE_RUNNING_P2P)) {
		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_STOP_COPY &&
	    new == VFIO_DEVICE_STATE_STOP) {
		ret = dlb2_vfio_pci_put_mig_file(mig, dlb2_vdev);
		if (ret < 0)
			return ERR_PTR(ret);
		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_RESUMING &&
	    new == VFIO_DEVICE_STATE_STOP) {
		ret = dlb2_vfio_pci_put_mig_file(mig, dlb2_vdev);
		if (ret < 0)
			return ERR_PTR(ret);
		return NULL;
	}

	dev_warn(dev, "%s: unexpected state transition (0x%x -> 0x%x)\n",
		 __func__, dlb2_vdev->migration.minfo->device_state, new);

	return ERR_PTR(-EINVAL);
}

static void dlb2_vfio_reset_done(struct dlb2_vfio_core_device *dlb2_vdev)
{
	dlb2_vdev->migration.minfo->device_state = VFIO_DEVICE_STATE_RUNNING;
	dlb2_vfio_disable_fds(dlb2_vdev);
}

static struct file *dlb2_vfio_pci_set_device_state(struct vfio_device *vdev,
						enum vfio_device_mig_state new_state)
{
	struct dlb2_vfio_core_device *dlb2_vdev = container_of(vdev,
			struct dlb2_vfio_core_device, core_device.vdev);
	enum vfio_device_mig_state next_state;
	struct file *res = NULL;
	int ret;

	mutex_lock(&dlb2_vdev->migration.state_mutex);
	while (new_state != dlb2_vdev->migration.minfo->device_state) {
		ret = vfio_mig_get_next_state(vdev, dlb2_vdev->migration.minfo->device_state,
					      new_state, &next_state);
		if (ret) {
			res = ERR_PTR(ret);
			break;
		}
		res = dlb2_vfio_pci_step_device_state(dlb2_vdev, next_state);
		if (IS_ERR(res))
			break;
		dlb2_vdev->migration.minfo->device_state = next_state;
		if (WARN_ON(res && new_state != dlb2_vdev->migration.minfo->device_state)) {
			fput(res);
			res = ERR_PTR(-EINVAL);
			break;
		}
	}
	mutex_unlock(&dlb2_vdev->migration.state_mutex);

	return res;
}

static int dlb2_vfio_pci_get_device_state(struct vfio_device *vdev,
				       enum vfio_device_mig_state *curr_state)
{
	struct dlb2_vfio_core_device *dlb2_vdev = container_of(vdev,
			struct dlb2_vfio_core_device, core_device.vdev);

	mutex_lock(&dlb2_vdev->migration.state_mutex);
	*curr_state = dlb2_vdev->migration.minfo->device_state;
	mutex_unlock(&dlb2_vdev->migration.state_mutex);

	return 0;
}

static int dlb2_vfio_pci_get_data_size(struct vfio_device *vdev,
				    unsigned long *stop_copy_length)
{
	struct dlb2_vfio_core_device *dlb2_vdev = container_of(vdev,
			struct dlb2_vfio_core_device, core_device.vdev);

	mutex_lock(&dlb2_vdev->migration.state_mutex);
	*stop_copy_length = dlb2_vdev->migration.minfo->data_size;
	mutex_unlock(&dlb2_vdev->migration.state_mutex);

	return 0;
}

static const struct vfio_migration_ops dlb2_vfio_pci_mig_ops = {
	.migration_set_state = dlb2_vfio_pci_set_device_state,
	.migration_get_state = dlb2_vfio_pci_get_device_state,
	.migration_get_data_size = dlb2_vfio_pci_get_data_size,
};

static void dlb2_vfio_pci_release_dev(struct vfio_device *core_vdev)
{
	struct dlb2_vfio_core_device *dlb2_vdev = container_of(core_vdev,
			struct dlb2_vfio_core_device, core_device.vdev);

	list_del(&dlb2_vdev->next);
	mutex_destroy(&dlb2_vdev->migration.state_mutex);
	vfio_pci_core_release_dev(core_vdev);
}

static int dlb2_vfio_pci_init_dev(struct vfio_device *core_vdev)
{
	struct dlb2_vfio_core_device *dlb2_vdev = container_of(core_vdev,
			struct dlb2_vfio_core_device, core_device.vdev);
	struct pci_dev *parent;
	struct dlb2 *dlb2;
	int ret, vf_id;

	core_vdev->migration_flags = VFIO_MIGRATION_STOP_COPY | VFIO_MIGRATION_P2P;
	core_vdev->mig_ops = &dlb2_vfio_pci_mig_ops;

	ret = vfio_pci_core_init_dev(core_vdev);
	if (ret)
		return ret;

	mutex_init(&dlb2_vdev->migration.state_mutex);
	mutex_init(&dlb2_vdev->migration.f_lock);

	parent = pci_physfn(dlb2_vdev->core_device.pdev);
	vf_id = pci_iov_vf_id(dlb2_vdev->core_device.pdev);
	if (vf_id < 0) {
		ret = -ENODEV;
		goto err_rel;
	}

	dlb2_vdev->parent = parent;
	dlb2_vdev->id = vf_id;

	ret = dlb2_vfio_pci_migration_init(dlb2_vdev, DLB2_LM_MIGRATION_CMD_SIZE);
	if (ret)
		goto err_rel;

	dlb2 = pci_get_drvdata(parent);
	list_add(&dlb2_vdev->next, &dlb2->vdev_list);
	return 0;
err_rel:
	vfio_pci_core_release_dev(core_vdev);
	return ret;
}

static const struct vfio_device_ops dlb2_vfio_pci_ops = {
	.name = "dlb2-vfio-pci",
	.init = dlb2_vfio_pci_init_dev,
	.release = dlb2_vfio_pci_release_dev,
	.open_device = dlb2_vfio_pci_open_device,
	.close_device = dlb2_vfio_pci_close_device,
	.ioctl = vfio_pci_core_ioctl,
	.read = vfio_pci_core_read,
	.write = vfio_pci_core_write,
	.mmap = vfio_pci_core_mmap,
	.request = vfio_pci_core_request,
	.match = vfio_pci_core_match,
#if KERNEL_VERSION(6, 17, 0) <= LINUX_VERSION_CODE
	.match_token_uuid = vfio_pci_core_match_token_uuid,
#endif
	.bind_iommufd = vfio_iommufd_physical_bind,
	.unbind_iommufd = vfio_iommufd_physical_unbind,
	.attach_ioas = vfio_iommufd_physical_attach_ioas,
#if KERNEL_VERSION(6, 2, 0) >= LINUX_VERSION_CODE
	.attach_hwpt    = vfio_iommufd_physical_attach_hwpt,
#else
	.detach_ioas = vfio_iommufd_physical_detach_ioas,
#endif
};

static struct dlb2_vfio_core_device *dlb2_vfio_drvdata(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *core_device = pci_get_drvdata(pdev);

	return container_of(core_device, struct dlb2_vfio_core_device, core_device);
}

static void dlb2_vfio_pci_aer_reset_done(struct pci_dev *pdev)
{
	struct dlb2_vfio_core_device *dlb2_vdev = dlb2_vfio_drvdata(pdev);

	if (!dlb2_vdev->mig_state.src_vm_state)
		return;

	mutex_lock(&dlb2_vdev->migration.state_mutex);
	dlb2_vfio_reset_done(dlb2_vdev);
	mutex_unlock(&dlb2_vdev->migration.state_mutex);
}

static int
dlb2_vfio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct dlb2_vfio_core_device *dlb2_vdev;
	int ret;

	dlb2_vdev = vfio_alloc_device(dlb2_vfio_core_device, core_device.vdev, dev, &dlb2_vfio_pci_ops);
	if (IS_ERR(dlb2_vdev))
		return PTR_ERR(dlb2_vdev);

	pci_set_drvdata(pdev, &dlb2_vdev->core_device);
	ret = vfio_pci_core_register_device(&dlb2_vdev->core_device);
	if (ret)
		goto out_put_device;

	return 0;

out_put_device:
	vfio_put_device(&dlb2_vdev->core_device.vdev);
	return ret;
}

static void dlb2_vfio_pci_remove(struct pci_dev *pdev)
{
	struct dlb2_vfio_core_device *dlb2_vdev = dlb2_vfio_drvdata(pdev);

	dlb2_vfio_pci_migration_destory(dlb2_vdev);

	vfio_pci_core_unregister_device(&dlb2_vdev->core_device);

	vfio_put_device(&dlb2_vdev->core_device.vdev);
}

static const struct pci_device_id dlb2_vfio_pci_table[] = {
	/* Intel DLB2+ VF device */
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_INTEL, 0x2711) },
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_INTEL, 0x2715) },
	{}
};
MODULE_DEVICE_TABLE(pci, dlb2_vfio_pci_table);

static const struct pci_error_handlers dlb2_err_handlers = {
	.reset_done = dlb2_vfio_pci_aer_reset_done,
	.error_detected = vfio_pci_core_aer_err_detected,
};

static struct pci_driver dlb2_vfio_pci_driver = {
	.name = "dlb2-vfio-pci",
	.id_table = dlb2_vfio_pci_table,
	.probe = dlb2_vfio_pci_probe,
	.remove = dlb2_vfio_pci_remove,
	.err_handler = &dlb2_err_handlers,
	.driver_managed_dma = true,
};
module_pci_driver(dlb2_vfio_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Copyright(c) 2018-2024 Intel Corporation");
MODULE_DESCRIPTION("DLB VFIO PCI - VFIO PCI driver with live migration support for Intel(R) Dynamic Load Balancer 2.x Driver");
#endif
