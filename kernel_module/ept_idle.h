#ifndef _EPT_IDLE_H
#define _EPT_IDLE_H

#include "ept_idle_common.h"

#define SCAN_HUGE_PAGE		O_NONBLOCK	/* only huge page */
#define SCAN_SKIM_IDLE		O_NOFOLLOW	/* stop on PMD_IDLE_PTES */
#define SCAN_DIRTY_PAGE		O_NOATIME   /* report pte/pmd dirty bit */

enum ProcIdlePageType {
	PTE_ACCESSED,	/* 4k page */
	PMD_ACCESSED,	/* 2M page */
	PUD_PRESENT,	/* 1G page */

	PTE_DIRTY,
	PMD_DIRTY,

	PTE_IDLE,
	PMD_IDLE,
	PMD_IDLE_PTES,	/* all PTE idle */

	PTE_HOLE,
	PMD_HOLE,

	PIP_CMD,

	IDLE_PAGE_TYPE_MAX
};

#define PIP_TYPE(a)		(0xf & (a >> 4))
#define PIP_SIZE(a)		(0xf & a)
#define PIP_COMPOSE(type, nr)	((type << 4) | nr)

#define PIP_CMD_SET_HVA		PIP_COMPOSE(PIP_CMD, 0)

#define _PAGE_BIT_EPT_ACCESSED	8
#define _PAGE_BIT_EPT_DIRTY		9
#define _PAGE_EPT_ACCESSED	(_AT(pteval_t, 1) << _PAGE_BIT_EPT_ACCESSED)
#define _PAGE_EPT_DIRTY	(_AT(pteval_t, 1) << _PAGE_BIT_EPT_DIRTY)

#define _PAGE_EPT_PRESENT	(_AT(pteval_t, 7))

static inline int ept_pte_present(pte_t a)
{
	return pte_flags(a) & _PAGE_EPT_PRESENT;
}

static inline int ept_pmd_present(pmd_t a)
{
	return pmd_flags(a) & _PAGE_EPT_PRESENT;
}

static inline int ept_pud_present(pud_t a)
{
	return pud_flags(a) & _PAGE_EPT_PRESENT;
}

static inline int ept_p4d_present(p4d_t a)
{
	return p4d_flags(a) & _PAGE_EPT_PRESENT;
}

static inline int ept_pgd_present(pgd_t a)
{
	return pgd_flags(a) & _PAGE_EPT_PRESENT;
}

static inline int ept_pte_accessed(pte_t a)
{
	return pte_flags(a) & _PAGE_EPT_ACCESSED;
}

static inline int ept_pmd_accessed(pmd_t a)
{
	return pmd_flags(a) & _PAGE_EPT_ACCESSED;
}

static inline int ept_pud_accessed(pud_t a)
{
	return pud_flags(a) & _PAGE_EPT_ACCESSED;
}

static inline int ept_p4d_accessed(p4d_t a)
{
	return p4d_flags(a) & _PAGE_EPT_ACCESSED;
}

static inline int ept_pgd_accessed(pgd_t a)
{
	return pgd_flags(a) & _PAGE_EPT_ACCESSED;
}

extern struct file_operations proc_ept_idle_operations;

#define EPT_IDLE_KBUF_FULL	1
#define EPT_IDLE_BUF_FULL	2
#define EPT_IDLE_BUF_MIN	(sizeof(uint64_t) * 2 + 3)

#define EPT_IDLE_KBUF_SIZE	8000

#define IDLE_PAGE_SET_PID   _IOW(0x1, 0x1, pid_t)

struct ept_idle_ctrl {
	struct mm_struct *mm;
	struct kvm *kvm;

	uint8_t kpie[EPT_IDLE_KBUF_SIZE];
	int pie_read;
	int pie_read_max;

	void __user *buf;
	int buf_size;
	int bytes_copied;

	unsigned long next_hva;		/* GPA for EPT; VA for PT */
	unsigned long gpa_to_hva;
	unsigned long restart_gpa;
	unsigned long last_va;

	unsigned int flags;
};

#endif
