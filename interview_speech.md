# CS 519 Interview Prep: All Projects Explained

---

## Project Overview

| Project | Topic | Key Skills |
|---------|-------|------------|
| **Locks Bench** | Lock implementations + perf profiling | Spinlocks, mutexes, semaphores, `perf record`, `perf report`, `cycles:u`/`cycles:k` |
| **Project 1** | Custom Linux syscall (`app_helper`) | Kernel hacking, `copy_from_user`/`copy_to_user`, `kmalloc`, `clock_gettime` benchmarking |
| **Project 1 Part 2** | Parallel matrix multiply via IPC | Shared memory (`shmget`/`shmat`), pipes, cache tiling, `perf stat`, `perf report` |
| **Project 2** | Extent-based page table in the kernel | Page faults, rb-trees, linked lists, kernel synchronization, `perf stat`, `perf record`, `perf probe` |

---

# 1. Locks Bench

## What I Built

A benchmarking framework that tests 5 different lock implementations under contention. Each thread repeatedly acquires a lock, increments a counter inside the critical section, and releases the lock.

## The 5 Lock Implementations

### Lock 1: CAS Spinlock (`compare_and_swap.h`)

```c
void caslock_lock(caslock_t *lock) {
    while (__sync_val_compare_and_swap(&(lock->lock), 0, 1)) {}
}
```

**How it works:** `__sync_val_compare_and_swap(ptr, old, new)` is a GCC built-in that atomically does: "if `*ptr == old`, set `*ptr = new` and return `old`; otherwise return `*ptr`." The thread spins until it successfully swaps 0 -> 1.

**CPU-level:** Compiles to `lock cmpxchg`. The `lock` prefix locks the memory bus (or cache line) for the duration of the instruction, ensuring atomicity across cores.

**Problem under contention:** Every failed CAS attempt is still an atomic write attempt, which invalidates the cache line on all other cores (MESI protocol: modified -> invalid). This causes "cache-line bouncing" -- the line ping-pongs between cores' L1 caches.

### Lock 2: TAS Spinlock (`test_and_set.h`)

```c
void taslock_lock(taslock_t *lock) {
    while (__sync_lock_test_and_set(&(lock->lock), 1)) {}
}
```

**How it works:** `__sync_lock_test_and_set(ptr, val)` atomically writes `val` to `*ptr` and returns the old value. If old value was 0, we got the lock. If 1, someone else has it -- spin.

**CPU-level:** Compiles to `xchg`. On x86, `xchg` with memory is implicitly locked (no `lock` prefix needed).

**Difference from CAS:** TAS always writes (even on failure), CAS only writes on success. In practice both cause similar cache-line bouncing under high contention. TAS is slightly simpler (one less comparison).

### Lock 3: Ticket Lock (`ticket_lock.h`)

```c
typedef struct ticket_lock_t {
    volatile unsigned short next_ticket;
    char padding[64];                    // separate cache lines
    volatile unsigned short now_serving;
} ticket_lock_t;

void ticket_acquire(ticket_lock_t *lock) {
    unsigned int my_ticket = __sync_fetch_and_add(&(lock->next_ticket), 1);
    while (lock->now_serving != my_ticket) {
        sched_yield();
    }
}

void ticket_release(ticket_lock_t *lock) {
    lock->now_serving += 1;
}
```

**How it works:** Like a deli counter. Each thread takes a number (`fetch_and_add` on `next_ticket`), then waits until `now_serving` matches their number. This guarantees FIFO fairness -- no starvation.

**Key detail: `sched_yield()`** -- Instead of pure busy-waiting, the ticket lock calls `sched_yield()` which is a syscall that tells the OS "I have nothing useful to do, let another thread run." This means the ticket lock spends cycles in the **kernel scheduler** rather than spinning in userspace.

**The 64-byte padding** separates `next_ticket` and `now_serving` onto different cache lines, preventing false sharing.

### Lock 4: Mutex (`mutex_lock.h`)

```c
void mutex_lock(pthread_mutex_t *lock) {
    pthread_mutex_lock(lock);
}
```

