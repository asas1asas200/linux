#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmprofiling

#if !defined(_TRACE_VMPROFILING_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VMPROFILING_H

#include <linux/tracepoint.h>

TRACE_EVENT(copy_page_range,

	TP_PROTO(const char *event_name, unsigned int ticket, unsigned long pgtable_bytes),

	TP_ARGS(event_name, ticket, pgtable_bytes),

	TP_STRUCT__entry(
		__string(	event_name,	event_name	)
		__field(	unsigned int,	ticket		)
		__field(	unsigned long,	pgtable_bytes	)
	),

	TP_fast_assign(
		__entry->pgtable_bytes	= pgtable_bytes;
		__entry->ticket		= ticket;
		__assign_str(event_name, event_name);
	),

	TP_printk("vmp: [%s] #%u pgtable bytes=%lu",
		  __get_str(event_name), __entry->ticket, __entry->pgtable_bytes)
);

#endif /* _TRACE_VMPROFILING_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
