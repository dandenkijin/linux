# Linux Kernel DRAM Channel-Hedged Memory Pool

A kernel implementation of the Tailslayer concept for reducing tail latency in RAM reads caused by DRAM refresh stalls. This patches add a hedged memory allocator that replicates data across multiple, independent DRAM channels with uncorrelated refresh schedules.

## Overview

This kernel patch series implements a DRAM channel-hedged memory pool that:

- Reserves a 64MB physically contiguous region at boot via memblock
- Probes DRAM channel boundaries using percentile-based latency thresholds  
- Maps the region into kernel virtual space
- Exposes a hedged allocator for kernel-internal use

## Patches Applied

This repository implements three RFC-style patches:

1. **[RFC 1/3]** `mm: add CONFIG_HEDGE_POOL and public API`
   - Adds `CONFIG_HEDGE_POOL` kernel configuration option
   - Creates `include/linux/hedge.h` with public API
   - Hooks `hedge_pool.o` into the build system

2. **[RFC 2/3]** `mm: implement DRAM channel-hedged pool (hedge_pool)`
   - Full implementation in `mm/hedge_pool.c` (333 lines)
   - Channel detection using TSC-based latency measurements
   - Bump allocator with channel-aware address arithmetic

3. **[RFC 3/3]** `init/main: reserve hedged pool before mm_core_init`
   - Calls `hedge_pool_reserve()` before buddy allocator initialization
   - Ensures memblock allocation precedes page allocator

## Concept

Based on the [Tailslayer](https://github.com/LaurieWired/tailslayer) library, this implementation uses the same principle:

> **DRAM refresh stalls cause tail latency. By replicating data across channels with uncorrelated refresh schedules, we can service reads from whichever channel is available, reducing tail latency.**

### Channel Detection Algorithm

The kernel probes channel boundaries by:

1. **Timing reads** at different address offsets using `rdtsc`/`rdtscp`
2. **Computing 95th percentile** baseline latency 
3. **Setting spike threshold** at 1.33x baseline (`base_thresh * 4 / 3`)
4. **Detecting decorrelation** when one address shows >25% spikes and other shows <12.5% spikes
5. **Falling back** to hardcoded 800-cycle threshold if insufficient samples

### Address Arithmetic

Uses the same logical-to-physical mapping as Tailslayer:

```c
size_t elems_per_chunk = ha->channel_offset / ha->elem_size;
size_t chunk_mask      = elems_per_chunk - 1;  
size_t chunk_shift     = __builtin_ctzl(elems_per_chunk);
size_t stride          = (2 * ha->channel_offset) / ha->elem_size;
```

## Building

### Prerequisites

- Linux kernel source (tested with torvalds/master)
- GCC with support for `__attribute__((always_inline))`
- x86_64 or ARM64 architecture
- SMP support

### Configuration

```bash
# Enable the hedged pool
make menuconfig
# Navigate to: Device Drivers -> Memory Management options
# Enable "DRAM channel-hedged memory pool (EXPERIMENTAL)"

# Or directly:
echo "CONFIG_HEDGE_POOL=y" >> .config
make localmodconfig
```

### Build Commands

```bash
# Clean build
make clean
make -j$(nproc)

# Or incremental
make -j$(nproc)
```

## Usage

### Kernel API

```c
#include <linux/hedge.h>

// Allocate hedged buffer
struct hedge_alloc ha;
int ret = hedge_alloc_buf(&ha, sizeof(my_struct), 2);  // 2 replicas

// Insert data (replicates across channels)
my_struct data = {.field = value};
hedge_insert(&ha, 0, &data);

// Use hedged data (read from appropriate replica)
void *ptr = hedge_addr(&ha, 0, 0);  // logical index 0, replica 0

// Free allocation
hedge_free_buf(&ha);
```

### Integration Example

```c
static int my_driver_init(void)
{
    struct hedge_alloc buffer;
    int ret;
    
    ret = hedge_alloc_buf(&buffer, sizeof(struct my_item), 2);
    if (ret) {
        pr_err("Failed to allocate hedged buffer: %d\n", ret);
        return ret;
    }
    
    // Use buffer for latency-sensitive operations...
    
    return 0;
}
```

## Configuration Options

| Option | Default | Description |
|---------|----------|-------------|
| `HEDGE_POOL_SIZE` | `1UL << 26` (64MB) | Total pool reservation |
| `HEDGE_MAX_REPLICAS` | `4` | Maximum supported replicas |
| `HEDGE_ASSUMED_BIT` | `8` | Fallback channel bit |
| `HEDGE_ASSUMED_OFFSET` | `256` | Fallback channel offset |

## Debug Output

Enable with kernel log level:

```bash
dmesg | grep hedge_pool
```

Expected output:
```
hedge_pool: reserved 64MB at phys 0x...
hedge_pool: channel bit detected at 8 (offset 0x100)
hedge_pool: ready - channel_bit=8 offset=256 pool=64MB
```

## Performance Characteristics

- **Allocation**: O(1) bump allocator
- **Insert**: O(n) where n = number of replicas  
- **Access**: O(1) direct addressing
- **Memory overhead**: One replica per channel (2x minimum)
- **CPU overhead**: Minimal during normal operation

## Limitations

### Current Implementation (RFC)

- **No per-instance free path**: Uses bump allocator only
- **Static pool size**: Fixed 64MB at compile time
- **2-channel focus**: Designed for dual-channel systems
- **Experimental**: Not production-hardened

### Platform Dependencies

- **x86_64/ARM64 only**: Requires TSC or similar
- **SMP required**: Multi-core for channel probing
- **DRAM-specific**: May not work with all memory types

## Testing

### Unit Tests

```bash
# Build with debug config
make CONFIG_HEDGE_POOL=y CONFIG_DEBUG_KERNEL=y

# Check for compilation warnings
make W=1

# Test boot behavior
qemu-system-x86_64 -kernel arch/x86/boot/bzImage
```

### Benchmarks

Compare against standard allocation:

```c
// Standard allocation
ktime_t start = ktime_get();
// ... memory access ...
ktime_t end = ktime_get();
ktime_t std_latency = ktime_sub(end, start);

// Hedged allocation  
ktime_t start = ktime_get();
// ... hedged memory access ...
ktime_t end = ktime_get();
ktime_t hedge_latency = ktime_sub(end, start);
```

## Contributing

This is an RFC-style implementation. Areas for improvement:

1. **Dynamic pool sizing**: Runtime configuration
2. **Free implementation**: Per-instance deallocation  
3. **Multi-channel**: Support 3+ channel systems
4. **Production hardening**: Error handling, validation
5. **Architecture support**: RISC-V, PowerPC

## License

SPDX-License-Identifier: GPL-2.0

## References

- [Tailslayer Library](https://github.com/LaurieWired/tailslayer) - Original userspace implementation
- [DRAM Refresh](https://en.wikipedia.org/wiki/DRAM_refresh) - Background on refresh stalls
- [Linux Memory Management](https://www.kernel.org/doc/html/latest/mm/) - Kernel memory subsystem

## Maintainers

Dan Denkijin <dandenkijin@gmail.com>

---

**Note**: This is an EXPERIMENTAL implementation intended for testing and feedback. Not suitable for production use without additional hardening.