**How it works internally:** `pthread_mutex_lock` uses a **futex** (fast userspace mutex) on Linux:
1. First tries an atomic CAS in userspace (fast path -- no syscall if uncontended)
2. If the lock is taken, calls `futex(FUTEX_WAIT)` which puts the thread to sleep in the kernel
3. When the lock holder calls `pthread_mutex_unlock`, it calls `futex(FUTEX_WAKE)` to wake one sleeping thread
4. The scheduler decides which thread runs next

**Why it's faster under contention:** Sleeping threads don't consume CPU cycles. The cores can either run other threads or enter idle states (C-states), saving power and avoiding cache-line bouncing.

### Lock 5: Semaphore (`semaphore_lock.h`)

```c
void semaphore_lock(sem_t *lock) {
    sem_wait(lock);
}
void semaphore_unlock(sem_t *lock) {
    sem_post(lock);
}
```

**How it works:** Initialized with `sem_init(lock, 0, 1)` -- value=1, so it acts as a binary semaphore (functionally identical to a mutex). `sem_wait` decrements; if the value would go below 0, the thread blocks. `sem_post` increments and wakes a waiter.

**Internally:** Also uses futex on Linux, similar to mutex. The difference is semantic -- semaphores can have count > 1 for producer-consumer patterns. Here with count=1, it behaves like a mutex.

## How I Profiled with `perf` (Locks Bench)

### Tool 1: `perf record` -- Function-Level Profiling

```bash
sudo perf record -g ./locks_bench 1 16 100000 10
sudo perf report --stdio --no-children
```

**`perf record -g`** -- Records CPU samples with call graph (`-g`). By default, perf samples at 4000 Hz (every ~250 microseconds, the kernel checks what instruction is executing and records it). Over millions of samples, this builds a statistical profile of where CPU time is spent.

**`perf report`** -- Aggregates samples by function. Shows:
- **Overhead %** -- what fraction of total samples landed in this function
- **Self %** -- time in the function itself (not callees)
- **Children %** -- time in the function + everything it calls

**What I found per lock type:**

| Lock | Where CPU Time Is Spent | Why |
|------|------------------------|-----|
| CAS | 93% in `caslock_lock` (spinning on `cmpxchg`) | Pure userspace busy-wait, threads never yield |
| TAS | Similar -- dominated by `taslock_lock` | Same busy-wait pattern, `xchg` loop |
| Ticket | `sched_yield` + `entry_SYSCALL_64` + `do_sched_yield` + `schedule` | Threads yield to kernel, scheduler runs constantly |
| Mutex | `futex_wait` + `futex_wake` + `schedule` + `cpuidle_enter` | Threads sleep, CPUs go idle, kernel wakes them |
| Semaphore | `cpuidle_enter` + `mwait_idle` + `futex_wait` + `sem_post` | Same as mutex -- threads sleep, CPUs idle |

### Tool 2: `cycles:u` vs `cycles:k` -- Userspace/Kernel Split

```bash
# Count CPU cycles spent in userspace only
perf record -e cycles:u ./locks_bench 1 16 100000 10

# Count CPU cycles spent in kernel only
perf record -e cycles:k ./locks_bench 1 16 100000 10

# Summarize by DSO (shared object / binary)
perf report --stdio --no-children --sort dso
```

**What `:u` and `:k` mean:**

Every CPU cycle executes at a **privilege level** (ring 0 = kernel, ring 3 = user). The performance monitoring unit (PMU) can filter events by privilege level:
- `cycles:u` -- only count cycles when the CPU is in ring 3 (userspace code)
- `cycles:k` -- only count cycles when the CPU is in ring 0 (kernel code)
- `cycles` (no suffix) -- count both

**What this tells you:**
- **Spinlocks (CAS, TAS):** ~95%+ cycles in userspace. The spinning loop runs entirely in user code.
- **Ticket lock:** Mixed. `sched_yield()` is a syscall, so the yield call itself crosses into kernel. Significant kernel cycles from the scheduler.
- **Mutex/Semaphore:** Majority of cycles in kernel. `futex_wait`/`futex_wake` and the scheduler are all kernel code. Userspace cycles are minimal -- the thread either has the lock (doing `do_work`) or is sleeping.

**`--sort dso`** groups results by which binary/library the cycles were in:
- `[kernel.kallsyms]` = time in the kernel
- `locks_bench` = time in your program
- `libc.so` / `libpthread.so` = time in library code

