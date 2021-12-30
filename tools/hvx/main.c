/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/eventfd.h>
#include <linux/io.h>
#include <linux/anon_inodes.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>

#include "log.h"
#include "hvx.h"
#include "ipa.h"
#include "interface.h"

#define HVX_MAX_VCPUS	8

#define HVX_DEVICE_NAME "hvx"
#define HVX_CLASS_NAME "hvx"

struct hvx_memory_chunk {
	struct page *page;
	size_t size;
	struct list_head head;
};

struct hvx_vcpu {
	unsigned int id;
	struct eventfd_ctx  *eventfd;
};

struct hvx_domain {
	unsigned long		id;
	unsigned int		vcpus;
	struct device 		*dev;
	struct list_head	head;
	struct list_head	extent_list;
	atomic_t			refcnt;
	struct mutex		lock;
	unsigned long		flags;
	struct page			*page_table;
};

struct hvx_event {
	unsigned long id;
};

static int hvx_dev_major;
static struct class *hvx_class;
static struct device *hvx_device;

static LIST_HEAD(hvx_domain_list);
static DEFINE_RWLOCK(hvx_domain_list_lock);
static struct hvx_event __percpu *hvx_event;

static unsigned int hvx_event_irq = 31;

void free_guest_pages(struct page *pg, size_t size)
{
	unsigned long addr = page_to_phys(pg);
	unsigned long end = addr + PAGE_ALIGN(size);

	while (addr < end) {
		__free_pages(pg, 0);
		addr += PAGE_SIZE;
		pg++;
	}
}

struct page *alloc_guest_pages(size_t size, gfp_t gfp_mask)
{
	unsigned int order = get_order(size);
	unsigned long addr, end, used;
	struct page *pg;

	pg = alloc_pages(gfp_mask, order);
	if (pg == NULL)
		return NULL;

	addr = page_to_phys(pg);
	end = addr + (PAGE_SIZE << order);
	used = addr + PAGE_ALIGN(size);

	split_page(pg, order);
	while (used < end) {
		__free_pages(phys_to_page(used), 0);
		used += PAGE_SIZE;
	}

	return pg;
}

void hvx_free_extents(struct hvx_domain *domain)
{
	unsigned long freed = 0;
	struct hvx_memory_chunk *ext, *tmp;

	list_for_each_entry_safe(ext, tmp, &domain->extent_list, head) {
		free_guest_pages(ext->page, ext->size);
		freed += ext->size;
		list_del(&ext->head);
		kfree(ext);
	}

	hvx_debug("%ld memories freed\n", freed);
}

#define MIN_PAGE_SIZE	(PAGE_SIZE * 512)

int hvx_alloc_extents(struct hvx_domain *domain, size_t maxmem)
{
	size_t allocated = 0;

	while (allocated < maxmem) {
		struct hvx_memory_chunk *ext;

		struct page *pg = alloc_guest_pages(MIN_PAGE_SIZE, GFP_HIGHUSER);
		if (pg == NULL) {
			hvx_error("Not enough memory: %ld allocated\n", allocated);
			hvx_free_extents(domain);
			return -ENOMEM;
		}

		ext = kmalloc(sizeof(*ext), GFP_KERNEL);
		if (ext == NULL) {
			free_guest_pages(pg, MIN_PAGE_SIZE);
			break;
		}

		ext->page  = pg;
		ext->size  = MIN_PAGE_SIZE;
		list_add_tail(&ext->head, &domain->extent_list);

		allocated += MIN_PAGE_SIZE;
	}

	if (allocated < maxmem) {
		hvx_error("Not enough memory: %ld allocated\n", allocated);
		hvx_free_extents(domain);
		return -ENOMEM;
	}

	hvx_debug("%ld memories allocated\n", maxmem);

	return 0;
}

static long hvx_create_address_space(struct hvx_domain *dom, size_t size)
{
	struct page *pgd;
	struct hvx_memory_chunk *ext;
	unsigned long *ptr;
	unsigned long vaddr = 0x40000000UL >> IPA_PAGE_SHIFT; 

	if (hvx_alloc_extents(dom, size) != 0) {
		hvx_error("Failed to allocate memory\n");
		return -ENOMEM;
	}

	pgd = alloc_pages(GFP_KERNEL | __GFP_ZERO, 0);
	if (pgd == NULL) {
		hvx_free_extents(dom);
		hvx_error("Page to allocate page directory\n");
		return -ENOMEM;
	}

	ptr = page_to_virt(pgd);
	list_for_each_entry(ext, &dom->extent_list, head) {
		unsigned long nr = ext->size >> PAGE_SHIFT;
		unsigned long pfn = page_to_pfn(ext->page);
		ipa_map_range(ptr, vaddr, pfn, nr, IPA_TYPE_NORMAL);
		vaddr += nr;
	}

	ipa_map_range(ptr, 0x50042000 >> PAGE_SHIFT, 0x50046000 >> PAGE_SHIFT, 2, IPA_TYPE_DEVICE);
	ipa_unmap_range(ptr, 0x50041000 >> PAGE_SHIFT, 1);

	dom->page_table = pgd;

	ipa_dump_page_maps(ptr, 0);

	return 0;
}

