// SPDX-License-Identifier: GPL-2.0
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/bitmap.h>
#include <linux/sched/mm.h>
#include <linux/version.h>
#include <linux/module.h>
#include <asm/tlbflush.h>
#include <linux/fdtable.h>
#include "ept_idle.h"
#include "ept_idle_native_pagewalk.h"
#include "tlb_flush.h"

/* #define DEBUG 1 */

/*
   Fallback to false for kernel doens't support KVM_INVALID_SPTE
   ept_idle can sitll work in this situation but the scan accuracy may drop, depends on
   the access frequences of the workload.
*/
#ifdef KVM_INVALID_SPTE
  #define KVM_CHECK_INVALID_SPTE(val) (val) == KVM_INVALID_SPTE
#else
  #define KVM_CHECK_INVALID_SPTE(val) (0)
#endif


#if LINUX_VERSION_CODE == KERNEL_VERSION(4, 17, 0)
# define pgtable_l5_enabled() (pgtable_l5_enabled)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
# define pgtable_l5_enabled() (0)
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
# define kvm_arch_mmu_pointer(vcpu) (vcpu->arch.mmu)
/*For RedHat 7.7 beta*/
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 10, 0)
# define kvm_arch_mmu_pointer(vcpu) (vcpu->arch.mmu)
#else
# define kvm_arch_mmu_pointer(vcpu) (&vcpu->arch.mmu)
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
# define kvm_mmu_ad_disabled(mmu) (mmu->mmu_role.base.ad_disabled)
/*For RedHat 7.7 beta*/
#elif LINUX_VERSION_CODE == KERNEL_VERSION(3, 10, 0)
# define kvm_mmu_ad_disabled(mmu) (mmu->mmu_role.base.ad_disabled)
#else
# define kvm_mmu_ad_disabled(mmu) (mmu->base_role.ad_disabled)
#endif

#ifdef DEBUG

#define debug_printk trace_printk

#define set_restart_gpa(val, note)	({			\
	unsigned long old_val = eic->restart_gpa;		\
	eic->restart_gpa = (val);				\
	trace_printk("restart_gpa=%lx %luK  %s  %s %d\n",	\
		     (val), (eic->restart_gpa - old_val) >> 10,	\
		     note, __func__, __LINE__);			\
})

#define set_next_hva(val, note)	({				\
	unsigned long old_val = eic->next_hva;			\
	eic->next_hva = (val);					\
	trace_printk("   next_hva=%lx %luK  %s  %s %d\n",	\
		     (val), (eic->next_hva - old_val) >> 10,	\
		     note, __func__, __LINE__);			\
})

#else

#define debug_printk(...)

#define set_restart_gpa(val, note)	({			\
	eic->restart_gpa = (val);				\
})

#define set_next_hva(val, note)	({				\
	eic->next_hva = (val);					\
})

#endif

static struct proc_dir_entry* dir_entry;

static unsigned long pagetype_size[16] = {
	[PTE_ACCESSED]	= PAGE_SIZE,	/* 4k page */
	[PMD_ACCESSED]	= PMD_SIZE,	/* 2M page */
	[PUD_PRESENT]	= PUD_SIZE,	/* 1G page */

	[PTE_DIRTY]	= PAGE_SIZE,
	[PMD_DIRTY]	= PMD_SIZE,

	[PTE_IDLE]	= PAGE_SIZE,
	[PMD_IDLE]	= PMD_SIZE,
	[PMD_IDLE_PTES] = PMD_SIZE,

	[PTE_HOLE]	= PAGE_SIZE,
	[PMD_HOLE]	= PMD_SIZE,
};

static void u64_to_u8(uint64_t n, uint8_t *p)
{
	p += sizeof(uint64_t) - 1;

	*p-- = n; n >>= 8;
	*p-- = n; n >>= 8;
	*p-- = n; n >>= 8;
	*p-- = n; n >>= 8;

	*p-- = n; n >>= 8;
	*p-- = n; n >>= 8;
	*p-- = n; n >>= 8;
	*p   = n;
}

