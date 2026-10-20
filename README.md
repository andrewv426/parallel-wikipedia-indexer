# Parallel Wikipedia Indexer

A C++ systems project that streams large Wikipedia text files into case-insensitive word counts using Win32 threads, a bounded work queue, and custom hash tables. It explores how tokenization, memory layout, and synchronization affect throughput as the workload grows from megabytes to tens of gigabytes.

Recorded measurements reached **2,003 MiB/s with 16 workers**, a **5.62× speedup** over one worker on an 8.64 GB input. A separate 30.24 GB run processed 4.53 billion candidate words and counted 8.86 million unique eligible words.

## Architecture

```mermaid
flowchart LR
    Input["Wikipedia text file"] --> Disk["Disk reader"]
    Disk --> Queue["Bounded buffer queue"]
    Queue --> Workers["N search workers"]
    Workers --> Tables["N private hash tables"]
    Tables --> Merge["Merge after workers finish"]
    Merge --> Output["Word counts and timing"]
    Workers -. progress .-> Stats["Statistics thread"]
    Tables -. lookup depth .-> Stats
```

| Component | Design and purpose |
| --- | --- |
| Disk reader | Reads sequentially and carries a small shadow region between buffers so workers can handle words at chunk boundaries. |
| Work queue | A 24-slot ring buffer uses semaphores and a critical section to coordinate the producer and consumers with bounded buffering. |
| Search workers | Each worker tokenizes its buffers and updates a private hash table, avoiding a shared table lock during counting. |
| Hash tables | Chained hash bins point into packed records. Each table reserves 2 GiB of virtual address space and commits memory in 1 MiB increments as needed. |
| Final merge | Once workers finish, their tables are folded into one table to obtain the final unique-word count. |
| Statistics thread | Reports progress, throughput, word counts, hash lookup depth, CPU utilization, and working-set memory every two seconds. |

Tokenization uses byte lookup tables for ASCII letter detection, lowercase conversion, delimiters, and hashing. Eligible words contain 3–31 letters and must meet the delimiter rules. The program counts words in the supplied text; it does not strip Wikipedia markup or perform Unicode language processing.

## Build and run

### Requirements

- Windows x64.
- Visual Studio 2022 with **Desktop development with C++**, the MSVC v143 toolset, and a Windows SDK.
- Enough memory for the per-worker tables; memory use grows with the worker count and vocabulary size.

From a **Developer PowerShell for VS 2022**:

```powershell
git clone https://github.com/andrewv426/parallel-wikipedia-indexer.git
cd parallel-wikipedia-indexer
msbuild wordIndexer.sln /m /p:Configuration=Release /p:Platform=x64
.\x64\Release\wordIndexer.exe 20 .\enwiki-all.txt
```

Alternatively, open `wordIndexer.sln`, select **Release | x64**, and build in Visual Studio. Supply your own plain-text input file; the large Wikipedia datasets are excluded from Git.

```text
wordIndexer <log2_buf_size> <input_file>
```

The first argument sets the buffer size to `2^log2_buf_size` bytes: `20` means 1 MiB. Quote input paths that contain spaces. The current executable chooses the worker count from the logical processor count, capped at 64, and uses `2^20` hash bins per worker. Hash size, queue capacity, word-length limits, and the statistics interval are defined in [Config.h](wordIndexer/Config.h).

The program prints progress and timing to standard error, and final `Unique`, `Invalid`, and `Total` counts to standard output. Debug builds also run lookup-table assertions at startup.

## Measured performance

The saved runs used Windows 11, Visual Studio 2022 / MSVC x64 Release, 1 MiB read buffers, and warm input caches. Dataset runs used 16 workers and `2^20` hash bins per worker. Timing covers thread startup, indexing, and the final merge. These are individual recorded runs; the hardware model and repeated-run variance were not captured.

Throughput below is **MiB/s** (`bytes / 1,048,576 / seconds`), matching the calculation in the source. Input sizes use decimal MB and GB.

| Input | Size | Time (s) | Throughput (MiB/s) | Unique words |
| --- | ---: | ---: | ---: | ---: |
| Tiny | 52.16 MB | 0.0765 | 650 | 183,733 |
| Small | 536.87 MB | 0.5139 | 996 | 757,198 |
| Medium | 8.64 GB | 4.1547 | 1,982 | 4,871,639 |
| Large | 30.24 GB | 27.1293 | 1,063 | 8,861,171 |

### Thread scaling

On the medium input, throughput rose from 357 MiB/s with one worker to 2,003 MiB/s with 16 workers. Scaling is sublinear: the pipeline still shares the reader, queue, memory bandwidth, and final merge.

![Measured throughput for 1, 2, 4, 8, 12, and 16 workers](benchmarks/figures/thread_scaling.png)

| Workers | 1 | 2 | 4 | 8 | 12 | 16 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| MiB/s | 357 | 525 | 923 | 1,476 | 1,811 | 2,003 |
| Speedup | 1.00× | 1.47× | 2.59× | 4.14× | 5.08× | 5.62× |

### Hash table sizing

With 16 workers, increasing the bin count from `2^16` to `2^20` raised throughput from 1,289 to 2,015 MiB/s. The `2^22` run reached 2,096 MiB/s, about 4% higher, while using four times the bin-array memory per worker. The default `2^20` setting uses 4 MiB per bin array, compared with 16 MiB at `2^22`; record storage is additional.

![Measured throughput across four hash table sizes](benchmarks/figures/hash_sizing.png)

## What the word counts show

On the large input, the vocabulary peaks at **8-character words**, while **3-character words** account for the most occurrences. The distinction matters: many distinct longer words coexist with a small set of very frequent short words.

![Unique-word and occurrence distributions by word length](benchmarks/figures/word_lengths.png)

The top 10,000 words follow a strong rank-frequency relationship. A least-squares fit in log space gives `frequency ∝ rank^-1.203`, with `R² = 0.9870`. The fit describes the overall distribution; the most frequent words visibly depart from the fitted line.

![Measured word frequencies and fitted power law on logarithmic axes](benchmarks/figures/word_frequencies.png)

## Benchmark data and figures

The figures above are generated from the original measured CSV snapshots in [benchmarks/data](benchmarks/data). They are not new benchmark runs. The thread/hash sweeps and word-distribution exports were collected with a parameterized benchmark build; the current CLI accepts only the buffer size and input path.

To regenerate the figures with Python, NumPy, and Matplotlib:

```powershell
python -m pip install numpy matplotlib
python benchmarks/plot_results.py
```

[plot_results.py](benchmarks/plot_results.py) reads the saved measurements and writes the four PNG figures. The summary CSV retains the exact byte counts, timings, thread counts, and hash settings behind the rounded tables.

## Source map

| Path | Contents |
| --- | --- |
| [wordIndexer/](wordIndexer) | Tokenizer, hash table, disk reader, queue, workers, statistics, and CLI. |
| [mt/](mt) | Mersenne Twister utilities used to seed the hashing lookup table. |
| [benchmarks/](benchmarks) | Measured CSV snapshots, plotting script, and README figures. |
| `keywords-A.txt` through `keywords-D.txt` | Keyword fixtures for experimentation. |