### Key Takeaways for Interview

**"Why is mutex faster than spinlocks under high contention?"**
> Spinlocks waste CPU cycles spinning. With 16 threads on 8 cores, 15 threads are always spinning and only 1 has the lock. Each spin attempt bounces the cache line between cores. Mutex puts contending threads to sleep -- they consume zero CPU cycles while waiting. The futex fast path means uncontended acquisition is still fast (just an atomic CAS, no syscall).

**"What was the perf evidence for this?"**
> `perf report` showed CAS spending 93% of cycles in the lock acquisition function. For mutex, cycles were in `futex_wait` and `cpuidle_enter` -- threads were sleeping and CPUs were idle. The `cycles:u` vs `cycles:k` split confirmed: spinlocks were ~95% userspace cycles (spinning), mutex was majority kernel cycles (sleeping/waking).

---

# 2. Project 1: Custom Syscall (`app_helper`)

## What I Built

A custom Linux syscall (number 548) that copies a buffer from userspace to kernel, sets every byte to 1, and copies it back. Then benchmarked the round-trip latency.

## The Kernel Patch (3 files)

### `syscall_64.tbl` -- Register the syscall number

```
548    common    app_helper    sys_app_helper
```

This maps syscall number 548 to the function `sys_app_helper`. When userspace calls `syscall(548, ...)`, the CPU traps into the kernel, looks up entry 548, and calls `sys_app_helper`.

### `syscalls.h` -- Declare the prototype

```c
asmlinkage long sys_app_helper(unsigned char __user *ubuf, size_t size);
```

**`asmlinkage`** -- tells the compiler that function arguments come from the stack (x86 syscall calling convention, not register-based).

**`__user`** -- annotation that tells the kernel static analysis tools "this pointer is from userspace, don't dereference it directly."

### `mmap.c` -- The implementation

```c
SYSCALL_DEFINE2(app_helper, unsigned char __user *, ubuf, size_t, size) {
    unsigned char *kbuf;
    long ret = 0;

    if (!ubuf) return -EINVAL;
    if (size == 0) return 0;
    if (size > (1024*1024)) return -EINVAL;    // cap at 1MB

    kbuf = kmalloc(size, GFP_KERNEL);          // allocate kernel buffer
    if (!kbuf) return -ENOMEM;

    if (copy_from_user(kbuf, ubuf, size)) {    // user -> kernel
        ret = -EFAULT;
        goto out;
    }

    memset(kbuf, 1, size);                     // set all bytes to 1

    if (copy_to_user(ubuf, kbuf, size)) {      // kernel -> user
        ret = -EFAULT;
        goto out;
    }

out:
    kfree(kbuf);
    return ret;
}
```

**`SYSCALL_DEFINE2`** -- macro that expands to the proper function signature with security/tracing hooks. The `2` means 2 arguments.

**`copy_from_user` / `copy_to_user`** -- safe memory copy between user and kernel address spaces. You can't just do `memcpy(kbuf, ubuf, size)` because:
1. The userspace pointer might be invalid (segfault)
2. The page might be swapped out
3. The kernel needs to handle faults gracefully (return -EFAULT) instead of panicking

These functions use special x86 instructions that allow the kernel to catch page faults from userspace addresses and return an error code instead of crashing.

**`kmalloc(size, GFP_KERNEL)`** -- kernel memory allocation. `GFP_KERNEL` means "this allocation can sleep if needed" (OK since we're in process context, not interrupt context).

**`goto out` pattern** -- standard Linux kernel cleanup pattern. Instead of nested if-else, you jump to a cleanup label that frees resources. This avoids memory leaks on error paths.

## The Benchmark (`benchmark_app_helper.c`)

```c
// Warmup: 1000 untimed iterations
for (long i = 0; i < WARMUP_ITERS; i++) {
    memset(buf, 4, buf_size);
    syscall(__NR_app_helper, buf, buf_size);
}

// Timed trials
for (int t = 0; t < trials; t++) {
    memset(buf, 4, buf_size);                  // only once per trial
    start_ns = now_ns();                        // clock_gettime(CLOCK_MONOTONIC)
    for (long i = 0; i < iterations; i++) {
        syscall(__NR_app_helper, buf, buf_size);
    }
    end_ns = now_ns();
    avg_ns = (end_ns - start_ns) / iterations;
}
```

**`clock_gettime(CLOCK_MONOTONIC)`** -- high-resolution monotonic timer. "Monotonic" means it never goes backwards (unlike wall clock which can be adjusted by NTP). Resolution is typically ~1ns on modern Linux.

**Why warmup matters:**
1. First iterations suffer **TLB misses** -- the page table entries for the buffer aren't cached yet
2. **Instruction cache** is cold -- the syscall handler code hasn't been loaded into L1i
3. **Branch predictor** hasn't learned the patterns yet
After ~1000 warmup iterations, caches are warm and measurements stabilize.

**Why `memset` only once:** If you call `memset(buf, 4, size)` inside the loop, you're measuring memset latency + syscall latency. By calling it once before the loop, you isolate just the syscall cost. (The syscall overwrites with 1s, but for benchmarking purposes we don't need to reset between iterations.)

