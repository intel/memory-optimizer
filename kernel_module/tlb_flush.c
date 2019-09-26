#include "tlb_flush.h"


/* copied from 4.20 kernel:
 * See Documentation/x86/tlb.txt for details.  We choose 33
 * because it is large enough to cover the vast majority (at
 * least 95%) of allocations, and is small enough that we are
 * confident it will not cause too much overhead.  Each single
 * flush is about 100 ns, so this caps the maximum overhead at
 * _about_ 3,000 ns.
 *
 * This is in units of pages.
 */
static unsigned long copied_tlb_single_page_flush_ceiling __read_mostly = 33;


static bool copied_tlb_is_not_lazy(int cpu, void *data)
{
	return !per_cpu(cpu_tlbstate.is_lazy, cpu);
}


/*
 * flush_tlb_func_common()'s memory ordering requirement is that any
 * TLB fills that happen after we flush the TLB are ordered after we
 * read active_mm's tlb_gen.  We don't need any explicit barriers
 * because all x86 flush operations are serializing and the
 * atomic64_read operation won't be reordered by the compiler.
 */
static void copied_flush_tlb_func_common(const struct flush_tlb_info *f,
				  bool local, enum tlb_flush_reason reason)
{
	/*
	 * We have three different tlb_gen values in here.  They are:
	 *
	 * - mm_tlb_gen:     the latest generation.
	 * - local_tlb_gen:  the generation that this CPU has already caught
	 *                   up to.
	 * - f->new_tlb_gen: the generation that the requester of the flush
	 *                   wants us to catch up to.
	 */
	struct mm_struct *loaded_mm = this_cpu_read(cpu_tlbstate.loaded_mm);
	u32 loaded_mm_asid = this_cpu_read(cpu_tlbstate.loaded_mm_asid);
	u64 mm_tlb_gen = atomic64_read(&loaded_mm->context.tlb_gen);
	u64 local_tlb_gen = this_cpu_read(cpu_tlbstate.ctxs[loaded_mm_asid].tlb_gen);

	/* This code cannot presently handle being reentered. */
	VM_WARN_ON(!irqs_disabled());

	/*
	 * The init_mm is unexported variable, but we don't need
	 * check this here for our case, we just want to flush
	 * the TLB on remote CPU cores which is running the task
	 * using f->mm as memory space
	 */
#if 0
	if (unlikely(loaded_mm == &init_mm))
		return;
#else
	if (unlikely(loaded_mm != f->mm)) {
		return;
	}
#endif

	VM_WARN_ON(this_cpu_read(cpu_tlbstate.ctxs[loaded_mm_asid].ctx_id) !=
		   loaded_mm->context.ctx_id);

	/*
	 * The caller of this function will set is_lazy to false explicitly
	 * so we don't need handle this case, just skip this.
	 */
 #if 0
	if (this_cpu_read(cpu_tlbstate.is_lazy)) {
		/*
		 * We're in lazy mode.  We need to at least flush our
		 * paging-structure cache to avoid speculatively reading
		 * garbage into our TLB.  Since switching to init_mm is barely
		 * slower than a minimal flush, just switch to init_mm.
		 *
		 * This should be rare, with native_flush_tlb_others skipping
		 * IPIs to lazy TLB mode CPUs.
		 */
		switch_mm_irqs_off(NULL, &init_mm, NULL);
		return;
	}
#endif

	if (unlikely(local_tlb_gen == mm_tlb_gen)) {
		/*
		 * There's nothing to do: we're already up to date.  This can
		 * happen if two concurrent flushes happen -- the first flush to
		 * be handled can catch us all the way up, leaving no work for
		 * the second flush.
		 */
		// trace_tlb_flush(reason, 0);
		return;
	}

	WARN_ON_ONCE(local_tlb_gen > mm_tlb_gen);
	WARN_ON_ONCE(f->new_tlb_gen > mm_tlb_gen);

	/*
	 * If we get to this point, we know that our TLB is out of date.
	 * This does not strictly imply that we need to flush (it's
	 * possible that f->new_tlb_gen <= local_tlb_gen), but we're
	 * going to need to flush in the very near future, so we might
	 * as well get it over with.
	 *
	 * The only question is whether to do a full or partial flush.
	 *
	 * We do a partial flush if requested and two extra conditions
	 * are met:
	 *
	 * 1. f->new_tlb_gen == local_tlb_gen + 1.  We have an invariant that
	 *    we've always done all needed flushes to catch up to
	 *    local_tlb_gen.  If, for example, local_tlb_gen == 2 and
	 *    f->new_tlb_gen == 3, then we know that the flush needed to bring
	 *    us up to date for tlb_gen 3 is the partial flush we're
	 *    processing.
	 *
	 *    As an example of why this check is needed, suppose that there
	 *    are two concurrent flushes.  The first is a full flush that
	 *    changes context.tlb_gen from 1 to 2.  The second is a partial
	 *    flush that changes context.tlb_gen from 2 to 3.  If they get
	 *    processed on this CPU in reverse order, we'll see
	 *     local_tlb_gen == 1, mm_tlb_gen == 3, and end != TLB_FLUSH_ALL.
	 *    If we were to use __flush_tlb_one_user() and set local_tlb_gen to
	 *    3, we'd be break the invariant: we'd update local_tlb_gen above
	 *    1 without the full flush that's needed for tlb_gen 2.
	 *
	 * 2. f->new_tlb_gen == mm_tlb_gen.  This is purely an optimiation.
	 *    Partial TLB flushes are not all that much cheaper than full TLB
	 *    flushes, so it seems unlikely that it would be a performance win
	 *    to do a partial flush if that won't bring our TLB fully up to
	 *    date.  By doing a full flush instead, we can increase
	 *    local_tlb_gen all the way to mm_tlb_gen and we can probably
	 *    avoid another flush in the very near future.
	 */
	if (f->end != TLB_FLUSH_ALL &&
	    f->new_tlb_gen == local_tlb_gen + 1 &&
	    f->new_tlb_gen == mm_tlb_gen) {
		/* Partial flush */
		unsigned long nr_invalidate = (f->end - f->start) >> f->stride_shift;
		unsigned long addr = f->start;

		while (addr < f->end) {
			__flush_tlb_one_user(addr);
			addr += 1UL << f->stride_shift;
		}
		if (local)
			count_vm_tlb_events(NR_TLB_LOCAL_FLUSH_ONE, nr_invalidate);
		// trace_tlb_flush(reason, nr_invalidate);
	} else {
		/* Full flush. */
		local_flush_tlb();
		if (local)
			count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		// trace_tlb_flush(reason, TLB_FLUSH_ALL);
	}

	/* Both paths above update our state to mm_tlb_gen. */
	this_cpu_write(cpu_tlbstate.ctxs[loaded_mm_asid].tlb_gen, mm_tlb_gen);
}

