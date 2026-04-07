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
#include <linux/mm.h>
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

	/* Flush from all cache levels */
	asm volatile("clflush (%0)" :: "r"(addr) : "memory");
	asm volatile("mfence" ::: "memory");

	asm volatile("lfence\n\t rdtsc"
		     : "=A"(t0) :: "memory");

	(void)*addr;  /* the read */

	asm volatile("rdtscp\n\t lfence"
		     : "=A"(t1) :: "memory", "%rcx");

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

	int idx = (n * 95) / 100;
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
	u64 latencies[PROBE_SAMPLES * 4];   /* room for a/b samples at multiple bits */
	int n_lat = 0;

	for (bit = 6; bit <= 13; bit++) {
		size_t offset_a = 0;
		size_t offset_b = (1UL << bit);
		u64 spikes_a = 0, spikes_b = 0;
		int i;

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
			u64 spike_thresh = base_thresh * 4 / 3;   /* 1.33x margin */

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
		if (spikes_a > (PROBE_SAMPLES / 4) &&
		    spikes_b < (PROBE_SAMPLES / 8)) {
			pr_info("hedge_pool: channel bit detected at %d "
				"(offset 0x%lx)\n", bit, offset_b);
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

	/* Map the reserved region into kernel virtual space */
	pool_base_virt = memremap(pool_base_phys, HEDGE_POOL_SIZE,
				  MEMREMAP_WB);
	if (!pool_base_virt) {
		pr_err("hedge_pool: memremap failed - releasing reservation\n");
		memblock_free(pool_base_phys, HEDGE_POOL_SIZE);
		pool_base_phys = 0;
		return -ENOMEM;
	}

	/* Warm the region so we're measuring DRAM, not page faults */
	memset(pool_base_virt, 0, HEDGE_POOL_SIZE);

	discovered_channel_bit    = probe_channel_bit(pool_base_virt);
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
	size_t elems_per_chunk = ha->channel_offset / ha->elem_size;
	size_t chunk_mask      = elems_per_chunk - 1;
	size_t chunk_shift     = __builtin_ctzl(elems_per_chunk);
	size_t stride          = (2 * ha->channel_offset) / ha->elem_size;

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

	if (!pool_valid)
		return -ENODEV;

	if (n_replicas < 2 || n_replicas > HEDGE_MAX_REPLICAS)
		return -EINVAL;

	/* Each replica gets its own channel-offset-aligned slice */
	needed = n_replicas * discovered_channel_offset;

	spin_lock_irqsave(&pool_lock, flags);

	if (pool_cursor + needed > HEDGE_POOL_SIZE) {
		spin_unlock_irqrestore(&pool_lock, flags);
		return -ENOMEM;
	}

	ha->n_replicas      = n_replicas;
	ha->elem_size       = elem_size;
	ha->logical_count   = 0;
	ha->channel_bit     = discovered_channel_bit;
	ha->channel_offset  = discovered_channel_offset;

	for (i = 0; i < n_replicas; i++) {
		ha->replicas[i].virt  = pool_base_virt + pool_cursor
					+ (i * discovered_channel_offset);
		ha->replicas[i].phys  = pool_base_phys + pool_cursor
					+ (i * discovered_channel_offset);
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

	if (!pool_valid || idx >= ha->logical_count)
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