## Results

| Buffer Size | Steady-State Latency | What Dominates |
|-------------|---------------------|----------------|
| 64 bytes | ~750 ns | Fixed syscall overhead (user->kernel transition) |
| 256 bytes | ~760 ns | Still dominated by fixed overhead |
| 4 KB | ~950 ns | `copy_from_user`/`copy_to_user` starting to matter |
| 64 KB | ~12,800 ns | Data copy dominates (cache effects) |

**Key insight:** For small buffers (64B, 256B), latency is nearly identical (~750ns) because the fixed cost of the syscall transition dominates -- entering ring 0, saving registers, looking up the handler, returning to ring 3. For 64KB, the actual data copying through `copy_from_user`/`copy_to_user` becomes the bottleneck.

---

# 3. Project 1 Part 2: Parallel Matrix Multiply via IPC

## What I Built

Two parallel matrix multiplication implementations using different IPC mechanisms:
1. **Shared memory** (`shmget`/`shmat`) -- processes read/write a shared buffer directly
2. **Pipes** -- processes communicate results through per-child pipes

## Implementation Details

### Shared Memory Version (`IPC-shmem.c`)

```c
// Allocate matrices in shared memory
int shm_a  = shmget(IPC_PRIVATE, size, IPC_CREAT | 0600);
int *A     = (int *)shmat(shm_a, NULL, 0);

// Fork children -- they inherit the shared memory mappings
for (int rank = 1; rank < P; rank++) {
    pid_t pid = fork();
    if (pid == 0) {
        multiply_rows(n, r0, r1, A, BT, C);  // writes directly to shared C
        _exit(0);
    }
}
```

**How `shmget`/`shmat` work:**
- `shmget(IPC_PRIVATE, size, IPC_CREAT | 0600)` -- creates a shared memory segment in the kernel. Returns an integer ID.
- `shmat(id, NULL, 0)` -- maps the segment into the calling process's virtual address space. Returns a pointer.
- After `fork()`, the child inherits the mapping. Both parent and child can read/write the same physical pages.
- **No synchronization needed** because each child writes to a disjoint set of rows in C. No two processes touch the same memory.

### Pipe Version (`IPC-pipe.c`)

```c
// Create per-child pipes
for (int i = 0; i < P; i++) {
    pipe(task_pipes[i]);    // parent -> child: send row assignment
    pipe(res_pipes[i]);     // child -> parent: send computed rows
    fcntl(res_pipes[i][1], F_SETPIPE_SZ, 1 << 20);  // 1 MiB pipe buffer
}
```

**Per-child pipes** instead of one shared pipe: The baseline used a single shared result pipe protected by a semaphore, which serialized all writes. With per-child pipes, each child has its own dedicated pipe -- no contention, no semaphore.

**`F_SETPIPE_SZ`** -- Linux pipes default to 64KB buffer. For a 5000x5000 matrix, each child's result can be several MB. Enlarging to 1MB reduces the number of `write()`/`read()` syscalls needed and avoids blocking on a full pipe.

**Data flow:**
```
Parent:  send task_t{start_row, end_row} --> task_pipe[i] --> Child i
Child i: compute rows, write results     --> res_pipe[i]  --> Parent
Parent:  read_all(res_pipe[i], &C[offset])  // directly into correct position
```

### The Cache Optimization: Tiled Loop