static void hvx_destroy_address_space(struct hvx_domain *dom)
{
	if (dom->page_table) {
		ipa_free_table(page_to_virt(dom->page_table));
		dom->page_table = NULL;
	}

	hvx_free_extents(dom);
}

static long hvx_ioctl_domain_create(struct hvx_domain *dom, void __user *udata)
{
	long id = -1;
	struct hvx_proto_domain_create op;
	struct domain_control domctl;

	if (copy_from_user(&op, udata, sizeof(op)))
		return -EFAULT;

	if (hvx_create_address_space(dom, op.memory) != 0) {
		return -ENOMEM;
	}

	domctl.vcpus = op.vcpus;
	domctl.xlate = page_to_phys(dom->page_table);
	if ((id = vmcall(VMI_DOMAIN_CONTROL, VMI_DOMAIN_CREATE, &domctl, 0, 0, 0)) < 0) {
		hvx_error("Failed to create domain\n");
		return -EFAULT;
	}

	dom->id = id;

	return id;
}

static long hvx_ioctl_domain_destroy(struct hvx_domain *dom, void __user *udata)
{
	struct hvx_proto_domain_destroy domain;
	struct domain_control domctl;

	if (copy_from_user(&domain, udata, sizeof(domain)))
		return -EFAULT;

	domctl.id = dom->id;
	if (vmcall(VMI_DOMAIN_CONTROL, VMI_DOMAIN_DESTROY, &domctl, 0, 0, 0) < 0) {
		hvx_error("Failed to destroy domain. domain busy\n");
		return -EFAULT;
	}

	hvx_destroy_address_space(dom);

	return 0;
}

static long hvx_vcpu_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	long r = 0;
	return r;
}

static int hvx_vcpu_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations hvx_vcpu_fops = {
	.release        = hvx_vcpu_release,
	.unlocked_ioctl = hvx_vcpu_ioctl,
	.llseek			= noop_llseek,
};

static int hvx_create_eventfd(struct hvx_vcpu *vcpu, long r)
{
	int ret = 0;
	struct fd f;
	struct eventfd_ctx *eventfd = NULL;

	f = fdget(r);
	if (!f.file) {
		ret = -EBADF;
		goto out;
	}

	eventfd = eventfd_ctx_fileget(f.file);
	if (IS_ERR(eventfd)) {
		ret = PTR_ERR(eventfd);
		goto out_fdput;
	}

	vcpu->eventfd = eventfd;

out_fdput:
	fdput(f);

out:
	return ret;
}

#define ITOA_MAX_LEN 12

static int hvx_create_vcpu_fd(struct hvx_vcpu *vcpu)
{
	char name[8 + 1 + ITOA_MAX_LEN + 1];

	snprintf(name, sizeof(name), "hvx-vcpu:%d", vcpu->id);
	return anon_inode_getfd(name, &hvx_vcpu_fops, vcpu, O_RDWR | O_CLOEXEC);
}

static long hvx_ioctl_vcpu_context(struct hvx_domain *dom, void __user *udata)
{
	long r = 0;
	struct hvx_vcpu *v;
	struct hvx_proto_vcpu_context vcpu;
	struct vcpu_control vcpuctl;

	if (copy_from_user(&vcpu, udata, sizeof(vcpu)))
		return -EFAULT;

	v = kzalloc(sizeof(struct hvx_domain), GFP_KERNEL);
	if (!v)
		return -ENOMEM;

	vcpuctl.domain = dom->id;
	vcpuctl.entry = vcpu.entry + 0x40000000UL;
	vcpuctl.affinity = vcpu.affinity;
	vcpuctl.contextid = vcpu.contextid;
	if (vmcall(VMI_VCPU_CONTROL, VMI_VCPU_CREATE, &vcpuctl, 0, 0, 0) < 0) {
		hvx_error("Failed to destroy domain. domain busy\n");
		r = -EFAULT;
		goto free_vcpu;
	}

	mutex_lock(&dom->lock);
	if (dom->vcpus == HVX_MAX_VCPUS) {
		mutex_unlock(&dom->lock);
		hvx_error("Maximum vcpus\n");
		r = -EINVAL;
		goto destroy_vcpu;
	}

	v->id = dom->vcpus++;
	mutex_unlock(&dom->lock);

	r = hvx_create_vcpu_fd(v);
	if (r < 0) {
		goto unlock_vcpu_destroy;
	}

	hvx_create_eventfd(v, r);

	return r;

unlock_vcpu_destroy:
	mutex_lock(&dom->lock);
	dom->vcpus--;
	mutex_unlock(&dom->lock);

destroy_vcpu:

free_vcpu:
	kfree(v);
	
	return r;
}