static void copied_flush_tlb_func_remote(void *info)
{
	const struct flush_tlb_info *f = info;
	bool saved_lazy;

	inc_irq_stat(irq_tlb_count);

	if (f->mm && f->mm != this_cpu_read(cpu_tlbstate.loaded_mm))
		return;

	saved_lazy = this_cpu_read(cpu_tlbstate.is_lazy);
	this_cpu_write(cpu_tlbstate.is_lazy, false);

	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH_RECEIVED);
	copied_flush_tlb_func_common(f, false, TLB_REMOTE_SHOOTDOWN);

	this_cpu_write(cpu_tlbstate.is_lazy, saved_lazy);
}


static void copied_native_flush_tlb_others(const struct cpumask *cpumask,
			     const struct flush_tlb_info *info)
{
	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH);

#if 0
	if (info->end == TLB_FLUSH_ALL)
		trace_tlb_flush(TLB_REMOTE_SEND_IPI, TLB_FLUSH_ALL);
	else
		trace_tlb_flush(TLB_REMOTE_SEND_IPI,
				(info->end - info->start) >> PAGE_SHIFT);
#endif
	/*
	 * Use non-UV system way in first version to reduce porting affort,
	 * we will support UV system later if necessary
	 */
#if 0
	if (is_uv_system()) {
		/*
		 * This whole special case is confused.  UV has a "Broadcast
		 * Assist Unit", which seems to be a fancy way to send IPIs.
		 * Back when x86 used an explicit TLB flush IPI, UV was
		 * optimized to use its own mechanism.  These days, x86 uses
		 * smp_call_function_many(), but UV still uses a manual IPI,
		 * and that IPI's action is out of date -- it does a manual
		 * flush instead of calling flush_tlb_func_remote().  This
		 * means that the percpu tlb_gen variables won't be updated
		 * and we'll do pointless flushes on future context switches.
		 *
		 * Rather than hooking native_flush_tlb_others() here, I think
		 * that UV should be updated so that smp_call_function_many(),
		 * etc, are optimal on UV.
		 */
		unsigned int cpu;

		cpu = smp_processor_id();
		cpumask = uv_flush_tlb_others(cpumask, info);
		if (cpumask)
			smp_call_function_many(cpumask, copied_flush_tlb_func_remote,
					       (void *)info, 1);
		return;
	}
#endif

	/*
	 * If no page tables were freed, we can skip sending IPIs to
	 * CPUs in lazy TLB mode. They will flush the CPU themselves
	 * at the next context switch.
	 *
	 * However, if page tables are getting freed, we need to send the
	 * IPI everywhere, to prevent CPUs in lazy TLB mode from tripping
	 * up on the new contents of what used to be page tables, while
	 * doing a speculative memory access.
	 */
	if (info->freed_tables)
		smp_call_function_many(cpumask, copied_flush_tlb_func_remote,
			       (void *)info, 1);
	else
		on_each_cpu_cond_mask(copied_tlb_is_not_lazy, copied_flush_tlb_func_remote,
				(void *)info, 1, GFP_ATOMIC, cpumask);
}


void copied_flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned int stride_shift,
				bool freed_tables)
{
	int cpu;

	struct flush_tlb_info info __aligned(SMP_CACHE_BYTES) = {
		.mm = mm,
		.stride_shift = stride_shift,
		.freed_tables = freed_tables,
	};

	cpu = get_cpu();

	/* This is also a barrier that synchronizes with switch_mm(). */
	info.new_tlb_gen = inc_mm_tlb_gen(mm);

	/* Should we flush just the requested range? */
	if ((end != TLB_FLUSH_ALL) &&
	    ((end - start) >> stride_shift) <= copied_tlb_single_page_flush_ceiling) {
		info.start = start;
		info.end = end;
	} else {
		info.start = 0UL;
		info.end = TLB_FLUSH_ALL;
	}

	/* This should never happend in our case */
	if (mm == this_cpu_read(cpu_tlbstate.loaded_mm)) {
		VM_WARN_ON(irqs_disabled());
		local_irq_disable();
		copied_flush_tlb_func_common(&info, true, TLB_LOCAL_MM_SHOOTDOWN);
		local_irq_enable();
	}

	if (cpumask_any_but(mm_cpumask(mm), cpu) < nr_cpu_ids)
		copied_native_flush_tlb_others(mm_cpumask(mm), &info);

	put_cpu();
}