static void dump_eic(struct ept_idle_ctrl *eic)
{
	debug_printk("ept_idle_ctrl: pie_read=%d pie_read_max=%d buf_size=%d "
		     "bytes_copied=%d next_hva=%lx restart_gpa=%lx "
		     "gpa_to_hva=%lx\n",
		     eic->pie_read,
		     eic->pie_read_max,
		     eic->buf_size,
		     eic->bytes_copied,
		     eic->next_hva,
		     eic->restart_gpa,
		     eic->gpa_to_hva);
}

static void eic_report_addr(struct ept_idle_ctrl *eic, unsigned long addr)
{
	unsigned long hva;
	eic->kpie[eic->pie_read++] = PIP_CMD_SET_HVA;
	hva = addr;
	u64_to_u8(hva, &eic->kpie[eic->pie_read]);
	eic->pie_read += sizeof(uint64_t);
	debug_printk("eic_report_addr %lx\n", addr);
	dump_eic(eic);
}

static int eic_add_page(struct ept_idle_ctrl *eic,
			unsigned long addr,
			unsigned long next,
			enum ProcIdlePageType page_type)
{
	int page_size = pagetype_size[page_type];

	debug_printk("eic_add_page addr=%lx next=%lx "
		     "page_type=%d pagesize=%dK\n",
		     addr, next, (int)page_type, (int)page_size >> 10);
	dump_eic(eic);

	/* align kernel/user vision of cursor position */
	next = round_up(next, page_size);

	if (!eic->pie_read ||
	    addr + eic->gpa_to_hva != eic->next_hva) {
		/* merge hole */
		if (page_type == PTE_HOLE ||
		    page_type == PMD_HOLE) {
			set_restart_gpa(next, "PTE_HOLE|PMD_HOLE");
			return 0;
		}

		if (addr + eic->gpa_to_hva < eic->next_hva) {
			debug_printk("ept_idle: addr moves backwards\n");
			WARN_ONCE(1, "ept_idle: addr moves backwards");
		}

		if (eic->pie_read + sizeof(uint64_t) + 2 >= eic->pie_read_max) {
			set_restart_gpa(addr, "EPT_IDLE_KBUF_FULL");
			return EPT_IDLE_KBUF_FULL;
		}

		eic_report_addr(eic, round_down(addr, page_size) +
							eic->gpa_to_hva);
	} else {
		if (PIP_TYPE(eic->kpie[eic->pie_read - 1]) == page_type &&
		    PIP_SIZE(eic->kpie[eic->pie_read - 1]) < 0xF) {
			set_next_hva(next + eic->gpa_to_hva, "IN-PLACE INC");
			set_restart_gpa(next, "IN-PLACE INC");
			eic->kpie[eic->pie_read - 1]++;
			WARN_ONCE(page_size < next-addr, "next-addr too large");
			return 0;
		}
		if (eic->pie_read >= eic->pie_read_max) {
			set_restart_gpa(addr, "EPT_IDLE_KBUF_FULL");
			return EPT_IDLE_KBUF_FULL;
		}
	}

	set_next_hva(next + eic->gpa_to_hva, "NEW-ITEM");
	set_restart_gpa(next, "NEW-ITEM");
	eic->kpie[eic->pie_read] = PIP_COMPOSE(page_type, 1);
	eic->pie_read++;

	return 0;
}

// Borrowed fronm zhou, jianshi <jianshi.zhou@intel.com> and modified by yy, thanks to jianshi.
static int get_mm_and_kvm_by_pid(pid_t nr,
                                 struct mm_struct** mmp,
                                 struct kvm** kvmp)
{
	struct task_struct* task;
	struct files_struct* files;
	struct kvm* kvm = NULL;
	struct mm_struct* mm = NULL;
	struct pid* pid;
	int fd, max_fds;

	rcu_read_lock();

	if(!(pid = find_vpid(nr))) {
		rcu_read_unlock();
		printk(KERN_ERR"failed to get vpid for pid = %d\n", nr);
		return -ESRCH;
	}

	if(!(task = pid_task(pid, PIDTYPE_PID))){
		rcu_read_unlock();
		printk(KERN_ERR"failed to get task_struct for pid = %d\n", nr);
		return -ESRCH;
	}

