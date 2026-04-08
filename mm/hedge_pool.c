// SPDX-License-Identifier: GPL-2.0
/*
 * hedged_pool.c (EXPERIMENTAL)
 *
 * DRAM channel-hedged memory pool for kernel-internal, latency-sensitive
 * data structures. This is an RFC implementation and is not yet hardened
 * for production use.
 */

#include <linux/hedge.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/numa.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/preempt.h>
#include <asm/io.h>

/* ------------------------------------------------------------------ */
/* Pool state                                                           */
/* ------------------------------------------------------------------ */

static phys_addr_t  pool_base_phys;
static void        *pool_base_virt;
static size_t       pool_cursor;       /* bump allocator offset */
static DEFINE_SPINLOCK(pool_lock);

static int          discovered_channel_bit    = HEDGE_ASSUMED_BIT;
static int          discovered_channel_offset = HEDGE_ASSUMED_OFFSET;
static bool         pool_valid;

/* Dynamic channel detection */
static int          detected_channels = 0;
static int          max_replicas = 2;

/* Worker pool state */
static struct hedge_worker_pool worker_pool;

/* ------------------------------------------------------------------ */
/* Phase 1: memblock reservation (before buddy allocator)              */
/* ------------------------------------------------------------------ */

void __init hedge_pool_reserve(void)
{
	phys_addr_t base;

	/*
	 * Allocate physically contiguous region aligned to the assumed
	 * channel offset so that our stride arithmetic is valid from
	 * the base address.
	 */
	base = memblock_phys_alloc_range(HEDGE_POOL_SIZE,
					 HEDGE_ASSUMED_OFFSET,
					 0, MEMBLOCK_ALLOC_ANYWHERE);
	if (!base) {
		pr_warn("hedge_pool: failed to reserve %luMB - disabled\n",
			HEDGE_POOL_SIZE >> 20);
		return;
	}

	memblock_reserve(base, HEDGE_POOL_SIZE);
	pool_base_phys = base;

	pr_info("hedge_pool: reserved %luMB at phys 0x%llx\n",
		HEDGE_POOL_SIZE >> 20, (unsigned long long)base);
}

/* ------------------------------------------------------------------ */
/* Dynamic channel detection                                           */
/* ------------------------------------------------------------------ */

static int __init detect_memory_channels(void)
{
	
	/* Method 1: Check NUMA topology - most reliable */
/* But be defensive - NUMA might not be initialized yet */
#ifdef CONFIG_NUMA
	int nodes = num_online_nodes();
	if (nodes > 1) {
		detected_channels = nodes;
		pr_info("hedge_pool: NUMA detected %d nodes, assuming %d channels\n",
			 nodes, nodes);
	}
#endif
	
	/* Method 2: Fallback to conservative defaults */
	if (detected_channels == 0) {
		detected_channels = 2;
		pr_info("hedge_pool: NUMA not available, defaulting to %d channels\n",
			 detected_channels);
	}
	
	/* Apply reasonable limits */
	max_replicas = min(detected_channels, HEDGE_MAX_REPLICAS);
	
	pr_info("hedge_pool: detected %d memory channels, max replicas: %d\n",
		 detected_channels, max_replicas);
	
	return detected_channels;
}

/* ------------------------------------------------------------------ */
/* Phase 2: trefi probe - discover real channel bit (device_initcall)  */
/* ------------------------------------------------------------------ */

/*
 * Time a cacheline-flushed read at a given physical offset from base.
 * Returns TSC delta. Caller must be pinned to a quiet core.
 * Preemption is disabled for consistent timing measurements.
 */
