# CFS Scheduler for xv6 Operating System

## Table of contents
1. [Project summary](#project-summary)  
2. [Motivation & goals](#motivation--goals)  
3. [What was implemented](#what-was-implemented)  
4. [Design & algorithms](#design--algorithms)  
   - Red-Black tree as the ready set  
   - Virtual runtime (vruntime) and fairness metric  
   - Niceness & weight calculation  
   - Preemption policy  
5. [Code changes — key functions & files](#code-changes--key-functions--files)  
   - `procinit`, `allocproc`, `scheduler`, `yield`, `wakeup`, `kill`  
   - `insertProc`, `removeProc`, `getMinProc` (rb-tree helpers)  
   - `setNice` system call  
6. [How to build & run](#how-to-build--run)  
7. [Benchmarks & experimental results](#benchmarks--experimental-results)  
8. [Runtime logging & monitoring](#runtime-logging--monitoring)  
9. [Project structure & important files](#project-structure--important-files)  
10. [Limitations, lessons learned, and future work](#limitations-lessons-learned-and-future-work)  
11. [FAQ & Troubleshooting](#faq--troubleshooting)  

---

# Project summary

This project implements a **Completely Fair Scheduler (CFS)** for the xv6 teaching operating system. The original xv6 uses a Round-Robin (RR) scheduler. The CFS implementation replaces the RR ready queue with a red-black tree keyed by each process's *virtual runtime* (`vruntime`), and adjusts scheduling decisions to fairly allocate CPU time based on dynamic process weights computed from a `nice` value.

The implementation includes:
- red-black tree (RB-tree) data structure to store runnable processes (O(log n) insert/remove/min lookup),
- virtual runtime bookkeeping and weight calculation,
- a `setNice` system call for user control of niceness,
- instrumented trace/log output for benchmarking and visualization,
- a benchmark suite to compare CFS vs RR under different workload mixes (CPU-bound, I/O-bound, mixed).

---

# Motivation & goals

From a research perspective, the main goals were:
- Demonstrate how a CFS-style scheduler can be realized inside xv6.
- Show that a tree keyed by `vruntime` provides efficient access to the next process and preserves fairness.
- Explore niceness → weight mapping and the effect on turnaround/waiting time.
- Compare responsiveness and fairness of CFS against the original RR scheduler in microbenchmarks.

From a developer perspective, the goals included:
- Produce a clean, modular change set so maintainers can enable/disable CFS.
- Expose a user-space interface (`setNice`) to tune process priorities.
- Provide logs and simple visualizable output to reproduce the evaluation.

---

# What was implemented

- **RB-tree ready set**: A self-balancing red-black tree stores runnable processes. The scheduler always selects the process with the smallest `vruntime`.
- **Virtual runtime (vruntime)**: Each process accumulates `currentRuntime` while running. After it yields, sleeps, or is preempted, `p->virtualRuntime += p->currentRuntime`. This is the scheduling key.
- **Weight & niceness**: A niceness value in range `[0, 30]` is provided for each process; lower `nice` → higher priority. Weight is computed by `weight = 1024 / (1.25^nice)` (project's chosen mapping).
- **Preemption**: The running process can be preempted if its `vruntime` becomes larger than the minimum `vruntime` among RUNNABLE processes by a threshold. This check is performed at yield points and on timer tick boundaries.
- **Instrumentation & benchmark**: The kernel logs process switch events to enable post-processing for waiting/turnaround time and creating Gantt-like visualizations.

---

# Design & algorithms

## Red-Black tree for the ready set
- **Why RB-tree?** CFS requires quick insertion, deletion, and retrieval of the minimum `vruntime`. RB-tree guarantees O(log n) complexity and keeps operations efficient with relatively simple balancing logic.
- **Operations added**: `rb_insert(process)`, `rb_remove(process)`, `rb_min()` — plus helpers for rotations and fixup cases. Convenience functions `isEmpty()` and `count()` are also provided.

## Virtual runtime (vruntime)
- **Definition**: `vruntime` is an abstract per-process clock measuring how much weighted CPU time a process has consumed.
- **Update rule**: After a process runs for `currentRuntime` (measured in ticks), we update:
  ```c
  p->virtualRuntime += p->currentRuntime;
  p->currentRuntime = 0;
  ```
- The scheduler selects the runnable process with the **minimum** `virtualRuntime` — giving preference to those that have received less CPU time relative to their weight.

## Niceness & weight calculation
- **Niceness domain**: `[0, 30]` (project-specific choice).
- **Weight calculation**: The implemented `calculateWeight(nice)` uses an exponential decay:
  ```
  denom = 1.25^nice
  weight = 1024 / denom
  ```
  This maps higher `nice` (less desirable) to smaller weight; processes with larger weight receive proportionally more CPU share.

## Preemption policy
- **Goal**: Ensure fairness and responsiveness. When the current process's `vruntime` grows beyond the next contender's `vruntime` by a threshold, the scheduler triggers preemption.
- Practically implemented as a `checkPreemption()` invoked at yield points and on timer tick boundaries.

---

# Code changes — key functions & files

> All modifications are clustered in the process management and scheduler sources (e.g., `proc.c`, `proc.h`, `defs.h`), plus the small userland wrapper for `setNice`.

### Major modified/added functions
- **`procinit()`**  
  Initialize RB-tree and per-process scheduling fields (e.g., `virtualRuntime`, `currentRuntime`, `nice`, `weight`) alongside original initialization code.

- **`allocproc()`**  
  When allocating a new process, set initial scheduling fields (`virtualRuntime = 0`, default `nice`, compute `weight`, `currentRuntime = 0`).

- **`scheduler()`**  
  Main loop replaced/augmented: instead of scanning a circular list for RUNNABLE processes (RR), call `rb_min()` to retrieve the process with minimum `vruntime`, remove it from the tree, set it RUNNING, switch to it, and on return update its `vruntime` and (if still runnable) reinsert.

- **`yield()`**  
  On voluntary yield: acquire lock, update `currentRuntime` → `virtualRuntime`, reset `currentRuntime`, reinsert into RB-tree as RUNNABLE, call `sched()`.

- **`wakeup(chan)`**  
  For processes being woken from sleep: reset `currentRuntime`, update `virtualRuntime` as needed, set state to RUNNABLE and insert into RB-tree.

- **`kill(pid)`**  
  Mark process `killed`, if it is RUNNABLE or SLEEPING update `currentRuntime` and `virtualRuntime`, and remove/insert as necessary to maintain RB-tree invariants.

- **RB-tree helpers**  
  `insertProc(p)`, `removeProc(p)`, `getMinProc()` with correct rotations and `fixup` rules for inserts/removals.

- **`setNice(int nice)` system call**  
  Exposed to userland with prototype:
  ```c
  int setNice(int nice);
  ```
  Validates `nice` in [0,30], updates `p->nice` and `p->weight = calculateWeight(nice)`. Returns `0` on success, `-1` on invalid value.

---

# How to build & run

> These are generic xv6 build/run steps adapted to this project. The exact commands may vary with your xv6 fork.

1. **Apply the patch / copy files**  
   - Merge/replace the modified `proc.c`, `proc.h`, RB-tree files, and `user/setNice.c` (if provided) into your xv6 source tree.

2. **Build xv6**
   ```sh
   make clean
   make
   ```

3. **Run in QEMU**
   ```sh
   make qemu
   ```
   or
   ```sh
   make qemu-nox   # if you prefer no graphical console
   ```

4. **Install userland test programs**
   - Ensure `benchmark` userland programs are built and included in `UPROGS` so `make` installs them into the xv6 disk image.

5. **Run benchmark(s) inside xv6**
   - Example (inside xv6 shell):
     ```sh
     benchmark cfs
     # Or set niceness:
     setNice 10
     ./cpu_bound_proc
     ```

6. **Collect logs**  
   Redirect QEMU output to a host file if you want to postprocess logs:
   ```sh
   make qemu > qemu.log
   ```
   Then postprocess `qemu.log` with the provided analyzer script to produce CSVs or graph data.

---

# Benchmarks & experimental results

**Benchmarks performed**
- CPU-bound processes (heavy compute loops)
- I/O-bound processes (frequent sleeps/waits)
- Mixed workloads
- Comparisons: CFS vs original RR scheduler

**Key findings**
- **Fairness**: CFS reduced variance in CPU allocation across processes; starvation risk decreased compared to RR.
- **Waiting time**: Average waiting time often decreased under CFS in mixed and CPU-bound settings.
- **Responsiveness**: Interactive or I/O-bound tasks generally experienced better responsiveness under CFS because `vruntime` favored those that had run less recently.
- **Overhead**: RB-tree operations add O(log n) cost versus O(1) RR queue ops; in tested workloads the fairness benefits outweighed overhead.

For exact numerical results, run the included `benchmark` program and use the `log_parser` scripts in the repository to produce CSVs/plots.

---

# Runtime logging & monitoring

The kernel is instrumented to produce events on every context switch. The log format contains a simple switch line, for example:
```
[SWITCH] time=<tick> out_pid=<pid_out> out_state=<state> in_pid=<pid_in> in_state=<state>
```
To analyze:
1. `make qemu > qemu.log`
2. `python3 tools/log_parser/parse_qemu_log.py qemu.log > schedule.csv`
3. `python3 tools/log_parser/plot_results.py schedule.csv --out results.png`

The repository includes sample parser scripts and example plots exported from the experiments.

---

# Project structure & important files

```
  ├─src/
  │   ├─ kernel/
  │   │   ├─ proc.c        # main changes: scheduler, allocproc, yield, wakeup, kill
  │   │   ├─ proc.h
  │   │   ├─ rb_tree.c     # red-black tree implementation (insert/remove/min)
  │   │   └─ rb_tree.h
  │   ├─ user/
  │   │   ├─ setNice.c     # user wrapper for syscall
  │   │   └─ benchmark.c   # user benchmark programs (CPU/I/O/mixed)
  └─ README.md
```

---

# Limitations, lessons learned, and future work

**Limitations**
- The niceness range `[0,30]` and the exact `weight` mapping are project-specific choices; other mappings (Linux-like) may behave differently.
- RB-tree increases kernel complexity and memory overhead.
- Single-runqueue design — no per-CPU runqueues or load balancing implemented in this educational port.

**Lessons learned**
- A small per-process key (`vruntime`) plus a balanced tree provides an elegant, provably fair approach.
- Instrumentation is crucial to demonstrate scheduler behavior.

**Future work**
- Per-CPU runqueues and load balancing for SMP experiments.
- Explore alternative niceness/weight mappings and negative niceness values.
- Implement hierarchical scheduling or deadline schedulers.

---

# FAQ & Troubleshooting

**Q: Kernel panics after adding RB-tree code.**  
A: Common causes:
- Uninitialized pointers or missing lock handling around RB-tree updates. Ensure `acquire(&ptable.lock)` / `release(&ptable.lock)` wrap all mutations.
- Incorrect insert/remove fixup logic — check rotations and color assignments.

**Q: `setNice(...)` returns -1.**  
A: `setNice` validates `nice` in `[0,30]`. Also ensure the syscall is registered in `syscall.h` and `syscall.c`.

**Q: Benchmark results are inconsistent.**  
A: Make sure you:
1. Build a kernel image containing the CFS changes (no mixed binaries).
2. Use consistent `UPROGS` so userland binaries match the kernel.
3. Redirect and analyze full QEMU logs with the provided parser.

---