The original naive triple loop:
```c
for (i) for (j) for (k) C[i][j] += A[i][k] * B[k][j]  // B access is column-wise = cache disaster
```

My optimized version:
```c
// B is stored transposed as BT, so BT[j][k] = B[k][j]
// Tiled with TILE_K=64, TILE_J=64
for (kk = 0; kk < n; kk += 64)         // outer tile over k
  for (jj = 0; jj < n; jj += 64)       // outer tile over j
    for (i = r0; i < r1; i++)           // all rows assigned to this process
      for (j = jj; j < j_end; j++)      // inner j within tile
        for (k = kk; k < k_end; k++)    // inner k within tile
          C[i][j] += A[i][k] * BT[j][k]
```

**Why this loop order matters:**

The BT sub-block (64x64 ints = 16KB) fits in the 32KB L1d cache. With the loop order `kk -> jj -> i -> j -> k`:
- The BT sub-block `BT[jj..jj+64][kk..kk+64]` loads into L1d once
- Then ALL rows of A are processed against that same BT sub-block
- The BT sub-block stays in cache for the entire `i` loop
- Only after all rows are done does the next BT sub-block load

The broken order `kk -> i -> jj -> j -> k` would evict the BT sub-block every time `i` advances (because the A row displaces it).

**Transposing B:** The original B matrix has columns stored non-contiguously in memory (column j is at offsets 0, n, 2n, 3n...). By storing B transposed, the inner dot product `A[i][k] * BT[j][k]` accesses both arrays with stride-1 (sequential memory access), which is optimal for hardware prefetchers and cache lines.

## How I Profiled (Project 1 Part 2)

### Tool 1: `perf stat` -- Hardware Counter Measurement

```bash
perf stat -e cache-misses,cache-references,L1-dcache-load-misses,instructions,cycles,context-switches \
  ./IPC-shmem 2000 4
```

**What each counter measures:**

| Counter | What It Is | Hardware Mechanism |
|---------|-----------|-------------------|
| `cache-misses` | LLC (Last Level Cache, L3) misses -- had to go to RAM | PMU counter `LONGEST_LAT_CACHE.MISS` |
| `cache-references` | LLC accesses (hits + misses) | PMU counter `LONGEST_LAT_CACHE.REFERENCE` |
| `L1-dcache-load-misses` | L1 data cache misses -- had to go to L2 or further | PMU counter `L1D.REPLACEMENT` |
| `instructions` | Total instructions retired | PMU counter `INST_RETIRED.ANY` |
| `cycles` | Total CPU cycles consumed | PMU counter `CPU_CLK_UNHALTED.THREAD` |
| `context-switches` | OS scheduler switches to a different thread | Software event from kernel scheduler |

**IPC (Instructions Per Cycle)** = `instructions / cycles`. Higher is better:
- IPC ~1.0-1.2 = pipeline is stalling (waiting on memory)
- IPC ~1.9-2.0 = good utilization (data in cache, pipeline full)
- Modern x86 can theoretically retire 4-6 instructions/cycle

**Results:**
- Baseline: 549M cache misses, IPC 1.19
- Optimized: 34M cache misses, IPC 1.92
- **16x reduction in cache misses, 61% improvement in IPC**

### Tool 2: `perf report` -- Hotspot Analysis

```bash
perf record -g ./IPC-shmem 5000 4
perf report
```

**Results:** `multiply_rows` dominated at 95.8% (shmem) / 98.2% (pipe). Remaining: kernel page faults ~1.5%, memset <1%. This confirms the computation is the bottleneck, not IPC overhead.

### Tool 3: `taskset` -- CPU Pinning

```bash
taskset -c 0-3 ./IPC-shmem 5000 4    # pin to cores 0-3
taskset -c 0-7 ./IPC-shmem 5000 8    # pin to cores 0-7
```

**What `taskset` does:** Sets the CPU affinity mask for a process. Without it, the OS scheduler can migrate processes between cores, which invalidates caches. Pinning ensures:
- Each child process stays on its assigned core
- L1/L2 caches stay warm
- Results are reproducible (no scheduling noise)

**Scalability results:** Near-linear speedup up to 8 cores (7.0x on 8 cores for shmem). At 16 cores, speedup dropped to 6.87x because cores 8-15 are hyperthreads sharing physical execution units with cores 0-7.