static long hvx_ioctl_hypercall(void __user *udata)
{
	long ret;
	struct hvx_proto_hypercall hcall;

	if (copy_from_user(&hcall, udata, sizeof(hcall)))
		return -EFAULT;

	ret = hypercall(hcall.op,
					hcall.params[0],
					hcall.params[1],
					hcall.params[2],
					hcall.params[3],
					hcall.params[4]);

	return ret;
}

void hvx_vma_close(struct vm_area_struct *vma)
{
	hvx_debug("Release memory area %lx-%lx\n", vma->vm_start, vma->vm_end);
	vma->vm_private_data = NULL;
}

static long hvx_ioctl(struct file *file, unsigned int cmd, unsigned long data)
{
	int ret = -ENOTTY;
    struct hvx_domain *dom = file->private_data;
	void __user *udata = (void __user *)data;

	switch (cmd) {
	case HVX_IOCTL_HYPERCALL:
		ret = hvx_ioctl_hypercall(udata);
		break;

	case HVX_IOCTL_DOMAIN_CREATE:
		ret = hvx_ioctl_domain_create(dom, udata);
		hvx_error("Domain Created\n");
		break;

	case HVX_IOCTL_DOMAIN_DESTROY:
		hvx_error("Destroy domain\n");
		ret = hvx_ioctl_domain_destroy(dom, udata);
		hvx_error("Domain destroyed\n");
		break;

	case HVX_IOCTL_VCPU_CONTEXT:
		hvx_error("Start vcpu\n");
		ret = hvx_ioctl_vcpu_context(dom, udata);
		hvx_error("Vcpu started\n");
		break;

	default:
		break;
	}

	return ret;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0))
static int hvx_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
#else
static int hvx_vma_fault(struct vm_fault *vmf)
#endif
{
	return VM_FAULT_SIGBUS;
}

static const struct vm_operations_struct hvx_vm_ops = {
	.close = hvx_vma_close,
	.fault = hvx_vma_fault
};

static int hvx_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long uaddr = vma->vm_start  & PAGE_MASK;
	unsigned long usize = ((vma->vm_end - uaddr) + (PAGE_SIZE - 1)) & PAGE_MASK;
	unsigned long pgoff = vma->vm_pgoff << PAGE_SHIFT;
	struct hvx_domain *dom = (struct hvx_domain *)file->private_data;
	struct hvx_memory_chunk *ext;

	if (usize == 0)
		return 0;

	if (pgoff < 0x40000000UL)
		return -ENOMEM;

	pgoff -= 0x40000000UL;

	vma->vm_flags = vma->vm_flags | VM_DONTCOPY | VM_DONTEXPAND | VM_MIXEDMAP | VM_DONTDUMP | VM_PFNMAP;

	list_for_each_entry(ext, &dom->extent_list, head) {
		unsigned long mapsize, pfn;

		if (ext->size < pgoff) {
			pgoff -= ext->size;
			continue;
		}

		pfn = page_to_pfn(ext->page);
		pfn += pgoff >> PAGE_SHIFT;

		if (usize < (ext->size - pgoff)) {
			mapsize = usize;
		} else {
			mapsize = ext->size - pgoff;
		}
		pgoff = 0;
		hvx_info("mmap: 0x%lx, 0x%lx:0x%lx\n", uaddr, pfn, mapsize);
		if (remap_pfn_range(vma, uaddr, pfn, mapsize, vma->vm_page_prot) != 0)
			return -EINVAL;

		uaddr += mapsize;
		usize -= mapsize;
		if (usize == 0)
			break;
	}

	vma->vm_ops = &hvx_vm_ops;

	hvx_debug("Memory area 0x%lx - 0x%lx allocated to %d\n", vma->vm_start, vma->vm_end, current->pid);

	return 0;
}

static ssize_t hvx_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
	/* Does Nothing */
	return 0;
}

static ssize_t hvx_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    /* Does Nothing */
	return 0;
}

static int hvx_release(struct inode *ino, struct file *file)
{
	struct hvx_domain *domain = file->private_data;
	if (domain == NULL) {
		hvx_error("hvx: invalid domain\n");
		return -EFAULT;
	}

	write_lock_bh(&hvx_domain_list_lock);
	list_del_init(&domain->head);
	write_unlock_bh(&hvx_domain_list_lock);

	hvx_destroy_address_space(domain);

	kfree(domain);

	file->private_data = NULL;

	hvx_debug("All resources are released\n");

	return 0;
}

static unsigned int hvx_poll(struct file *file, poll_table *wait)
{
	return 0;
}