	// kthread has no mm_struct*
	mm = get_task_mm(task);
	if (!mm) {
		rcu_read_unlock();
		printk(KERN_ERR"faild to get mm_struct for pid = %d\n", nr);
		return -ESRCH;
	}

	files = task->files;
	max_fds = files_fdtable(files)->max_fds;
	for(fd = 0; fd < max_fds; fd++) {
		struct file* file;
		char buffer[32];
		char* fname;

		if(!(file = fcheck_files(files, fd)))
			continue;

		fname = d_path(&(file->f_path), buffer, sizeof(buffer));
		if(fname < buffer || fname >= buffer + sizeof(buffer))
			continue;

		if(strcmp(fname, "anon_inode:kvm-vm") == 0) {
			kvm = file->private_data;
			if (kvm)
				kvm_get_kvm(kvm);
            break;
		}
	}

	rcu_read_unlock();
	*kvmp = kvm;
	*mmp = mm;

    return 0;
}


static int ept_pte_range(struct ept_idle_ctrl *eic,
			 pmd_t *pmd, unsigned long addr, unsigned long end)
{
	pte_t *pte;
	enum ProcIdlePageType page_type;
	int err = 0;

	pte = pte_offset_kernel(pmd, addr);
	do {
		if (KVM_CHECK_INVALID_SPTE(pte->pte)) {
			page_type = PTE_IDLE;
		} else if (!ept_pte_present(*pte))
			page_type = PTE_HOLE;
		else if (!test_and_clear_bit(_PAGE_BIT_EPT_ACCESSED,
					     (unsigned long *) &pte->pte))
			page_type = PTE_IDLE;
		else {
			page_type = PTE_ACCESSED;
			if (eic->flags & SCAN_DIRTY_PAGE) {
				if (test_and_clear_bit(_PAGE_BIT_EPT_DIRTY,
						(unsigned long *) &pte->pte))
					page_type = PTE_DIRTY;
			}
		}

		err = eic_add_page(eic, addr, addr + PAGE_SIZE, page_type);
		if (err)
			break;
	} while (pte++, addr += PAGE_SIZE, addr != end);

	return err;
}

static int ept_pmd_range(struct ept_idle_ctrl *eic,
			 pud_t *pud, unsigned long addr, unsigned long end)
{
	pmd_t *pmd;
	unsigned long next;
	enum ProcIdlePageType page_type;
	enum ProcIdlePageType pte_page_type;
	int err = 0;

	if (eic->flags & SCAN_HUGE_PAGE)
		pte_page_type = PMD_IDLE_PTES;
	else
		pte_page_type = IDLE_PAGE_TYPE_MAX;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (KVM_CHECK_INVALID_SPTE(pmd->pmd)) {
			page_type = PMD_IDLE;
		} else if (!ept_pmd_present(*pmd))
			page_type = PMD_HOLE;	/* likely won't hit here */
		else if (!test_and_clear_bit(_PAGE_BIT_EPT_ACCESSED,
					     (unsigned long *)pmd)) {
			if (pmd_large(*pmd))
				page_type = PMD_IDLE;
			else if (eic->flags & SCAN_SKIM_IDLE)
				page_type = PMD_IDLE_PTES;
			else
				page_type = pte_page_type;
		} else if (pmd_large(*pmd)) {
			page_type = PMD_ACCESSED;
			if (eic->flags & SCAN_DIRTY_PAGE) {
				if (test_and_clear_bit(_PAGE_BIT_EPT_DIRTY,
						(unsigned long *) pmd))
					page_type = PMD_DIRTY;
			}

		} else
			page_type = pte_page_type;

		if (page_type != IDLE_PAGE_TYPE_MAX)
			err = eic_add_page(eic, addr, next, page_type);
		else
			err = ept_pte_range(eic, pmd, addr, next);
		if (err)
			break;
	} while (pmd++, addr = next, addr != end);

	return err;
}