---

# 4. Project 2: Extent-Based Page Table

## What I Built

Modified the Linux kernel's page fault handler to track which physical page frames are allocated to a process. Groups physically contiguous pages into **extents** (runs of adjacent physical frames). Exposes this data through 4 custom syscalls.

## The Data Structures (3-Level Hierarchy)

```
Level 1: procs_tree (global rb-tree, keyed by thread PID)
  |
  +-- rb_procs {pid, proc_extns, procs_node}
        |
        Level 2: proc_extns (per-thread rb-tree, keyed by VMA range)
          |
          +-- rb_extns {count, p_node, pg_tree}
                |
                Level 3: linked list of contiguous pages
                  |
                  +-- list_extns {vma, pfn, pg_list}
                  +-- list_extns {vma, pfn, pg_list}
                  +-- ...
```

**`struct list_extns`** -- one page mapping: virtual address + physical frame number + list link.

**`struct rb_extns`** -- one extent: a count + the head of a linked list of contiguous pages + an rb-tree node for searching.

**`struct rb_procs`** -- one thread: a PID + the root of its extent tree + an rb-tree node for the global tree.

**Why rb-trees?** O(log n) lookup. The page fault handler calls `handle_extents()` on EVERY page fault -- it must be fast. A hash table can't do range queries ("does this VMA fall within any extent's range?"), but an rb-tree can.

## The Core Algorithm: `add_extents`

When a new page fault produces mapping (vma, pfn):

```
1. Check should_join_front: is there an extent starting at (vma+4096, pfn+1)?
2. Check should_join_back:  is there an extent ending at (vma-4096, pfn-1)?

Case 1 (front only):    Prepend new page to front extent
Case 2 (back only):     Append new page to back extent
Case 3 (both):          Bridge merge -- join two extents into one, delete the extra rb node
Case 4 (neither):       Create a new single-page extent
```

The bridge merge (case 3) is the most interesting: the new page fills the gap between two existing extents, so they get spliced into one extent and the extent count decreases.

## The Page Fault Hooks

Two one-line hooks in the kernel:

```c
// mm/memory.c -- anonymous page faults (heap, stack, mmap anonymous)
handle_extents(vmf->address, page_to_pfn(page));

// mm/filemap.c -- file-backed page faults (mmap'd files, shared libraries)
handle_extents(vmf->address, page_to_pfn(page));
```

Both placed right after the kernel allocates the physical page but before the PTE is installed.

**`handle_extents` fast path:** First instruction checks `current->tgid == do_extents_tgid`. For the 99.9% of processes not being tracked, this is a single comparison and return -- near-zero overhead.

## The 4 Syscalls

| # | Name | What It Does |
|---|------|-------------|
| 335 | `enable_extents` | Sets `do_extents_tgid = current->tgid`. Only one process group at a time. |
| 336 | `disable_extents` | Resets tracking, calls `free_procs()` to free all memory. |
| 337 | `get_ex_count` | `copy_to_user` the extent count and page count to userspace pointers. |
| 338 | `print_ex_pid` | Debug helper -- prints PID/TGID hierarchy to dmesg. |

## Kernel Synchronization

**`DEFINE_SPINLOCK(extns_lock)`** -- protects the global `procs_tree`.

**`spin_lock_irqsave(&extns_lock, flags)`** -- used instead of plain `spin_lock` because `handle_extents` runs in the page fault path. If an interrupt fired while holding the lock and the handler also triggered a page fault, you'd deadlock. `irqsave` disables local interrupts while the lock is held.

**`atomic_t pt_count` / `atomic_t total_extents`** -- lockless counters. Can be read/incremented/decremented without holding a lock.

## How I Profiled (Project 2)

### Campaign 1: `perf stat` -- Aggregate Overhead

```bash
sudo perf stat -e faults -r 1000 --table --output st.out ./st.o 1 65536
sudo perf stat -e faults -r 1000 --table --output est.out ./est.o 1 65536
```

**`-e faults`** -- counts the `PERF_COUNT_SW_PAGE_FAULTS` software event. Every time the kernel's page fault handler runs for this process, the counter increments. Internally: `perf_event_open()` sets up a counter in the `task_struct`; the fault handler bumps it.