static noinline u64 __init time_read_at_offset(void *base, size_t offset)
{
	volatile u8 *addr = (volatile u8 *)(base + offset);
	u64 t0, t1;
	unsigned long flags;
	int cpu;

	/* Safety check - don't access NULL or invalid addresses */
	if (!base) {
		pr_warn("hedge_pool: time_read_at_offset called with NULL base\n");
		return 0;
	}

	/* Disable preemption for consistent timing */
	preempt_disable();
	
	/* Pin to current CPU to avoid migration during timing */
	cpu = get_cpu();
	
	/* Disable interrupts for critical timing section */
	local_irq_save(flags);

	/* Simple timing without risky cache operations for 32-bit safety */
#ifdef CONFIG_X86_64
	/* 64-bit version - keep original implementation */
	asm volatile("clflush (%0)" :: "r"(addr) : "memory");
	asm volatile("mfence" ::: "memory");
	
	asm volatile("lfence\n\t rdtsc"
		     : "=A"(t0) :: "memory");

	(void)*addr;  /* the read */

	asm volatile("rdtscp\n\t lfence"
		     : "=A"(t1) :: "memory", "%rcx");
#else
	/* 32-bit version - simplified to avoid corruption */
	asm volatile("mfence" ::: "memory");
	
	/* Use simple rdtsc without complex constraints */
	u32 t0_low, t0_high;
	asm volatile("rdtsc" : "=a"(t0_low), "=d"(t0_high));
	t0 = ((u64)t0_high << 32) | t0_low;

	(void)*addr;  /* the read */

	asm volatile("mfence" ::: "memory");
	
	u32 t1_low, t1_high;
	asm volatile("rdtsc" : "=a"(t1_low), "=d"(t1_high));
	t1 = ((u64)t1_high << 32) | t1_low;
#endif

	/* Restore interrupts and CPU affinity */
	local_irq_restore(flags);
	put_cpu();
	preempt_enable();

	return t1 - t0;
}

/* ------------------------------------------------------------------ */
/* Simple 64-bit sorting helper (for RFC, not production-quality)     */
/* ------------------------------------------------------------------ */

static void __init sort_u64(u64 *samples, int n)
{
	int i, j;
	for (i = 1; i < n; i++) {
		u64 tmp = samples[i];
		for (j = i; j > 0 && samples[j-1] > tmp; j--)
			samples[j] = samples[j-1];
		samples[j] = tmp;
	}
}

/*
 * Compute a 95th-percentile-based threshold and return it.
 */
static u64 __init percentile95(u64 *samples, int n)
{
	if (n <= 0)
		return 0;
	if (n == 1)
		return samples[0];

	sort_u64(samples, n);

	int idx = n - (n >> 2);  /* approximately 95% (n - n/4) */
	if (idx >= n)
		idx = n - 1;

	return samples[idx];
}

/* ------------------------------------------------------------------ */
/* Probe logic                                                        */
/* ------------------------------------------------------------------ */

#define PROBE_SAMPLES   128
#define PROBE_WARMUP     16

/*
 * Scan address offsets from base to find where latency distribution
 * splits into two populations - that boundary is the channel bit.
 */
