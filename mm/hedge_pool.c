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
 */
static noinline u64 __init time_read_at_offset(void *base, size_t offset)
{
	volatile u8 *addr = (volatile u8 *)(base + offset);
	u64 t0, t1;

	/* Safety check - don't access NULL or invalid addresses */
	if (!base) {
		pr_warn("hedge_pool: time_read_at_offset called with NULL base\n");
		return 0;
	}

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

	return 0;
}
device_initcall(hedge_pool_probe_init);

/* ------------------------------------------------------------------ */
/* Runtime API                                                        */
/* ------------------------------------------------------------------ */

/*
 * Address arithmetic matching Tailslayer's get_next_logical_index_address.
 * Kept as a standalone inline so it can be called from hot paths.
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
	
	/* Use power-of-2 alignment to avoid division */
	size_t elems_per_chunk = 8;  /* Assume 8 elements per chunk for 256-byte alignment */
	size_t chunk_mask      = elems_per_chunk - 1;
	size_t chunk_shift     = 3;  /* log2(8) */
	size_t stride          = (ha->channel_offset << 1) >> ha->elem_size_shift;

	size_t chunk_idx       = logical_idx >> chunk_shift;
	size_t offset_in_chunk = logical_idx & chunk_mask;
	size_t element_offset  = (chunk_idx * stride) + offset_in_chunk;

	return ha->replicas[replica].virt + (element_offset * ha->elem_size);
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

	spin_lock_irqsave(&pool_lock, flags);

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

	for (i = 0; i < n_replicas; i++) {
		ha->replicas[i].virt  = pool_base_virt + pool_cursor
					+ (i * (size_t)discovered_channel_offset);
		ha->replicas[i].phys  = pool_base_phys + pool_cursor
					+ (i * (size_t)discovered_channel_offset);
		ha->replicas[i].channel =
			(ha->replicas[i].phys >> discovered_channel_bit) & 1;
	}

	pool_cursor += needed;

	spin_unlock_irqrestore(&pool_lock, flags);

	/* Verify replicas actually landed on different channels */
	for (i = 1; i < n_replicas; i++) {
		if (ha->replicas[i].channel == ha->replicas[0].channel) {
			pr_warn("hedge_pool: replica %d on same channel as replica 0\n", i);
		}
	}

	return 0;
}

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

void hedge_free_buf(struct hedge_alloc *ha)
{
	/* RFC implementation: bump allocator, no per-instance free path */
	memset(ha, 0, sizeof(*ha));
}
