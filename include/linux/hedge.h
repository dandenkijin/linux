/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HEDGE_H
#define _LINUX_HEDGE_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/ptr_ring.h>

#define HEDGE_MAX_REPLICAS   4
#define HEDGE_POOL_SIZE      (1UL << 26)   /* 64MB default reservation */
#define HEDGE_ASSUMED_BIT    8
#define HEDGE_ASSUMED_OFFSET 256
#define HEDGE_BATCH_SIZE     64            /* Default max batch size */
#define HEDGE_TIMEOUT_NS     10000         /* Default timeout 10µs */

struct hedge_replica {
	void        *virt;
	phys_addr_t  phys;
	int          channel;   /* 0 or 1, verified at probe time */
};

struct hedge_alloc {
	struct hedge_replica replicas[HEDGE_MAX_REPLICAS];
	int          n_replicas;
	size_t       elem_size;
	size_t       logical_count;
	int          channel_bit;
	int          channel_offset;
	int          elem_size_shift;
};

/* Hedged read request structure */
struct hedge_read_req {
	struct hedge_alloc   *ha;            /* Associated allocation */
	size_t               idx;           /* Element index to read */
	void                *output;        /* Output buffer */
	struct completion    done;           /* Completion notification */
	atomic_t             completed;     /* Completion counter */
	u64                  timeout_ns;    /* Request timeout */
	int                  result;        /* Result code (0 = success) */
	/* For batch operations */
	struct hedge_read_req *batch_next;  /* Linked list for batch */
};

/* Worker thread state */
struct hedge_worker {
	struct task_struct *task;           /* Worker thread */
	int                cpu_id;         /* Pinned CPU */
	int                replica_idx;    /* Which replica this worker handles */
	struct ptr_ring    *work_queue;     /* Lockless work queue */
	atomic_t           active;         /* Worker active flag */
};

/* Global worker pool state */
struct hedge_worker_pool {
	struct hedge_worker workers[HEDGE_MAX_REPLICAS];
	int                n_workers;      /* Active worker count */
	bool               initialized;    /* Pool initialization state */
};

#ifdef CONFIG_HEDGE_POOL

/* Called by init/main.c before mm_init() */
void __init hedge_pool_reserve(void);

/* Called after boot to verify channel separation */
int __init hedge_pool_probe_init(void);

/* Standard API - preemptible versions */
int hedge_alloc_buf(struct hedge_alloc *ha, size_t elem_size,
		    int n_replicas);
void hedge_insert(struct hedge_alloc *ha, size_t idx, const void *val);
void hedge_free_buf(struct hedge_alloc *ha);

/* Atomic API - non-preemptible versions for real-time use */
void hedge_insert_atomic(struct hedge_alloc *ha, size_t idx, const void *val);

/* Hedged read API - the core Tailslayer functionality */
int hedge_read_element(struct hedge_alloc *ha, size_t idx, 
                       void *output, u64 timeout_ns);

int hedge_read_batch(struct hedge_alloc *ha, size_t start_idx, 
                     size_t count, void *output_buf, u64 timeout_ns);

/* Worker pool management */
int hedge_worker_pool_init(void);
void hedge_worker_pool_cleanup(void);

#ifdef CONFIG_PREEMPT_RT
/* RT-specific API - uses raw spinlocks for RT safety */
int hedge_alloc_buf_rt(struct hedge_alloc *ha, size_t elem_size,
		      int n_replicas);
void hedge_insert_rt(struct hedge_alloc *ha, size_t idx, const void *val);
#endif /* CONFIG_PREEMPT_RT */

#else

static inline void hedge_pool_reserve(void) {}
static inline int hedge_pool_probe_init(void) { return 0; }
static inline int hedge_alloc_buf(struct hedge_alloc *ha, size_t elem_size,
				  int n_replicas) { return -ENODEV; }
static inline void hedge_insert(struct hedge_alloc *ha, size_t idx,
				const void *val) { }
static inline void hedge_free_buf(struct hedge_alloc *ha) { }
static inline void hedge_insert_atomic(struct hedge_alloc *ha, size_t idx,
				       const void *val) { }
static inline int hedge_read_element(struct hedge_alloc *ha, size_t idx,
                                     void *output, u64 timeout_ns) { return -ENODEV; }
static inline int hedge_read_batch(struct hedge_alloc *ha, size_t start_idx,
                                   size_t count, void *output_buf, u64 timeout_ns) { return -ENODEV; }
static inline int hedge_worker_pool_init(void) { return -ENODEV; }
static inline void hedge_worker_pool_cleanup(void) { }
#ifdef CONFIG_PREEMPT_RT
static inline int hedge_alloc_buf_rt(struct hedge_alloc *ha, size_t elem_size,
				    int n_replicas) { return -ENODEV; }
static inline void hedge_insert_rt(struct hedge_alloc *ha, size_t idx,
				   const void *val) { }
#endif /* CONFIG_PREEMPT_RT */

#endif /* CONFIG_HEDGE_POOL */
#endif /* _LINUX_HEDGE_H */