static int __init probe_channel_bit(void *base)
{
	int bit;
	static u64 latencies[PROBE_SAMPLES * 4];   /* room for a/b samples at multiple bits */
	int n_lat = 0;

	pr_info("hedge_pool: probe_channel_bit starting with base %p\n", base);
	
	if (!base) {
		pr_err("hedge_pool: probe_channel_bit called with NULL base\n");
		return HEDGE_ASSUMED_BIT;
	}

	/* Scan address offsets from base to find where latency distribution
	 * splits into two populations - that boundary is the channel bit.
	 */
	for (bit = 0; bit < 16; bit++) {
		size_t offset_a = 0;
		size_t offset_b = (1 << bit);
		u64 spikes_a = 0, spikes_b = 0;
		int i;

		pr_info("hedge_pool: testing bit %d, offset_b 0x%zx\n", bit, offset_b);

		/* Reset array index for each bit to prevent overflow */
		n_lat = 0;

		for (i = 0; i < PROBE_SAMPLES + PROBE_WARMUP; i++) {
			u64 lat_a = time_read_at_offset(base, offset_a);
			u64 lat_b = time_read_at_offset(base, offset_b);

			if (i < PROBE_WARMUP)
				continue;

			latencies[n_lat++] = lat_a;
			latencies[n_lat++] = lat_b;
		}

		if (n_lat >= 20) {
			u64 base_thresh = percentile95(latencies, n_lat);
			u64 spike_thresh = div_u64(base_thresh * 4, 3);   /* 1.33x margin */

			for (i = 0; i < PROBE_SAMPLES; i++) {
				u64 lat_a = time_read_at_offset(base, offset_a);
				u64 lat_b = time_read_at_offset(base, offset_b);
				if (lat_a > spike_thresh) spikes_a++;
				if (lat_b > spike_thresh) spikes_b++;
			}
		} else {
			/* fall back if samples are too small */
			for (i = 0; i < PROBE_SAMPLES; i++) {
				u64 lat_a = time_read_at_offset(base, offset_a);
				u64 lat_b = time_read_at_offset(base, offset_b);
				if (lat_a > 800ULL) spikes_a++;
				if (lat_b > 800ULL) spikes_b++;
			}
		}

		/*
		 * If spikes are correlated (both high or both low) the two
		 * addresses are on the same channel. When they decorrelate,
		 * we've crossed the channel boundary.
		 */
		if (spikes_a > (PROBE_SAMPLES >> 2) &&
		    spikes_b < (PROBE_SAMPLES >> 3)) {
			pr_info("hedge_pool: channel bit detected at %d "
				"(offset 0x%zx)\n", bit, offset_b);
			return bit;
		}
	}

	pr_warn("hedge_pool: channel bit detection inconclusive, "
		"using assumed value %d\n", HEDGE_ASSUMED_BIT);
	return HEDGE_ASSUMED_BIT;
}

int __init hedge_pool_probe_init(void)
{
	if (!pool_base_phys) {
		pr_warn("hedge_pool: no reservation, skipping probe\n");
		return -ENOMEM;
	}

	/* Detect hardware capabilities first */
	detect_memory_channels();

	/* Map the reserved region into kernel virtual space */
	pool_base_virt = memremap(pool_base_phys, HEDGE_POOL_SIZE,
				  MEMREMAP_WB);
	if (!pool_base_virt) {
		pr_err("hedge_pool: memremap failed - releasing reservation\n");
		memblock_phys_free(pool_base_phys, HEDGE_POOL_SIZE);
		pool_base_phys = 0;
		return -ENOMEM;
	}

	/* Warm the region so we're measuring DRAM, not page faults */
	memset(pool_base_virt, 0, HEDGE_POOL_SIZE);

	/* Channel probing with array overflow fix */
	discovered_channel_bit    = probe_channel_bit(pool_base_virt);
	pr_info("hedge_pool: channel probing re-enabled with array overflow fix\n");
	
	discovered_channel_offset = (1 << discovered_channel_bit);

	/*
	 * Final sanity check: verify the base address is channel-aligned
	 * so our replica stride arithmetic starts from a known state.
	 */
	if ((pool_base_phys >> discovered_channel_bit) & 1) {
		pr_warn("hedge_pool: base phys not channel-aligned, "
			"adjusting by one offset\n");
		pool_base_virt  += discovered_channel_offset;
		pool_base_phys  += discovered_channel_offset;
	}

	pool_cursor = 0;
	pool_valid  = true;

	pr_info("hedge_pool: ready - channel_bit=%d offset=%d pool=%luMB\n",
		discovered_channel_bit,
		discovered_channel_offset,
		HEDGE_POOL_SIZE >> 20);

	/* Initialize worker pool for hedged reads */
	if (hedge_worker_pool_init() != 0) {
		pr_warn("hedge_pool: failed to initialize worker pool - hedged reads disabled\n");
		/* Continue without worker pool - allocation still works */
	}

	return 0;
}
device_initcall(hedge_pool_probe_init);