**`-r 1000`** -- runs the program 1000 times. Computes mean, stddev, min, max. This is crucial for statistical significance -- a single run can have 10-20% variance from scheduling noise, cache state, etc.

**`--table`** -- shows per-run data so you can check for outliers.

Comparing `st.out` (baseline) vs `est.out` (extent-enabled) gives the software overhead of extent tracking. The programs themselves also print CSV with timing and extent/page counts.

### Campaign 2: `perf record` + `perf probe` -- Per-Fault Latency

#### Step 1: Create dynamic tracepoint

```bash
sudo perf probe do_anonymous_page
```

**How `perf probe` works internally (kprobes):**
1. Looks up `do_anonymous_page` in `/proc/kallsyms` to find its address (e.g., `ffffffff812a3f40`)
2. Saves the original instruction at that address (e.g., `push rbp` = 0x55)
3. Overwrites it with `int3` (0xCC, debug breakpoint)
4. Every time any process calls `do_anonymous_page`, the CPU hits `int3`:
   - Trap handler fires, records event (timestamp, PID, CPU)
   - Original instruction is single-stepped via trap flag
   - `int3` is re-inserted for next time
5. Each probe hit costs ~0.5-1 microsecond

#### Step 2: Record events

```bash
sudo perf record -e faults -a ./est.o 1 65536
```

**`-e faults`** -- records every page fault event with metadata.
**`-a`** -- system-wide. Records on all CPUs (needed for multi-threaded tests where threads migrate between cores).

Internally: kernel writes events to per-CPU ring buffers. Each event contains: timestamp (nanosecond resolution), PID, CPU number, instruction pointer, event type. When the program exits, ring buffers are flushed to `perf.data`.

#### Step 3: Extract data

```bash
sudo perf script | grep 'set_bufs' | awk '{print cpu, timestamp_us, event}'
```

**`perf script`** -- converts binary `perf.data` to text. Each line = one event:
```
est.o  1234 [002]  5765.123456:  faults:  ffffa1234 set_bufs+0x1a
```

**`grep 'set_bufs'`** -- filters to faults inside the page-touching loop only (ignoring startup/cleanup faults).

**`awk` processing** -- extracts CPU number and converts timestamp from seconds to microseconds.

#### Step 4: Clean up probe

```bash
sudo perf probe -d do_anonymous_page
```

Restores original instruction, removes kprobe. Kernel goes back to normal.

### What I Measured

**Test matrix:** 7 buffer sizes (64KB-256MB) x 7 thread counts (1-64) x 2 fault types (anonymous + file-backed) x 2 versions (normal + extent-enabled)

**Overhead comparison:** Time difference between `st.o` (normal) and `est.o` (extent-enabled) = cost of running `handle_extents()` on every page fault.

**Per-fault latency:** Computed `time_diff` between consecutive fault timestamps on the same thread. Compared distributions between normal and extent versions.

**TLB savings:**
```
From syscall 337 (get_ex_count):
  pages   = total page faults = total 4KB pages mapped
  extents = number of contiguous groups

TLB entries saved = pages - extents
Compression ratio = pages / extents
```

Example: 10,000 pages, 500 extents = 95% TLB entry reduction. An extent-aware TLB would need 20x fewer entries.

## Graphs Generated

**From `perf stat` data (`gen_graph.py`):**
1. **3D surface: avg time vs threads vs buffer size** -- green (extents) vs teal (normal) overlaid surfaces. Gap = overhead.
2. **3D surface: avg extent count vs threads vs buffer size** -- how fragmented physical memory is.
3. **CDF of extent counts** -- distribution of extent counts across 1000 runs. Steep = consistent.

**From `perf record` data (`gen_plot.py`):**
1. **3D latency surface: per-fault inter-arrival time** -- normalized, per buffer size. Shows whether extent tracking adds per-fault delay.

---

# Complete `perf` Tool Reference Across All Projects

