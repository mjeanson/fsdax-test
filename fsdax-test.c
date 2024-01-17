#include <asm/errno.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/slab.h>                 //kmalloc()
#include <linux/uaccess.h>              //copy_to/from_user()
#include <linux/vmalloc.h>
#include <linux/highmem.h>

#include "fsdax-test.h"
 
struct fsdax_kmap {
	char *kaddr;
	char *uaddr;
	size_t nr_pages;
	struct page **pages;
};

dev_t dev = 0;
static struct class *class;
static struct fsdax_kmap daxkmap;

/*
** Function Prototypes
*/
static int      __init fsdax_test_init(void);
static void     __exit fsdax_test_exit(void);
static int      fsdax_test_open(struct inode *inode, struct file *file);
static int      fsdax_test_release(struct inode *inode, struct file *file);
static ssize_t  fsdax_test_read(struct file *filp, char __user *buf, size_t len,loff_t * off);
static ssize_t  fsdax_test_write(struct file *filp, const char *buf, size_t len, loff_t * off);
static long     fsdax_test_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/*
** File operation sturcture
*/
static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = fsdax_test_read,
        .write          = fsdax_test_write,
        .open           = fsdax_test_open,
        .unlocked_ioctl = fsdax_test_ioctl,
        .release        = fsdax_test_release,
};
/*
** This function will be called when we open the Device file
*/
static int fsdax_test_open(struct inode *inode, struct file *file)
{
        pr_info("fsdax-test: Device File Opened\n");
        return 0;
}
/*
** This function will be called when we close the Device file
*/
static int fsdax_test_release(struct inode *inode, struct file *file)
{
        pr_info("fsdax-test: Device File Closed\n");
        return 0;
}
/*
** This function will be called when we read the Device file
*/
static ssize_t fsdax_test_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("fsdax-test: Read Function\n");
        return 0;
}
/*
** This function will be called when we write the Device file
*/
static ssize_t fsdax_test_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("fsdax-test: Write function\n");
        return len;
}