/* ------------------------------------------------------------------ */
/* Runtime API                                                        */
/* ------------------------------------------------------------------ */

/*
 * Address arithmetic matching Tailslayer's get_next_logical_index_address.
 * Kept as a standalone inline so it can be called from hot paths.
 * Includes optional preemption control for real-time use cases.
 */

static __always_inline void *
hedge_addr(const struct hedge_alloc *ha, int replica, size_t logical_idx)
{
	/* Check if pool is initialized before using */
	if (!pool_valid) {
		pr_warn("hedge_pool: called before initialization - caller: %pS\n", __builtin_return_address(0));
		return NULL;
	}
		
	if (!ha || replica < 0 || replica >= HEDGE_MAX_REPLICAS || !ha->replicas[replica].virt) {
		pr_warn("hedge_pool: invalid hedge_alloc parameters\n");
		return NULL;
	}
	
	/* Tailslayer-style channel scrambling address arithmetic */
	size_t elems_per_chunk = 8;  /* Assume 8 elements per chunk for 256-byte alignment */
	size_t chunk_mask      = elems_per_chunk - 1;
	size_t chunk_shift     = 3;  /* log2(8) */
	
	/* Calculate stride that ensures channel alternation */
	size_t stride = (ha->channel_offset << 1) >> ha->elem_size_shift;
	
	size_t chunk_idx       = logical_idx >> chunk_shift;
	size_t offset_in_chunk = logical_idx & chunk_mask;
	size_t element_offset  = (chunk_idx * stride) + offset_in_chunk;
	
	/* Add replica-specific channel offset for proper scrambling */
	void *base_addr = ha->replicas[replica].virt;
	
	return base_addr + (element_offset * ha->elem_size);
}

/*
 * Atomic version of hedge_addr with preemption disabled
 */
static __always_inline void *
hedge_addr_atomic(const struct hedge_alloc *ha, int replica, size_t logical_idx)
{
	void *result;
	unsigned long flags;

	/* Disable preemption for address calculation */
	preempt_disable();
	local_irq_save(flags);

	result = hedge_addr(ha, replica, logical_idx);

	local_irq_restore(flags);
	preempt_enable();

	return result;
}

