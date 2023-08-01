#define pr_fmt(fmt) "bwd: " fmt

#include <linux/module.h>
#include <linux/memory.h>
#include <linux/cdev.h>
#include <linux/fs.h>

#define DEV_NAME	"bwd"

#define BWD_REGISTER_TASK		_IO('M', 0)
#define BWD_UNREGISTER_TASK		_IO('M', 1)

static struct class *dev_class;
static dev_t bwd_devt;
static struct cdev bwd_cdev;
static struct task_struct *bwd_task;

static int device_open(struct inode *inode, struct file *file)
{
	if (!try_module_get(THIS_MODULE)) {
		pr_err("Failed to get module\n");
		return -ENODEV;
	}

	return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
	module_put(THIS_MODULE);
	return 0;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case BWD_REGISTER_TASK:
		bwd_task = current;
		pr_info("Register pid %d\n", bwd_task->pid);
		break;
	case BWD_UNREGISTER_TASK:
		if (!bwd_task) {
			pr_err("task is not registered yet\n");
			return -EPERM;
		}
		if (bwd_task->pid != current->pid) {
			pr_err("Current task(%d) is different from registered pid(%d)\n",
					current->pid, bwd_task->pid);
			return -EPERM;
		}

		pr_info("Unregister task (pid: %d)\n", bwd_task->pid);
		bwd_task = NULL;
		break;
	default:
		pr_err("Not supported command: %x\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static struct file_operations fops = {
	.open = device_open,
	.release = device_release,
	.unlocked_ioctl = device_ioctl,
};

static int bwd_memory_notifier(struct notifier_block *nb,
		unsigned long val, void *v)
{
	struct memory_notify *mem = (struct memory_notify *)v;
	int rc;

	if (!bwd_task) {
		pr_warn_once("Task is not registered\n");
		return NOTIFY_DONE;
	}

	switch (val) {
	case MEM_OFFLINE:
	case MEM_ONLINE:
		if (mem->status_change_nid == -1)
			return NOTIFY_DONE;

		pr_info("Node %d status changed\n", mem->status_change_nid);
		rc = kill_pid(find_vpid(bwd_task->pid), SIGUSR1, 1);
		if (rc) {
			pr_err("Failed to send signal %d to pid %d\n",
					SIGUSR1, bwd_task->pid);
			return NOTIFY_DONE;
		}

		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block bwd_memory_nb = {
	.notifier_call = bwd_memory_notifier,
	.priority = 0
};

static int __init bwd_init(void)
{
	int rc;
	struct device *dev;

	rc = register_memory_notifier(&bwd_memory_nb);
	if (rc) {
		pr_err("Failed to register a memory notifier (%d)\n", rc);
		return rc;
	}

	rc = alloc_chrdev_region(&bwd_devt, 0, 1, DEV_NAME);
	if (rc) {
		pr_err("Failed to allocate chrdev region (%d)\n", rc);
		goto err_chrdev_region;
	}

	cdev_init(&bwd_cdev, &fops);

	rc = cdev_add(&bwd_cdev, bwd_devt, 1);
	if (rc) {
		pr_err("Failed to add device to the system (%d)\n", rc);
		goto err_cdev;
	}
	
	dev_class = class_create(DEV_NAME);
	if (IS_ERR(dev_class)) {
		rc = PTR_ERR(dev_class);
		pr_err("Failed to create class (%d)\n", rc);
		goto err_class;
	}

	dev = device_create(dev_class, NULL, bwd_devt, NULL, DEV_NAME);
	if (IS_ERR(dev)) {
		rc = PTR_ERR(dev);
		pr_err("Failed to create device (%d)\n", rc);
		goto err_device;
	}

	return 0;

err_device:
	class_destroy(dev_class);
err_class:
	cdev_del(&bwd_cdev);
err_cdev:
	unregister_chrdev_region(bwd_devt, 1);
err_chrdev_region:
	unregister_memory_notifier(&bwd_memory_nb);

	return rc;
}

static void __exit bwd_exit(void)
{
	device_destroy(dev_class, bwd_devt);
	class_destroy(dev_class);
	cdev_del(&bwd_cdev);
	unregister_chrdev_region(bwd_devt, 1);
	unregister_memory_notifier(&bwd_memory_nb);
}

module_init(bwd_init);
module_exit(bwd_exit);
MODULE_LICENSE("GPL");