static int ept_pud_range(struct ept_idle_ctrl *eic,
			 p4d_t *p4d, unsigned long addr, unsigned long end)
{
	pud_t *pud;
	unsigned long next;
	int err = 0;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);

		if (!ept_pud_present(*pud)) {
			set_restart_gpa(next, "PUD_HOLE");
			continue;
		}

		if (pud_large(*pud))
			err = eic_add_page(eic, addr, next, PUD_PRESENT);
		else
			err = ept_pmd_range(eic, pud, addr, next);

		if (err)
			break;
	} while (pud++, addr = next, addr != end);

	return err;
}

static int ept_p4d_range(struct ept_idle_ctrl *eic,
			 pgd_t *pgd, unsigned long addr, unsigned long end)
{
	p4d_t *p4d;
	unsigned long next;
	int err = 0;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (!ept_p4d_present(*p4d)) {
			set_restart_gpa(next, "P4D_HOLE");
			continue;
		}

		err = ept_pud_range(eic, p4d, addr, next);
		if (err)
			break;
	} while (p4d++, addr = next, addr != end);

	return err;
}

static int ept_page_range(struct ept_idle_ctrl *eic,
			  unsigned long addr,
			  unsigned long end)
{
	struct kvm_vcpu *vcpu;
	struct kvm_mmu *mmu;
	pgd_t *ept_root;
	pgd_t *pgd;
	unsigned long next;
	int err = 0;

	BUG_ON(addr >= end);

	spin_lock(&eic->kvm->mmu_lock);

	vcpu = kvm_get_vcpu(eic->kvm, 0);
	if (!vcpu) {
		spin_unlock(&eic->kvm->mmu_lock);
		return -EINVAL;
	}

	mmu = kvm_arch_mmu_pointer(vcpu);
	if (!VALID_PAGE(mmu->root_hpa)) {
		spin_unlock(&eic->kvm->mmu_lock);
		return -EINVAL;
	}

	ept_root = __va(mmu->root_hpa);

	spin_unlock(&eic->kvm->mmu_lock);
	local_irq_disable();
	pgd = pgd_offset_pgd(ept_root, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (!ept_pgd_present(*pgd)) {
			set_restart_gpa(next, "PGD_HOLE");
			continue;
		}

		err = ept_p4d_range(eic, pgd, addr, next);
		if (err)
			break;
	} while (pgd++, addr = next, addr != end);
	local_irq_enable();
	return err;
}

static int init_ept_idle_ctrl_buffer(struct ept_idle_ctrl *eic)
{
	eic->pie_read = 0;
	eic->pie_read_max = min(EPT_IDLE_KBUF_SIZE,
				eic->buf_size - eic->bytes_copied);
	/* reserve space for PIP_CMD_SET_HVA in the end */
	eic->pie_read_max -= sizeof(uint64_t) + 1;

	/*
	* Align with EPT_IDLE_KBUF_FULL
	* logic in eic_add_page(), to avoid eic->pie_read = 0 when
	* EPT_IDLE_KBUF_FULL happened.
	*/
	if (eic->pie_read_max <= sizeof(uint64_t) + 2)
		return EPT_IDLE_KBUF_FULL;

	memset(eic->kpie, 0, sizeof(eic->kpie));
	return 0;
}

static void setup_ept_idle_ctrl(struct ept_idle_ctrl *eic, void* buf,
                                int buf_size, unsigned int flags)
{
	eic->buf = buf;
	eic->buf_size = buf_size;
	eic->bytes_copied = 0;
	eic->next_hva = 0;
	eic->gpa_to_hva = 0;
	eic->restart_gpa = 0;
	eic->last_va = 0;
	eic->flags = flags;
}