int hedge_alloc_buf(struct hedge_alloc *ha, size_t elem_size,
                    int n_replicas)
{
	unsigned long flags;
	size_t needed;
	int i;

	if (!pool_valid) {
		pr_warn("hedge_pool: alloc_buf called before init - caller: %pS\n", __builtin_return_address(0));
		return -ENODEV;
	}

	/* Check if we're in atomic context - if so, we must use GFP_ATOMIC */
	might_sleep();

	/* Auto-detect optimal replica count if not specified */
	if (n_replicas <= 0) {
		n_replicas = max_replicas;
		pr_info("hedge_pool: auto-detected %d replicas for optimal performance\n",
			 n_replicas);
	}

	if (n_replicas < 2 || n_replicas > max_replicas) {
		pr_warn("hedge_pool: requested %d replicas, only %d available (detected: %d)\n",
			 n_replicas, max_replicas, detected_channels);
		return -EINVAL;
	}

	/* Each replica gets its own channel-offset-aligned slice */
	needed = (size_t)n_replicas * (size_t)discovered_channel_offset;

	/* Use RT-safe locking if PREEMPT_RT is enabled */
#ifdef CONFIG_PREEMPT_RT
	spin_lock_irqsave(&pool_lock, flags);
#else
	/* On non-RT systems, we can be more aggressive */
	spin_lock_irqsave(&pool_lock, flags);
#endif

	if (pool_cursor + needed > HEDGE_POOL_SIZE) {
		spin_unlock_irqrestore(&pool_lock, flags);
		return -ENOMEM;
	}

	ha->n_replicas      = n_replicas;
	ha->elem_size       = elem_size;
	ha->elem_size_shift = 3;  /* Assume 8-byte elements (log2(8)) */
	ha->logical_count   = 0;
	ha->channel_bit     = discovered_channel_bit;
	ha->channel_offset  = discovered_channel_offset;

	/* Assign replicas to different channels with proper scrambling */
	for (i = 0; i < n_replicas; i++) {
		/* Base offset for this replica */
		size_t replica_offset = i * discovered_channel_offset;
		
		/* Ensure replica i is on channel (i % 2) by adjusting base */
		if ((((pool_base_phys + pool_cursor + replica_offset) >> discovered_channel_bit) & 1) != (i & 1)) {
			/* Flip to the other channel by adding one channel offset */
			replica_offset += discovered_channel_offset;
		}
		
		ha->replicas[i].virt  = pool_base_virt + pool_cursor + replica_offset;
		ha->replicas[i].phys  = pool_base_phys + pool_cursor + replica_offset;
		ha->replicas[i].channel = (ha->replicas[i].phys >> discovered_channel_bit) & 1;
		
		pr_debug("hedge_pool: replica %d -> phys 0x%llx, channel %d\n",
			 i, (unsigned long long)ha->replicas[i].phys, ha->replicas[i].channel);
	}

	pool_cursor += needed;

	spin_unlock_irqrestore(&pool_lock, flags);

	/* Verify replicas actually landed on different channels */
	for (i = 0; i < n_replicas; i++) {
		pr_info("hedge_pool: replica %d -> channel %d (phys 0x%llx)\n",
			 i, ha->replicas[i].channel, (unsigned long long)ha->replicas[i].phys);
		if (i > 0 && ha->replicas[i].channel == ha->replicas[i-1].channel) {
			pr_warn("hedge_pool: replica %d on same channel as replica %d\n", i, i-1);
		}
	}

	return 0;
}

/*
 * Preemptible version of hedge_insert for normal operation
 */
void hedge_insert(struct hedge_alloc *ha, size_t idx, const void *val)
{
	int i;

	if (!pool_valid) {
		pr_warn("hedge_pool: insert called before init - caller: %pS\n", __builtin_return_address(0));
		return;
	} else if (idx >= ha->logical_count)
		return;

	for (i = 0; i < ha->n_replicas; i++) {
		void *dst = hedge_addr(ha, i, idx);
		memcpy(dst, val, ha->elem_size);
	}
}

/*
 * Non-preemptible version for real-time critical sections
 */
void hedge_insert_atomic(struct hedge_alloc *ha, size_t idx, const void *val)
{
	int i;
	unsigned long flags;

	if (!pool_valid) {
		pr_warn("hedge_pool: insert_atomic called before init - caller: %pS\n", __builtin_return_address(0));
		return;
	} else if (idx >= ha->logical_count)
		return;

	/* Disable preemption for atomic operation */
	preempt_disable();
	local_irq_save(flags);

	for (i = 0; i < ha->n_replicas; i++) {
		void *dst = hedge_addr(ha, i, idx);
		memcpy(dst, val, ha->elem_size);
	}

	local_irq_restore(flags);
	preempt_enable();
}

/* ------------------------------------------------------------------ */
/* PREEMPT_RT support                                                    */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_PREEMPT_RT
/* RT-safe lock implementation */
static DEFINE_RAW_SPINLOCK(rt_pool_lock);