static int fsdax_map_pages(char *uaddr, size_t size)
{
	int ret = 0;
	char buf[32];

	if (daxkmap.kaddr != NULL) {
		pr_err("fsdax-test: Existing mapping at: 0x%p\n", daxkmap.kaddr);
		ret = -1;
		goto end;
	}

	// Mapping size must 
	if ((size % PAGE_SIZE) != 0) {
		pr_err("fsdax-test: Mapping size invalid: %ld\n", size);
		ret = -1;
		goto end;
	}

	daxkmap.uaddr = uaddr;
	daxkmap.nr_pages = size / PAGE_SIZE;

	// Allocate page array
        pr_info("fsdax-test: Allocating page array of %ld pages\n", daxkmap.nr_pages);
	daxkmap.pages = kcalloc(daxkmap.nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (daxkmap.pages == NULL) {
		pr_err("fsdax-test: Failed to allocate page array\n");
		ret = -1;
		goto err;
	}

        pr_info("fsdax-test: Get %ld user pages at 0x%p\n", daxkmap.nr_pages, uaddr);

	// Get user pages with the mmap lock held
	down_read(&current->mm->mmap_lock);
	ret = get_user_pages((unsigned long) uaddr, daxkmap.nr_pages, FOLL_WRITE, daxkmap.pages);
	up_read(&current->mm->mmap_lock);

	if (ret == daxkmap.nr_pages) {
        	pr_info("fsdax-test: Mapping %ld pages\n", daxkmap.nr_pages);
		daxkmap.kaddr = vmap(daxkmap.pages, daxkmap.nr_pages, VM_MAP, PAGE_KERNEL);
		if (!daxkmap.kaddr) {
			pr_err("fsdax-test: Failed to map page\n");
			ret = -1;
			goto err;
		}

		pr_info("fsdax-test: Mapped %ld pages from uaddr 0x%p at kaddr 0x%p\n", daxkmap.nr_pages, uaddr, daxkmap.kaddr);

		strncpy(buf, daxkmap.kaddr, sizeof(buf));
		buf[sizeof(buf) - 1] = '\0';
		pr_info("fsdax-test: Read: '%s'\n", buf);

		// Fill the file
		memset(daxkmap.kaddr, 0xff, daxkmap.nr_pages * PAGE_SIZE);

		// Say hi to userspace
		strcpy(daxkmap.kaddr, "The kernel was here!");

		pr_info("fsdax-test: Wrote: '%s'\n", daxkmap.kaddr);
		
	} else {
	    pr_err("fsdax-test: Couldn't get all %ld pages :(\n", daxkmap.nr_pages);
	    ret = -1;
	    goto err;
	}

end:
	return ret;

err:
	if (daxkmap.pages) {
		kfree(daxkmap.pages);
	}
	memset(&daxkmap, '\0', sizeof(struct fsdax_kmap));

	return ret;
}

static int fsdax_unmap_pages(char *uaddr, size_t size)
{
	int ret = 0;
	char buf[32];

	if (daxkmap.kaddr == NULL) {
		pr_err("fsdax-test: unmap: No existing mapping\n");
		ret = -1;
		goto end;
	}

	if (daxkmap.uaddr != uaddr) {
		pr_err("fsdax-test: unmap: Wrong existing mapping: 0x%p != 0x%p\n", daxkmap.uaddr, uaddr);
		ret = -1;
		goto end;
	}

	if (daxkmap.nr_pages != size / PAGE_SIZE) {
		pr_err("fsdax-test: unmap: Invalif mapping size\n");
		ret = -1;
		goto end;
	}

	// Read before unmapping
	strncpy(buf, daxkmap.kaddr, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	pr_info("fsdax-test: Read: '%s'\n", buf);

	pr_info("fsdax-test: Unmapping pages at 0x%p\n", daxkmap.kaddr);
	vunmap(daxkmap.kaddr);
	
	pr_info("fsdax-test: Putting %ld pages\n", daxkmap.nr_pages);
	for (int i=0; i<daxkmap.nr_pages; i++) {
		// Not sure this is required for DAX backed mappings
		if (!PageReserved(daxkmap.pages[i])) {
			pr_info("fsdax-test: Page %d dirty\n", i);
			SetPageDirty(daxkmap.pages[i]);
		}

		// However, this is
		put_page(daxkmap.pages[i]);
	}

	if (daxkmap.pages) {
		kfree(daxkmap.pages);
	}
	memset(&daxkmap, '\0', sizeof(struct fsdax_kmap));

end:
	return ret;
}

/*
** This function will be called when we write IOCTL on the Device file
*/
static long fsdax_test_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;
	struct fsdax_umap umap;

	switch (cmd) {
		case FSDAX_TEST_IOCTL_MAP:
		        if (copy_from_user(&umap ,(struct fsdax_umap *) arg, sizeof(struct fsdax_umap))) {
		                pr_err("fsdax-test: ioctl MAP failed copy_from_user()\n");
				ret = -1;
				goto err;
		        }
		        pr_info("fsdax-test: ioctl FSDAX_TEST_IOCTL_MAP uaddr = 0x%p, size = %lu\n", umap.addr, umap.size);

			ret = fsdax_map_pages(umap.addr, umap.size);

		        break;
		
		case FSDAX_TEST_IOCTL_UNMAP:
		        if (copy_from_user(&umap ,(struct fsdax_umap *) arg, sizeof(struct fsdax_umap))) {
		                pr_err("fsdax-test: ioctl UNMAP failed copy_from_user()\n");
				ret = -1;
				goto err;
		        }
		        pr_info("fsdax-test: ioctl FSDAX_TEST_IOCTL_UNMAP uaddr = 0x%p, size = %lu\n", umap.addr, umap.size);

			ret = fsdax_unmap_pages(umap.addr, umap.size);

		        break;
		
		default:
		        pr_info("fsdax-test: Unknown ioctl: %u\n", cmd);
			ret = -2;
		        break;
	}
err:
	pr_info("fsdax-test: ioctl return with %ld\n", ret);
	return ret;
}
 
/*
 * Module Init function
 */
static int __init fsdax_test_init(void)
{
	if (register_chrdev(FSDAX_TEST_MAJOR_NUM, FSDAX_TEST_DEVICE_FILE_NAME, &fops) < 0) {
            pr_err("fsdax-test: Cannot create the Device 1\n");
	    return -1;
	}

	class = class_create(FSDAX_TEST_DEVICE_FILE_NAME);
	device_create(class, NULL, MKDEV(FSDAX_TEST_MAJOR_NUM, 0), NULL, FSDAX_TEST_DEVICE_FILE_NAME);

        pr_info("fsdax-test: Module loaded\n");
        return 0;
}

/*
 * Module exit function
 */
static void __exit fsdax_test_exit(void)
{
	if (daxkmap.kaddr != NULL) {
		pr_info("fsdax-test: Unmapping leftover mapping\n");
		fsdax_unmap_pages(daxkmap.uaddr, daxkmap.nr_pages * PAGE_SIZE);
	}

	device_destroy(class, MKDEV(FSDAX_TEST_MAJOR_NUM, 0));
        class_destroy(class);
	unregister_chrdev(FSDAX_TEST_MAJOR_NUM, FSDAX_TEST_DEVICE_FILE_NAME);
        pr_info("fsdax-test: Module unloaded\n");
}
 
module_init(fsdax_test_init);
module_exit(fsdax_test_exit);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Jeanson <mjeanson@efficios.com>");
MODULE_DESCRIPTION("FSDAX tests");
MODULE_VERSION("1.0");