static int ept_idle_copy_user(struct ept_idle_ctrl *eic,
			      unsigned long start, unsigned long end)
{
	int bytes_read;
	int lc = 0;	/* last copy? */
	int ret;

	debug_printk("ept_idle_copy_user %lx %lx\n", start, end);
	dump_eic(eic);

	/* Break out of loop on no more progress. */
	if (!eic->pie_read) {
		lc = 1;
		if (start < end)
			start = end;
	}

	if (start >= end && start > eic->next_hva) {
		set_next_hva(start, "TAIL-HOLE");
		eic_report_addr(eic, start);
	}

	bytes_read = eic->pie_read;
	if (!bytes_read)
		return 1;

	ret = copy_to_user(eic->buf, eic->kpie, bytes_read);
	if (ret)
		return -EFAULT;

	eic->buf += bytes_read;
	eic->bytes_copied += bytes_read;
	if (eic->bytes_copied >= eic->buf_size)
		return EPT_IDLE_BUF_FULL;
	if (lc)
		return lc;
	ret = init_ept_idle_ctrl_buffer(eic);
	if (ret)
		return ret;

	cond_resched();
	return 0;
}

/*
 * Depending on whether hva falls in a memslot:
 *
 * 1) found => return gpa and remaining memslot size in *addr_range
 *
 *                 |<----- addr_range --------->|
 *         [               mem slot             ]
 *                 ^hva
 *
 * 2) not found => return hole size in *addr_range
 *
 *                 |<----- addr_range --------->|
 *                                              [   first mem slot above hva  ]
 *                 ^hva
 *
 * If hva is above all mem slots, *addr_range will be ~0UL. We can finish read(2).
 */
static unsigned long ept_idle_find_gpa(struct ept_idle_ctrl *eic,
				       unsigned long hva,
				       unsigned long *addr_range)
{
	struct kvm *kvm = eic->kvm;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	unsigned long hva_end;
	gfn_t gfn;

	*addr_range = ~0UL;
	mutex_lock(&kvm->slots_lock);
	slots = kvm_memslots(eic->kvm);
	kvm_for_each_memslot(memslot, slots) {
		hva_end = memslot->userspace_addr +
		    (memslot->npages << PAGE_SHIFT);

		if (hva >= memslot->userspace_addr && hva < hva_end) {
			gpa_t gpa;
			gfn = hva_to_gfn_memslot(hva, memslot);
			*addr_range = hva_end - hva;
			gpa = gfn_to_gpa(gfn);
			debug_printk("ept_idle_find_gpa slot %lx=>%llx %lx=>%llx "
				     "delta %llx size %lx\n",
				     memslot->userspace_addr,
				     gfn_to_gpa(memslot->base_gfn),
				     hva, gpa,
				     hva - gpa,
				     memslot->npages << PAGE_SHIFT);
			mutex_unlock(&kvm->slots_lock);
			return gpa;
		}

		if (memslot->userspace_addr > hva)
			*addr_range = min(*addr_range,
					  memslot->userspace_addr - hva);
	}
	mutex_unlock(&kvm->slots_lock);
	return INVALID_PAGE;
}

static int ept_idle_supports_cpu(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;
	struct kvm_mmu *mmu;
	int ret;

	vcpu = kvm_get_vcpu(kvm, 0);
	if (!vcpu)
		return -EINVAL;

	spin_lock(&kvm->mmu_lock);
	mmu = kvm_arch_mmu_pointer(vcpu);
	if (kvm_mmu_ad_disabled(mmu)) {
		printk(KERN_NOTICE
		       "CPU does not support EPT A/D bits tracking\n");
		ret = -EINVAL;
	} else if (mmu->shadow_root_level != 4 + (! !pgtable_l5_enabled())) {
		printk(KERN_NOTICE "Unsupported EPT level %d\n",
		       mmu->shadow_root_level);
		ret = -EINVAL;
	} else
		ret = 0;
	spin_unlock(&kvm->mmu_lock);

	return ret;
}