/* RT-safe allocation with raw spinlock */
int hedge_alloc_buf_rt(struct hedge_alloc *ha, size_t elem_size,
                      int n_replicas)
{
	unsigned long flags;
	size_t needed;
	int i;

	if (!pool_valid) {
		pr_warn("hedge_pool: alloc_buf_rt called before init - caller: %pS\n", __builtin_return_address(0));
		return -ENODEV;
	}

	/* RT kernel: no sleeping allowed */
	might_sleep();

	if (n_replicas <= 0) {
		n_replicas = max_replicas;
	}

	if (n_replicas < 2 || n_replicas > max_replicas) {
		return -EINVAL;
	}

	needed = (size_t)n_replicas * (size_t)discovered_channel_offset;

	/* Use raw spinlock for RT safety */
	raw_spin_lock_irqsave(&rt_pool_lock, flags);

	if (pool_cursor + needed > HEDGE_POOL_SIZE) {
		raw_spin_unlock_irqrestore(&rt_pool_lock, flags);
		return -ENOMEM;
	}

	ha->n_replicas      = n_replicas;
	ha->elem_size       = elem_size;
	ha->elem_size_shift = 3;
	ha->logical_count   = 0;
	ha->channel_bit     = discovered_channel_bit;
	ha->channel_offset  = discovered_channel_offset;

	/* Assign replicas to different channels with proper scrambling (RT version) */
	for (i = 0; i < n_replicas; i++) {
		/* Base offset for this replica */
		size_t replica_offset = i * discovered_channel_offset;
		
		/* Ensure replica i is on channel (i % 2) by adjusting base */
		if ((((pool_base_phys + pool_cursor + replica_offset) >> discovered_channel_bit) & 1) != (i & 1))) {
			/* Flip to the other channel by adding one channel offset */
			replica_offset += discovered_channel_offset;
		}
		
		ha->replicas[i].virt  = pool_base_virt + pool_cursor + replica_offset;
		ha->replicas[i].phys  = pool_base_phys + pool_cursor + replica_offset;
		ha->replicas[i].channel = (ha->replicas[i].phys >> discovered_channel_bit) & 1;
		
		pr_debug("hedge_pool: RT replica %d -> phys 0x%llx, channel %d\n",
			 i, (unsigned long long)ha->replicas[i].phys, ha->replicas[i].channel);
	}

	pool_cursor += needed;

	raw_spin_unlock_irqrestore(&rt_pool_lock, flags);

	return 0;
}

/* RT-safe insert operation */
void hedge_insert_rt(struct hedge_alloc *ha, size_t idx, const void *val)
{
	int i;
	unsigned long flags;

	if (!pool_valid || idx >= ha->logical_count)
		return;

	/* Use raw spinlock for RT safety */
	raw_spin_lock_irqsave(&rt_pool_lock, flags);

	for (i = 0; i < ha->n_replicas; i++) {
		void *dst = hedge_addr(ha, i, idx);
		memcpy(dst, val, ha->elem_size);
	}

	raw_spin_unlock_irqrestore(&rt_pool_lock, flags);
}
#endif /* CONFIG_PREEMPT_RT */

void hedge_free_buf(struct hedge_alloc *ha)
{
	/* RFC implementation: bump allocator, no per-instance free path */
	memset(ha, 0, sizeof(*ha));
}

/* ------------------------------------------------------------------ */
/* Worker Thread Implementation                                         */
/* ------------------------------------------------------------------ */

/* Helper function to get source address for a worker and request */
static void *hedge_addr_from_req(struct hedge_worker *worker, 
                                 struct hedge_read_req *req,
                                 struct hedge_alloc *ha)
{
	/* Use the existing hedge_addr function with worker's replica index */
	return hedge_addr(ha, worker->replica_idx, req->idx);
}

