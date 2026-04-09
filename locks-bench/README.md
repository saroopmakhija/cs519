## CS519 Homework: Lock Implementations Analysis and Performance Analysis with `perf` (10 points)
In this simple homework, you will explore different lock implementations,
measure their performance using `perf`, and analyze where the time/overhead is
spent. You will be working with a provided benchmarking program that tests
multiple lock types under contention. **Please use
[CloudLab](https://www.cloudlab.us/) nodes** for running your experiments. If
you encounter any problems at any step, **please use Piazza** to pose your
questions and discuss solutions. 

This step will help you in other future assignments by using tools like perf.

---

## Part 1: Setup and Compilation

1. **Clone the repository**

    git clone <repo-url>
    cd locks-bench

   Replace `<repo-url>` with the actual URL provided by your instructor or posted on your course site.

2. **Compile the benchmark**

    make

   After this step, you should have an executable named `locks_bench` in the directory.

---

## Part 2: Running the Benchmark

The benchmark program is invoked as follows:

```
Usage: ./locks_bench <lock_type> [num_threads] [iterations] [work_length]

Where `<lock_type>` can be one of:
- `1`: Spin lock (Compare-and-Swap)
- `2`: Spin lock (Test-and-Set)
- `3`: Spin lock (Ticket Lock)
- `4`: Mutex
- `5`: Semaphore
```

First understand the logic of the code. Run the benchmark for each lock type at least once. Please experiment with different values to see how scalability and contention affect performance.

---

## Part 3: Installing and Using `perf`

1. **Install `perf`**  
   (Commands below are for Ubuntu with kernel version `5.15.0-122-generic`; adjust as necessary.)

```
    sudo apt update
    sudo apt install linux-tools-common linux-tools-5.15.0-122-generic
```

   Check your kernel version with:
```
    uname -r
```
2. **Verify that `perf` is installed**:
```
    perf -v
```
3. **Allow `perf` access**:
```
    echo -1 | sudo tee /proc/sys/kernel/perf_event_paranoid
```
   This allows `perf` to record events with fewer restrictions.

4. **Run `perf` with the benchmark**:
```
    perf record -a -g ./locks_bench <lock_type>

   - `-a`: System-wide collection (on all CPUs).
   - `-g`: Capture call stacks, helping you see where time is spent.
```
5. **View the `perf` report**:
```
    perf report -g "graph,0.5,caller"
```
   This launches an interactive report showing the hierarchical breakdown of CPU cycles per function.

**Example excerpt**:
```
    Samples: 28K of event 'cycles', Event count (approx.): 6295953030
      Children      Self  Command          Shared Object         Symbol
    +   99.62%     0.00%  locks_bench      libc.so.6             [.] start_thread
    +   99.60%     0.01%  locks_bench      locks_bench           [.] inc_thread
    +   97.77%     1.35%  locks_bench      locks_bench           [.] acquire_lock
    +   96.42%    96.07%  locks_bench      locks_bench           [.] caslock_lock
    +    1.03%     1.03%  locks_bench      locks_bench           [.] release_lock
    +    0.81%     0.80%  locks_bench      locks_bench           [.] do_work
         0.28%     0.00%  locks_bench      [unknown]             [.] 0xffffffff95e00f0b
```
- Look at which functions dominate CPU usage (`acquire_lock`, `caslock_lock`, etc.).  
- Use `Shift + +` in interactive mode to expand entries and reveal full call stacks.

For more details on `perf`, see:
- [Brendan Gregg’s Perf Guide](https://www.brendangregg.com/perf.html)
- [perf Wiki](https://perfwiki.github.io/main/)

---

## Part 4: Data Collection and Graphs

1. **Collect performance data**  
   - Run the benchmark for each lock type (`1` through `5`) multiple times.
   - Vary the number of threads (or other parameters if available).

2. **Gather metrics**  
   - Execution time or throughput (if the benchmark prints it).
   - CPU cycles or time spent in lock acquisition (`perf report`).
   - Any other relevant metrics.

3. **Visualize your results**  
   **Create two graphs**:
   1. A **bar chart** comparing the performance (e.g., execution time or throughput) of each lock type at a fixed thread count.
   2. A **bar chart** comparing the performance (e.g., execution time or throughput) of each lock type for different input sizes.

---

## Part 5: Analysis and Report

   - Based on your bar chart and `perf` data, which lock has the highest overhead for different thread configurations and input sizes?
   - How much is the time spent on the userspace vs. the OS? 
   - Which functions dominate CPU usage?


Write down your analysis in a report and submit.
---

You can read more about perf \
https://www.brendangregg.com/perf.html \
https://perfwiki.github.io/main/
