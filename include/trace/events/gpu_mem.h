/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Device GPU memory tracepoints
 *
 * Copyright (C) 2020 Google, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu_mem

#if !defined(_TRACE_GPU_MEM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPU_MEM_H

#include <linux/tracepoint.h>

/*
 * The gpu_memory_total event indicates that there's an update to either the
 * global or process total gpu memory counters.
 *
 * This event should be emitted whenever the kernel device driver allocates,
 * frees, imports, unimports or uses/unuses memory in a way that changes the
 * total amount of memory accounted to a process or globally.
 *
 * @gpu_id: This is the id of the gpu for which the memory is accounted.
 * @pid: The pid of the process for which the memory is accounted, or 0 for global.
 * @size: The total size of memory accounted to the process or globally in bytes.
 */
TRACE_EVENT(gpu_mem_total,

	TP_PROTO(uint32_t gpu_id, uint32_t pid, uint64_t size),

	TP_ARGS(gpu_id, pid, size),

	TP_STRUCT__entry(
		__field(uint32_t, gpu_id)
		__field(uint32_t, pid)
		__field(uint64_t, size)
	),

	TP_fast_assign(
		__entry->gpu_id = gpu_id;
		__entry->pid = pid;
		__entry->size = size;
	),

	TP_printk("gpu_id=%u pid=%u size=%llu",
		__entry->gpu_id,
		__entry->pid,
		__entry->size)
);

#endif /* _TRACE_GPU_MEM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