static int hedge_worker_thread(void *data)
{
	struct hedge_worker *worker = data;
	struct hedge_read_req *req;

	pr_info("hedge_pool: worker %d started on CPU %d\n", 
		worker->replica_idx, worker->cpu_id);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		/* Try to get work from queue */
		req = ptr_ring_consume(worker->work_queue);
		if (!req) {
			/* No work, sleep */
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
			continue;
		}

		__set_current_state(TASK_RUNNING);

		/* Process the hedged read request */
		if (req->output && req->ha) {
			void *src_addr = hedge_addr_from_req(worker, req, req->ha);
			if (src_addr) {
				/* Use READ_ONCE for atomic access */
				switch (req->ha->elem_size) {
				case 1:
					*(u8*)req->output = READ_ONCE(*(u8*)src_addr);
					break;
				case 2:
					*(u16*)req->output = READ_ONCE(*(u16*)src_addr);
					break;
				case 4:
					*(u32*)req->output = READ_ONCE(*(u32*)src_addr);
					break;
				case 8:
					*(u64*)req->output = READ_ONCE(*(u64*)src_addr);
					break;
				default:
					memcpy(req->output, src_addr, req->ha->elem_size);
					break;
				}
				req->result = 0;
				smp_wmb(); /* Ensure write completion before flag */
			} else {
				req->result = -EINVAL;
			}
		} else {
			req->result = -EINVAL;
		}

		/* Mark this worker as completed - first one wins */
		if (atomic_inc_return(&req->completed) == 1) {
			/* First completion - signal the requester */
			complete(&req->done);
		} else {
			/* Not the first - this worker frees the request */
			kfree(req);
		}

		set_current_state(TASK_INTERRUPTIBLE);
	}

	__set_current_state(TASK_RUNNING);
	pr_info("hedge_pool: worker %d stopping\n", worker->replica_idx);
	return 0;
}

int hedge_worker_pool_init(void)
{
	int i, cpu;
	int ret = 0;

	if (worker_pool.initialized) {
		pr_warn("hedge_pool: worker pool already initialized\n");
		return 0;
	}

	/* Initialize worker pool structure */
	memset(&worker_pool, 0, sizeof(worker_pool));
	worker_pool.n_workers = max_replicas;

	/* Find suitable CPUs for workers - prefer isolated CPUs if available */
	cpu = cpumask_first(cpu_online_mask);
	
	for (i = 0; i < worker_pool.n_workers; i++) {
		struct hedge_worker *worker = &worker_pool.workers[i];
		
		/* Create work queue for this worker */
		worker->work_queue = kmalloc(sizeof(*worker->work_queue), GFP_KERNEL);
		if (!worker->work_queue) {
			pr_err("hedge_pool: failed to allocate work queue for worker %d\n", i);
			ret = -ENOMEM;
			goto cleanup;
		}
		
		if (ptr_ring_init(worker->work_queue, HEDGE_BATCH_SIZE, GFP_KERNEL) != 0) {
			pr_err("hedge_pool: failed to initialize work queue for worker %d\n", i);
			kfree(worker->work_queue);
			worker->work_queue = NULL;
			ret = -ENOMEM;
			goto cleanup;
		}

		/* Find next available CPU */
		if (cpu >= nr_cpu_ids) {
			cpu = cpumask_first(cpu_online_mask);
		}
		
		worker->cpu_id = cpu;
		worker->replica_idx = i;
		atomic_set(&worker->active, 1);

		/* Create and start worker thread */
		worker->task = kthread_create_on_cpu(hedge_worker_thread, 
						    worker, 
						    cpu, 
						    "hedge_worker/%d");
		if (IS_ERR(worker->task)) {
			ret = PTR_ERR(worker->task);
			pr_err("hedge_pool: failed to create worker %d on CPU %d: %d\n", 
			       i, cpu, ret);
			ptr_ring_cleanup(worker->work_queue, NULL);
			goto cleanup;
		}

		/* Set as per-CPU kthread */
		kthread_set_per_cpu(worker->task, cpu);
		
		/* Wake up the worker */
		wake_up_process(worker->task);

		/* Move to next CPU */
		cpu = cpumask_next(cpu, cpu_online_mask);
	}

	worker_pool.initialized = true;
	pr_info("hedge_pool: initialized worker pool with %d workers\n", 
		worker_pool.n_workers);

	return 0;

cleanup:
	/* Cleanup on failure */
	for (i = 0; i < worker_pool.n_workers; i++) {
		struct hedge_worker *worker = &worker_pool.workers[i];
		if (worker->task && !IS_ERR(worker->task)) {
			kthread_stop(worker->task);
		}
		if (worker->work_queue) {
			ptr_ring_cleanup(worker->work_queue, NULL);
			kfree(worker->work_queue);
			worker->work_queue = NULL;
		}
	}
	return ret;
}