static int ept_idle_walk_hva_range(struct ept_idle_ctrl *eic,
				   unsigned long start, unsigned long end)
{
	unsigned long gpa_addr;
	unsigned long addr_range;
	unsigned long va_end;
	int ret;

	ret = ept_idle_supports_cpu(eic->kvm);
	if (ret)
		return ret;

	ret = init_ept_idle_ctrl_buffer(eic);
	if (ret)
		return ret;

	for (; start < end;) {
		gpa_addr = ept_idle_find_gpa(eic, start, &addr_range);

		if (gpa_addr == INVALID_PAGE) {
			eic->gpa_to_hva = 0;
			if (addr_range == ~0UL) /* beyond max virtual address */ {
				set_restart_gpa(TASK_SIZE, "EOF");
				va_end = end;
			} else {
				start += addr_range;
				set_restart_gpa(start, "OUT-OF-SLOT");
				va_end = start;
			}
		} else {
			eic->gpa_to_hva = start - gpa_addr;
			ept_page_range(eic, gpa_addr, gpa_addr + addr_range);
			va_end = eic->gpa_to_hva + gpa_addr + addr_range;
		}

		start = eic->restart_gpa + eic->gpa_to_hva;
		ret = ept_idle_copy_user(eic, start, va_end);
		if (ret)
			break;
	}

	if (eic->bytes_copied)
		ret = 0;
	return ret;
}

static ssize_t mm_idle_read(struct file *file, char *buf,
			    size_t count, loff_t *ppos);

static ssize_t ept_idle_read(struct file *file, char *buf,
			     size_t count, loff_t *ppos)
{
	struct ept_idle_ctrl *eic = file->private_data;
	unsigned long hva_start = *ppos;
	unsigned long hva_end = hva_start + (count << (3 + PAGE_SHIFT));
	int ret;

	if (!eic) {
		printk(KERN_ERR"NULL eic instance\n");
		return -ENOMEM;
	}

	if (hva_start >= TASK_SIZE) {
		debug_printk("ept_idle_read past TASK_SIZE: %lx %lx\n",
			     hva_start, TASK_SIZE);
		return 0;
	}

	if (!eic->mm)
		return -EINVAL;

	if (!eic->kvm)
		return mm_idle_read(file, buf, count, ppos);

	if (hva_end <= hva_start) {
		debug_printk("ept_idle_read past EOF: %lx %lx\n",
			     hva_start, hva_end);
		return 0;
	}
	if (*ppos & (PAGE_SIZE - 1)) {
		debug_printk("ept_idle_read unaligned ppos: %lx\n",
			     hva_start);
		return -EINVAL;
	}
	if (count < EPT_IDLE_BUF_MIN) {
		debug_printk("ept_idle_read small count: %lx\n",
			     (unsigned long)count);
		return -EINVAL;
	}

	setup_ept_idle_ctrl(eic, buf, count, file->f_flags);

	ret = ept_idle_walk_hva_range(eic, hva_start, hva_end);
	if (ret)
		goto out_kvm;

	ret = eic->bytes_copied;
	*ppos = eic->next_hva;
	debug_printk("ppos=%lx bytes_copied=%d\n",
		     eic->next_hva, ret);
out_kvm:
	return ret;
}

static int ept_idle_open(struct inode *inode, struct file *file)
{
	struct ept_idle_ctrl* eic;

	if (!try_module_get(THIS_MODULE)) {
		file->private_data = NULL;
		return -EBUSY;
	}

	eic = kzalloc(sizeof(*eic), GFP_KERNEL);
	file->private_data = eic;
	if (!eic) {
		printk(KERN_ERR"Failed to alloc ept_idle_ctrl \n");
		return -ENOMEM;
	}

	return 0;
}

static int ept_idle_release(struct inode *inode, struct file *file)
{
	struct kvm *kvm;
	struct ept_idle_ctrl* eic = file->private_data;
	int ret = 0;

	if (!eic)
		goto out;

	if (eic->kvm) {
		kvm = eic->kvm;
		spin_lock(&kvm->mmu_lock);
		kvm_flush_remote_tlbs(kvm);
		spin_unlock(&kvm->mmu_lock);

		kvm_put_kvm(kvm);
	} else if (eic->mm) {
		copied_flush_tlb_mm_range(eic->mm, 0UL, TLB_FLUSH_ALL, 0UL, true);
	}

	if (eic->mm)
		mmput(eic->mm);

	kfree(eic);
out:
	module_put(THIS_MODULE);
	return ret;
}

