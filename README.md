# AeroDrome-Analysis

A dynamic atomicity checker for multithreaded C programs, built as an [Intel Pin](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-dynamic-binary-instrumentation-tool.html) tool. It implements the **AeroDrome** vector-clock algorithm to detect **conflict serializability violations** in `pthread` programs while they run.

> CS636 course project.

## Table of Contents

- [Background](#background)
- [How It Works](#how-it-works)
- [Repository Structure](#repository-structure)
- [Prerequisites](#prerequisites)
- [Build](#build)
- [Usage](#usage)
- [Writing Programs for the Tool](#writing-programs-for-the-tool)
- [Output](#output)
- [Limitations](#limitations)
- [References](#references)
- [Acknowledgments](#acknowledgments)

## Background

In a multithreaded program, blocks of code that the programmer intends to be *atomic* (transactions) can be interleaved with other threads in ways that break that intent. An execution is **conflict serializable** if it is equivalent to one in which every transaction runs serially.

Earlier checkers detect violations by finding cycles in a graph of transactions, which can grow quadratically with trace length. **AeroDrome** (Mathur and Viswanathan, ASPLOS 2020) is a single-pass, linear-time algorithm that instead assigns vector timestamps to events and detects violations online. This project implements that algorithm on top of Pin's dynamic binary instrumentation, so it checks a real program's execution rather than a pre-recorded trace.

## How It Works

The Pin tool (`myproject.cpp`) instruments the target binary at two levels:

- **Routine level:** entry and exit hooks on every routine identify the program's structure by function name (`main`, `global_vars`, `thrd*`, `txn*`) and intercept `pthread_mutex_lock` / `pthread_mutex_unlock`.
- **Instruction level:** every memory read and write is intercepted. Accesses to the registered shared variables are passed to the algorithm, guarded by a per-variable Pin lock so the analysis state stays consistent.

The checker maintains the following state, as in the AeroDrome algorithm:

| Variable in code    | Meaning                                                      |
| ------------------- | ------------------------------------------------------------ |
| `C[t]`              | Vector clock of thread `t`                                   |
| `Ct[t]`             | Vector clock of `t` at the start of its current transaction  |
| `L[l]`              | Vector clock of the last release of lock `l`                 |
| `wrt[x]`            | Vector clock of the last write to variable `x`               |
| `rd[t][x]`          | Vector clock of the last read of `x` by thread `t`           |
| `lastwrt`, `lastrel` | Last writer of each variable and last releaser of each lock |
| `act_txn[t]`        | Number of active (possibly nested) transactions of thread `t` |

Each program event maps to an algorithm handler:

| Program event                                   | Handler              |
| ----------------------------------------------- | -------------------- |
| Entry of a `thrd*` function                     | `fork(parent, child)` |
| Return of a `thrd*` function                    | `join(parent, child)` |
| Entry of a `txn*` function                      | `begin(t)`           |
| Return of a `txn*` function                     | `end(t)`             |
| Return of `pthread_mutex_lock` on a tracked lock   | `acquire(t, l)`   |
| Return of `pthread_mutex_unlock` on a tracked lock | `release(t, l)`   |
| Read of a tracked variable                      | `read(t, x)`         |
| Write to a tracked variable                     | `write(t, x)`        |

A violation is reported when, on merging a clock into thread `t` (`checkAndGet`), the clock at the start of `t`'s active transaction is already ordered before it. That means another thread's action sits between two parts of one transaction.

## Repository Structure

| File                  | Description |
| --------------------- | ----------- |
| `myproject.cpp`       | The Pin tool that implements the AeroDrome checker. |
| `makefile`            | Standard Pin tool makefile (includes Pin's build configuration). |
| `makefile.rules`      | Pin build rules; registers `myproject` as the tool to build. |
| `sample.c`            | A small example target program that follows the tool's conventions. |
| `test1.c` – `test9.c` | Test programs for the checker. |
| `projecct_report.pdf` | Project report. |

## Prerequisites

- Linux on x86-64 (Intel Pin does not run on other platforms)
- A copy of [Intel Pin](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-dynamic-binary-instrumentation-tool.html), with the `PIN_ROOT` environment variable pointing at its root directory
- `g++` and `make`
- `gcc` with `pthread` support, to compile the test programs

## Build

The `makefile` looks for Pin's configuration under `$PIN_ROOT/source/tools/Config`, so either set `PIN_ROOT` or place this repository inside Pin's `source/tools/` directory.

```bash
export PIN_ROOT=/path/to/pin

git clone https://github.com/ashutoshkr4458/AeroDrome-Analysis.git
cd AeroDrome-Analysis

make obj-intel64/myproject.so
```

This produces the tool at `obj-intel64/myproject.so`.

## Usage

1. Compile a target program. Use `-O0` so the compiler does not inline the `txn*` and `thrd*` functions the tool looks for by name.

   ```bash
   gcc -O0 -g -pthread -o sample sample.c
   ```

2. Run it under Pin with the tool:

   ```bash
   $PIN_ROOT/pin -t obj-intel64/myproject.so -- ./sample
   ```

3. Read the verdict:

   ```bash
   cat serialisable.out
   ```

Repeat with the other programs, for example `test1.c` through `test9.c`.

## Writing Programs for the Tool

The tool finds what to monitor from **function names** and a fixed program structure rather than from annotations:

| Convention | Purpose |
| ---------- | ------- |
| `main` | Analysis is active from entry to exit of a routine whose name starts with `main`. |
| `global_vars()` | Called once at the start of `main`, before any threads are created. It registers what to track: pass the address of each shared variable to `write_(&var)`, and call `pthread_mutex_lock(&lock)` on each lock. When it returns, the tool numbers the variables and locks and initialises its state. |
| `thrd*` | Any function whose name starts with `thrd` is treated as a thread entry point. |
| `txn*` | Any function whose name starts with `txn` is treated as a transaction (atomic block). Nested calls are supported. |

Only the variables and locks registered in `global_vars()` are analysed.

A minimal skeleton:

```c
#include <pthread.h>

pthread_mutex_t lock;
pthread_t t1, t2;
int x, y;

void write_(int *a) { *a = 0; }        // registers a shared variable

void global_vars() {
    pthread_mutex_lock(&lock);         // registers the lock
    pthread_mutex_unlock(&lock);
    write_(&x);                        // registers x
    write_(&y);                        // registers y
}

void *txn1(void *arg) {                // one atomic block
    x++;
    y++;
    return NULL;
}

void *thrd1(void *arg) { txn1(0); return NULL; }   // thread entry
void *thrd2(void *arg) { txn1(0); return NULL; }   // thread entry

int main() {
    global_vars();
    pthread_create(&t1, NULL, thrd1, NULL);
    pthread_create(&t2, NULL, thrd2, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return 0;
}
```

See `sample.c` and the `test*.c` files for complete examples.

## Output

The tool writes its result to `serialisable.out` in the working directory.

| Content of `serialisable.out`             | Meaning |
| ----------------------------------------- | ------- |
| `This is initialisation`                  | Written at start-up (always the first line). |
| `Conflict serialisability violation`      | A violation was found. The tool stops the analysis and exits at this point. |
| `The transactions are serialisable`       | The program ran to completion with no violation. |
| `Parent tid not init`, `Error in join ptid`, `Error in join tid` | Thread bookkeeping errors, usually caused by threads that do not follow the `thrd*` naming convention. |

The tool also prints some debug lines (such as unlock and transaction-return events) to standard output while the target runs.

## Limitations

- **Intel Pin, x86-64 Linux only.**
- **Convention-based instrumentation.** Transactions, threads and shared variables are identified by function names and by registration in `global_vars()`, not inferred automatically.
- **At most 16 threads.** The thread bound is a constant (`thrd = 16`) in `myproject.cpp`.
- **Exact-address matching.** A memory access is tracked only if its effective address equals a registered variable's address.
- **Stops at the first violation.** The tool exits as soon as one is found.
- **Compile without optimisation** (`-O0`) so the named functions survive in the binary.

## References

- Umang Mathur and Mahesh Viswanathan. *Atomicity Checking in Linear Time using Vector Clocks.* ASPLOS 2020. [doi:10.1145/3373376.3378475](https://doi.org/10.1145/3373376.3378475) · [arXiv:2001.04961](https://arxiv.org/abs/2001.04961)
- Intel Pin: dynamic binary instrumentation framework.

## Acknowledgments

`myproject.cpp` builds on the routine-counting example that ships with Intel Pin (Copyright Intel Corporation, MIT license). The AeroDrome algorithm is due to Mathur and Viswanathan.