| Tool | Command | What It Does | Which Project |
|------|---------|-------------|---------------|
| `perf stat` | `perf stat -e events program` | Counts events for program lifetime | P1-Part2, P2 |
| `perf stat -r N` | `perf stat -r 1000 program` | Repeats N times, gives statistics | P2 |
| `perf stat -e cycles:u` | Filter to userspace cycles | Locks Bench |
| `perf stat -e cycles:k` | Filter to kernel cycles | Locks Bench |
| `perf stat -e faults` | Count page faults | P2 |
| `perf stat -e cache-misses,L1-dcache-load-misses` | Count cache misses | P1-Part2 |
| `perf record` | `perf record -g program` | Sample CPU activity into `perf.data` | Locks Bench, P2 |
| `perf record -a` | System-wide recording (all CPUs) | P2 |
| `perf report` | `perf report --stdio` | Show function-level hotspots | Locks Bench, P1-Part2 |
| `perf report --sort dso` | Group by binary/library | Locks Bench |
| `perf probe` | `perf probe function_name` | Create dynamic kprobe tracepoint | P2 |
| `perf probe -d` | `perf probe -d function_name` | Remove kprobe | P2 |
| `perf script` | `perf script` | Dump per-event text from `perf.data` | P2 |

---

# Quick-Fire Interview Answers

**Q: What's the difference between a spinlock and a mutex?**
> A spinlock busy-waits in a tight loop consuming CPU cycles. A mutex puts the thread to sleep via futex -- zero CPU consumption while waiting. Under low contention, spinlocks are faster (no syscall). Under high contention, mutexes win because sleeping threads don't waste cycles or cause cache-line bouncing. My perf data showed this: CAS spinlock had 93% of cycles in the spin loop; mutex had cycles in `futex_wait` and `cpuidle_enter`.

**Q: What does `copy_to_user` do and why can't you just use `memcpy`?**
> `copy_to_user` safely copies data from kernel to userspace. You can't use `memcpy` because the userspace pointer might be invalid, swapped out, or pointing to unmapped memory. `copy_to_user` uses special CPU instructions that let the kernel catch page faults from userspace addresses and return -EFAULT instead of panicking.

**Q: How did you optimize the matrix multiply cache performance?**
> Two things: (1) Stored B transposed so the inner dot product has stride-1 access instead of stride-N. (2) Tiled the loop with 64x64 tiles in the kk->jj->i->j->k order so the BT sub-block (16KB) stays in L1d cache while all rows are processed. This reduced cache misses 16x (549M -> 34M) and improved IPC from 1.19 to 1.92.

**Q: How does the extent merge algorithm work?**
> When a new (vma, pfn) arrives, I check if the page immediately after it is the start of an existing extent (front merge) and if the page immediately before it is the end of an existing extent (back merge). If both match, the new page bridges two extents -- I splice them into one using `list_splice` and delete the extra rb-tree node. If only one matches, I prepend or append. If neither matches, I create a new extent. Contiguity is checked in both virtual AND physical address space.

**Q: Why did you use `spin_lock_irqsave` in the kernel?**
> `handle_extents` runs in the page fault path. If we held a regular `spin_lock` and an interrupt fired on the same CPU, and the interrupt handler also triggered a page fault that tried to acquire the same lock, we'd deadlock. `irqsave` disables local interrupts while the lock is held, preventing this.

**Q: What did the TLB savings calculation show?**
> My `get_ex_count` syscall returns the extent count and page count. The ratio pages/extents tells you how many TLB entries an extent-aware TLB would save. For example, if 10,000 pages collapse into 500 extents, that's a 20x reduction in TLB entries needed -- from 10,000 entries to 500. This directly translates to fewer TLB misses for large working sets.

**Q: How does `perf probe` work at the CPU instruction level?**
> It uses kprobes. The kernel looks up the function address in `/proc/kallsyms`, saves the original first instruction, and replaces it with `int3` (breakpoint). When the function is called, the CPU hits the breakpoint, traps to a handler that records the event (timestamp, PID, etc.), single-steps the original instruction via the trap flag, re-inserts `int3`, and continues. Each probe hit costs ~0.5-1 microsecond.

**Q: What's the difference between `perf stat` and `perf record`?**
> `perf stat` counts events for the program's lifetime and gives you totals (e.g., "549 million cache misses"). `perf record` captures individual events with timestamps and metadata into `perf.data`, which you then analyze with `perf report` (hotspot analysis) or `perf script` (per-event dump). `perf stat` has near-zero overhead; `perf record` has higher overhead because it writes every event to a ring buffer.