static int mm_idle_pte_range(struct ept_idle_ctrl *eic, pmd_t *pmd,
			     unsigned long addr, unsigned long next)
{
	enum ProcIdlePageType page_type;
	pte_t *pte;
	int err = 0;

	pte = pte_offset_kernel(pmd, addr);
	do {
		if (!pte_present(*pte))
			page_type = PTE_HOLE;
		else if (!test_and_clear_bit(_PAGE_BIT_ACCESSED,
					     (unsigned long *) &pte->pte))
			page_type = PTE_IDLE;
		else {
			page_type = PTE_ACCESSED;
		}

		err = eic_add_page(eic, addr, addr + PAGE_SIZE, page_type);
		if (err)
			break;
	} while (pte++, addr += PAGE_SIZE, addr != next);

	return err;
}

static int mm_idle_pmd_entry(pmd_t *pmd, unsigned long addr,
			     unsigned long next, struct mm_walk *walk)
{
	struct ept_idle_ctrl *eic = walk->private;
	enum ProcIdlePageType page_type;
	enum ProcIdlePageType pte_page_type;
	int err;

	/*
	 * Skip duplicate PMD_IDLE_PTES: when the PMD crosses VMA boundary,
	 * walk_page_range() can call on the same PMD twice.
	 */
	if ((addr & PMD_MASK) == (eic->last_va & PMD_MASK)) {
		debug_printk("ignore duplicate addr %lx %lx\n",
			     addr, eic->last_va);
		return 0;
	}
	eic->last_va = addr;

	if (eic->flags & SCAN_HUGE_PAGE)
		pte_page_type = PMD_IDLE_PTES;
	else
		pte_page_type = IDLE_PAGE_TYPE_MAX;
#if 0
	if (!pmd_present(*pmd))
		page_type = PMD_HOLE;
	else if (!test_and_clear_bit(_PAGE_BIT_ACCESSED, (unsigned long *)pmd)) {
		if (pmd_large(*pmd))
			page_type = PMD_IDLE;
		else if (eic->flags & SCAN_SKIM_IDLE)
			page_type = PMD_IDLE_PTES;
		else
			page_type = pte_page_type;
	} else if (pmd_large(*pmd)) {
		page_type = PMD_ACCESSED;
	} else
		page_type = pte_page_type;
#else
	// don't clear A bit in PMD for 4K page, which conflicted with pmd_bad()
	if (!pmd_present(*pmd))
		page_type = PMD_HOLE;
	else if (!pmd_large(*pmd))
		page_type = pte_page_type;
	else if (!test_and_clear_bit(_PAGE_BIT_ACCESSED, (unsigned long *)pmd))
		page_type = PMD_IDLE;
	else
		page_type = PMD_ACCESSED;
#endif
	if (page_type != IDLE_PAGE_TYPE_MAX)
		err = eic_add_page(eic, addr, next, page_type);
	else
		err = mm_idle_pte_range(eic, pmd, addr, next);

	return err;
}

static int mm_idle_pud_entry(pud_t *pud, unsigned long addr,
			     unsigned long next, struct mm_walk *walk)
{
	struct ept_idle_ctrl *eic = walk->private;

	if ((addr & PUD_MASK) != (eic->last_va & PUD_MASK)) {
		eic_add_page(eic, addr, next, PUD_PRESENT);
		eic->last_va = addr;
	}
	return 1;
}

static int mm_idle_test_walk(unsigned long start, unsigned long end,
			     struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;

	if (vma->vm_file) {
		if ((vma->vm_flags & (VM_WRITE|VM_MAYSHARE)) == VM_WRITE)
		    return 0;
		return 1;
	}

	return 0;
}

static int mm_idle_walk_range(struct ept_idle_ctrl *eic,
			      unsigned long start,
			      unsigned long end,
			      struct mm_walk *walk)
{
	struct vm_area_struct *vma;
	int ret;

	ret = init_ept_idle_ctrl_buffer(eic);
	if (ret)
		return ret;

