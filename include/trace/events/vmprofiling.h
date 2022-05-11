#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmprofiling

#if !defined(_TRACE_VMPROFILING_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VMPROFILING_H

#include <linux/tracepoint.h>

TRACE_EVENT(copy_page_range,

	TP_PROTO(unsigned long call_site, const void *ptr, const char *name),

	TP_ARGS(call_site, ptr, name),

	TP_STRUCT__entry(
		__field(	unsigned long,	call_site	)
		__field(	const void *,	ptr		)
		__string(	name,	name	)
	),

	TP_fast_assign(
		__entry->call_site	= call_site;
		__entry->ptr		= ptr;
		__assign_str(name, name);
	),

	TP_printk("call_site=%pS ptr=%p name=%s",
		  (void *)__entry->call_site, __entry->ptr, __get_str(name))
);

#endif /* _TRACE_VMPROFILING_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