static int hvx_open(struct inode *ino, struct file *file)
{
	struct hvx_domain *domain;

	domain = kzalloc(sizeof(struct hvx_domain), GFP_KERNEL);
	if (!domain)
		return -ENOMEM;

	domain->dev = hvx_device;

	INIT_LIST_HEAD(&domain->head);
	INIT_LIST_HEAD(&domain->extent_list);

	mutex_init(&domain->lock);
	atomic_set(&domain->refcnt, 1);

	write_lock_bh(&hvx_domain_list_lock);
	list_add(&domain->head, &hvx_domain_list);
	write_unlock_bh(&hvx_domain_list_lock);

	file->private_data = domain;

	return 0;
}

const struct file_operations hvx_fops = {
	.owner = THIS_MODULE,
	.open = hvx_open,
	.read = hvx_read,
	.write = hvx_write,
	.poll = hvx_poll,
	.mmap = hvx_mmap,
	.unlocked_ioctl = hvx_ioctl,
	.release = hvx_release,
};

static irqreturn_t hvx_event_callback(int irq, void *arg)
{
	hvx_error("Event check\n");
    return IRQ_HANDLED;
}

static int hvx_starting_cpu(unsigned int cpu)
{
    hvx_info("Initializing cpu%d\n", cpu);
	enable_percpu_irq(hvx_event_irq, 0);
	return 0;
}

static int hvx_dying_cpu(unsigned int cpu)
{
    hvx_info("Finalizing cpu%d\n", cpu);
	disable_percpu_irq(hvx_event_irq);
	return 0;
}

static const struct of_device_id hvx_event_of_match[] = {
	{ .compatible   = "hvx,event",    },
	{},
};


int hvx_init(void)
{
	int err = 0;
	struct device_node *dn;
	struct irq_data* irq_data;

	dn = of_find_matching_node(NULL, hvx_event_of_match);
	if (!dn || !of_device_is_available(dn)) {
		hvx_error("hvx node is not available\n");
		return -EINVAL;
	}

    hvx_event = alloc_percpu(struct hvx_event);
    if (!hvx_event) {
        err = -ENOMEM;
        goto out;
    }

	hvx_dev_major = register_chrdev(0, HVX_DEVICE_NAME, &hvx_fops);
	if (hvx_dev_major < 0) {
		hvx_error("Failed to create hvx device\n");
		err = hvx_dev_major;
		goto out_free;
	}

	hvx_class = class_create(THIS_MODULE, HVX_CLASS_NAME);
	if (IS_ERR(hvx_class)) {
		hvx_error("Failed to register device class\n");
		err =  PTR_ERR(hvx_class);
		goto out_chdev;
	}

	hvx_device = device_create(hvx_class, NULL, MKDEV(hvx_dev_major, 0),
							   NULL, HVX_DEVICE_NAME);
	if (IS_ERR(hvx_device)) {
		hvx_error("Failed to create the device\n");
		err = PTR_ERR(hvx_device);
		goto out_class;
	}

	hvx_event_irq = irq_of_parse_and_map(dn, 0);
    of_node_put(dn);

	irq_data = irq_get_irq_data(hvx_event_irq);
	if (irq_data == NULL) {
		hvx_error("No irq data found\n");
		goto out_class;
	}

	printk("VIRQ = %d, HWIRQ = %ld\n", hvx_event_irq, irq_data->hwirq);
	err = request_percpu_irq(hvx_event_irq, hvx_event_callback,
							 "hvx-vm-event", hvx_event);
	if (err) {
		hvx_error("Failed to allocate irq\n");
		goto out_dev;
	}

	err = cpuhp_setup_state(CPUHP_AP_ONLINE, "hvx:starting",
							hvx_starting_cpu, hvx_dying_cpu);
	if (err) {
		hvx_error("Failed to setup cpu hotplug\n");
		goto out_irq;
	}

	return 0;

out_irq:
	free_percpu_irq(hvx_event_irq, hvx_event);

out_dev:
	device_destroy(hvx_class, MKDEV(hvx_dev_major, 0));

out_class:
	class_unregister(hvx_class);
	class_destroy(hvx_class);
	
out_chdev:
	unregister_chrdev(hvx_dev_major, HVX_DEVICE_NAME);

out_free:
	free_percpu(hvx_event);

out:
	return err;
}

void hvx_exit(void)
{
	free_percpu_irq(hvx_event_irq, hvx_event);
	device_destroy(hvx_class, MKDEV(hvx_dev_major, 0));
	class_unregister(hvx_class);
	class_destroy(hvx_class);
	unregister_chrdev(hvx_dev_major, HVX_DEVICE_NAME);
	free_percpu(hvx_event);
}

module_init(hvx_init);
module_exit(hvx_exit);

MODULE_LICENSE("GPL");