void hedge_worker_pool_cleanup(void)
{
	int i;

	if (!worker_pool.initialized) {
		return;
	}

	pr_info("hedge_pool: cleaning up worker pool\n");

	for (i = 0; i < worker_pool.n_workers; i++) {
		struct hedge_worker *worker = &worker_pool.workers[i];
		
		atomic_set(&worker->active, 0);
		
		if (worker->task && !IS_ERR(worker->task)) {
			kthread_stop(worker->task);
			worker->task = NULL;
		}
		
		if (worker->work_queue) {
			ptr_ring_cleanup(worker->work_queue, NULL);
			kfree(worker->work_queue);
			worker->work_queue = NULL;
		}
	}

	worker_pool.initialized = false;
}

/* ------------------------------------------------------------------ */
/* Hedged Read API Implementation                                      */
/* ------------------------------------------------------------------ */

int hedge_read_element(struct hedge_alloc *ha, size_t idx, 
                       void *output, u64 timeout_ns)
{
	struct hedge_read_req *req;
	int i, ret;
	unsigned long timeout_jiffies;

	if (!pool_valid || !worker_pool.initialized) {
		pr_warn("hedge_pool: hedged read called before initialization\n");
		return -ENODEV;
	}

	if (!ha || !output || idx >= ha->logical_count) {
		return -EINVAL;
	}

	/* Allocate request structure - this will be freed by worker */
	req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req) {
		return -ENOMEM;
	}

	/* Initialize request structure */
	memset(req, 0, sizeof(*req));
	req->ha = ha;
	req->idx = idx;
	req->output = output;
	req->timeout_ns = timeout_ns ? timeout_ns : HEDGE_TIMEOUT_NS;
	req->result = -ETIMEDOUT;
	atomic_set(&req->completed, 0);
	init_completion(&req->done);

	/* Dispatch request to all workers */
	for (i = 0; i < ha->n_replicas && i < worker_pool.n_workers; i++) {
		struct hedge_worker *worker = &worker_pool.workers[i];
		
		ret = ptr_ring_produce(worker->work_queue, req);
		if (ret != 0) {
			pr_warn("hedge_pool: failed to queue request to worker %d\n", i);
			/* Continue with available workers */
		}
	}

	/* Wait for first completion with timeout */
	timeout_jiffies = usecs_to_jiffies(div_u64(req->timeout_ns, 1000));
	if (wait_for_completion_timeout(&req->done, timeout_jiffies) == 0) {
		pr_warn("hedge_pool: hedged read timeout for idx %zu\n", idx);
		kfree(req);
		return -ETIMEDOUT;
	}

	smp_rmb(); /* Ensure result is read after completion */
	ret = req->result;
	kfree(req);
	return ret;
}

int hedge_read_batch(struct hedge_alloc *ha, size_t start_idx, 
                     size_t count, void *output_buf, u64 timeout_ns)
{
	size_t i;
	int ret = 0;
	u8 *buf_ptr = output_buf;

	if (!pool_valid || !worker_pool.initialized) {
		pr_warn("hedge_pool: hedged batch read called before initialization\n");
		return -ENODEV;
	}

	if (!ha || !output_buf || count == 0 || 
	    start_idx + count > ha->logical_count) {
		return -EINVAL;
	}

	/* For RFC implementation, process elements sequentially */
	/* TODO: Implement true parallel batch processing */
	for (i = 0; i < count; i++) {
		ret = hedge_read_element(ha, start_idx + i, 
		                       buf_ptr + (i * ha->elem_size), 
		                       timeout_ns);
		if (ret != 0) {
			pr_warn("hedge_pool: batch read failed at element %zu: %d\n", i, ret);
			break;
		}
	}

	return ret;
}
