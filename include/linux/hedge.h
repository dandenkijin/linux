/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HEDGE_H
#define _LINUX_HEDGE_H

#include <linux/types.h>
#include <linux/spinlock.h>

#define HEDGE_MAX_REPLICAS   4
#define HEDGE_POOL_SIZE      (1UL << 26)   /* 64MB default reservation */
#define HEDGE_ASSUMED_BIT    8
#define HEDGE_ASSUMED_OFFSET 256

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

#ifdef CONFIG_HEDGE_POOL

/* Called by init/main.c before mm_init() */
void __init hedge_pool_reserve(void);

/* Called after boot to verify channel separation */
int __init hedge_pool_probe_init(void);

/* Runtime API */
int hedge_alloc_buf(struct hedge_alloc *ha, size_t elem_size,
		    int n_replicas);
void hedge_insert(struct hedge_alloc *ha, size_t idx, const void *val);
void hedge_free_buf(struct hedge_alloc *ha);

#else

static inline void hedge_pool_reserve(void) {}
static inline int hedge_pool_probe_init(void) { return 0; }
static inline int hedge_alloc_buf(struct hedge_alloc *ha, size_t elem_size,
				  int n_replicas) { return -ENODEV; }
static inline void hedge_insert(struct hedge_alloc *ha, size_t idx,
				const void *val) { }
static inline void hedge_free_buf(struct hedge_alloc *ha) { }

#endif /* CONFIG_HEDGE_POOL */
#endif /* _LINUX_HEDGE_H */