	for (; start < end;)
	{
		down_read(&walk->mm->mmap_sem);
		vma = find_vma(walk->mm, start);
		if (vma) {
			if (end > vma->vm_start) {
				local_irq_disable();
				ret = ept_idle_walk_page_range(start, end, walk);
				local_irq_enable();
			} else
				set_restart_gpa(vma->vm_start, "VMA-HOLE");
		} else
			set_restart_gpa(TASK_SIZE, "EOF");
		up_read(&walk->mm->mmap_sem);

		WARN_ONCE(eic->gpa_to_hva, "non-zero gpa_to_hva");
		start = eic->restart_gpa;
		ret = ept_idle_copy_user(eic, start, end);
		if (ret)
			break;
	}

	if (eic->bytes_copied) {
		if (ret != EPT_IDLE_BUF_FULL && eic->next_hva < end)
			debug_printk("partial scan: next_hva=%lx end=%lx\n",
				     eic->next_hva, end);
		ret = 0;
	} else
		WARN_ONCE(1, "nothing read");
	return ret;
}

static ssize_t mm_idle_read(struct file *file, char *buf,
			    size_t count, loff_t *ppos)
{
	struct ept_idle_ctrl *eic = file->private_data;
	struct mm_walk mm_walk = {};
	unsigned long va_start = *ppos;
	unsigned long va_end = va_start + (count << (3 + PAGE_SHIFT));
	int ret;

	if (va_end <= va_start) {
		debug_printk("mm_idle_read past EOF: %lx %lx\n",
			     va_start, va_end);
		return 0;
	}
	if (*ppos & (PAGE_SIZE - 1)) {
		debug_printk("mm_idle_read unaligned ppos: %lx\n",
			     va_start);
		return -EINVAL;
	}
	if (count < EPT_IDLE_BUF_MIN) {
		debug_printk("mm_idle_read small count: %lx\n",
			     (unsigned long)count);
		return -EINVAL;
	}

	setup_ept_idle_ctrl(eic, buf, count, file->f_flags);

	mm_walk.mm = eic->mm;
	mm_walk.pmd_entry = mm_idle_pmd_entry;
	mm_walk.pud_entry = mm_idle_pud_entry;
	mm_walk.test_walk = mm_idle_test_walk;
	mm_walk.private = eic;

	ret = mm_idle_walk_range(eic, va_start, va_end, &mm_walk);
	if (ret)
		goto out_mm;

	ret = eic->bytes_copied;
	*ppos = eic->next_hva;
	debug_printk("ppos=%lx bytes_copied=%d\n",
		     eic->next_hva, ret);
out_mm:
	return ret;
}

// copied from fs/proc/base.c mem_lseek
static loff_t ept_idle_lseek(struct file *file, loff_t offset, int orig)
{
	switch (orig) {
	case 0:
		file->f_pos = offset;
		break;
	case 1:
		file->f_pos += offset;
		break;
	default:
		return -EINVAL;
	}
	force_successful_syscall_return();
	return file->f_pos;
}

static long ept_idle_ioctl(struct file *filp, unsigned int ioctl,
							unsigned long arg)
{
	struct ept_idle_ctrl* eic;
	pid_t target_pid = (pid_t)arg;
	long ret;

	eic = filp->private_data;
	if (!eic) {
		printk(KERN_ERR"NULL eic instance \n");
		return -ENOMEM;
	}

	switch(ioctl) {
	case IDLE_PAGE_SET_PID:
		ret = get_mm_and_kvm_by_pid(target_pid, &eic->mm, &eic->kvm);
		break;
	default:
		ret = -EINVAL;
		break;
    }

	return ret;
}

struct file_operations proc_idle_page_oprations = {
	.llseek	        = ept_idle_lseek,
	.read           = ept_idle_read,
	.open           = ept_idle_open,
	.release        = ept_idle_release,
	.unlocked_ioctl = ept_idle_ioctl
};

static int ept_idle_entry(void)
{
	dir_entry = proc_create("idle_pages", S_IWUSR | S_IRUGO, NULL,
							&proc_idle_page_oprations);
	if (!dir_entry) {
		printk("Failed to create idle_pages in /porc\n");
		return -ENOMEM;
	}

	return 0;
}

static void ept_idle_exit(void)
{
	if (dir_entry)
		proc_remove(dir_entry);
}

MODULE_LICENSE("GPL");
module_init(ept_idle_entry);
module_exit(ept_idle_exit);
