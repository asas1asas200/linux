#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmprofiling

#if !defined(_TRACE_VMPROFILING_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VMPROFILING_H

#include <linux/atomic.h>
#include <linux/tracepoint.h>

#define PGTABLE_LD(pxd)                                                        \
	unsigned int nr_##pxd, unsigned int nr_present_##pxd##_entry

#define PGTABLE_DD(pxd) nr_##pxd, nr_present_##pxd##_entry

#define PGTABLE_FD(pxd)                                                        \
	__field(unsigned int, nr_##pxd)                                        \
		__field(unsigned int, nr_present_##pxd##_entry)

#define PGTABLE_FA(pxd)                                                        \
	__entry->nr_##pxd = nr_##pxd;                                          \
	__entry->nr_present_##pxd##_entry = nr_present_##pxd##_entry

#define PGTABLE_SE(pxd) #pxd "%lu nr_present_" #pxd "_entry %lu"

#define PGTABLE_EE(pxd) __entry->nr_##pxd, __entry->nr_present_##pxd##_entry,

TRACE_EVENT(
	pgtable,

	TP_PROTO(unsigned int ticket,
		 unsigned long pgtable_bytes,
		 unsigned int pinned_vm,
		 unsigned int nr_swap,
		 unsigned int nr_cow_page,
		 struct mm_rss_stat *rss,
		 unsigned int nr_present_pte_entry,
		 PGTABLE_LD(pmd),
		 PGTABLE_LD(pud),
		 PGTABLE_LD(p4d)
	),

	TP_ARGS(ticket,
		pgtable_bytes,
		pinned_vm,
		nr_swap,
		nr_cow_page,
		rss,
		nr_present_pte_entry,
		PGTABLE_DD(pmd),
		PGTABLE_DD(pud),
		PGTABLE_DD(p4d)
	),

	TP_STRUCT__entry(
		__field(unsigned int, ticket)
		__field(unsigned long, pgtable_bytes)
		__field(unsigned int, pinned_vm)
		__field(unsigned int, nr_swap)
		__field(unsigned int, nr_cow_page)
		__field(long, MM_FILEPAGES)
		__field(long, MM_ANONPAGES)
		__field(long, MM_SWAPENTS)
		__field(long, MM_SHMEMPAGES)
		__field(unsigned int, nr_present_pte_entry)
		PGTABLE_FD(pmd)
		PGTABLE_FD(pud)
		PGTABLE_FD(p4d)),

	TP_fast_assign(__entry->ticket = ticket;
		       __entry->pgtable_bytes = pgtable_bytes;
		       __entry->pinned_vm = pinned_vm;
		       __entry->nr_swap = nr_swap;
		       __entry->nr_cow_page = nr_cow_page;
		       __entry->MM_FILEPAGES =
			       atomic_long_read(&rss->count[MM_FILEPAGES]);
		       __entry->MM_ANONPAGES =
			       atomic_long_read(&rss->count[MM_ANONPAGES]);
		       __entry->MM_SWAPENTS =
			       atomic_long_read(&rss->count[MM_SWAPENTS]);
		       __entry->MM_SHMEMPAGES =
			       atomic_long_read(&rss->count[MM_SHMEMPAGES]);
		       __entry->nr_present_pte_entry = nr_present_pte_entry;
		       PGTABLE_FA(pmd); PGTABLE_FA(pud); PGTABLE_FA(p4d);
	),

	TP_printk(
		"vmp: #%u pgtable bytes=%lu, pinned_vm=%u, nr_swap=%%u, nr_cow_page=%u, MM_FILEPAGES=%ld, MM_ANONPAGES=%ld, MM_SWAPENTS=%ld, MM_SHMEMPAGES=%ld nr_present_pte_entry=%lu," PGTABLE_SE(
			pmd) "," PGTABLE_SE(pud) "," PGTABLE_SE(p4d),
		__entry->ticket,
		__entry->pgtable_bytes,
		__entry->pinned_vm,
		__entry->nr_swap,
		__entry->nr_cow_page,
		__entry->MM_FILEPAGES,
		__entry->MM_ANONPAGES,
		__entry->MM_SWAPENTS,
		__entry->MM_SHMEMPAGES,
		__entry->nr_present_pte_entry,
		PGTABLE_EE(pmd),
		PGTABLE_EE(pud),
		PGTABLE_EE(p4d)
	)
);

#endif /* _TRACE_VMPROFILING_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
