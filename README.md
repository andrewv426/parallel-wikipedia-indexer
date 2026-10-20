# Parallel Wikipedia Indexer

A multithreaded C++ word indexer for large Wikipedia text files. A disk reader feeds a bounded queue, worker threads tokenize buffers into per-thread hash tables, and the main thread merges their word counts. A statistics thread reports progress during processing.

The tokenizer folds ASCII letters to lowercase and counts eligible words of 3–31 characters using explicit delimiter rules. The program prints unique, invalid, and total word counts along with execution time and throughput.

## Build

Use Windows with Visual Studio 2022, the Desktop development with C++ workload, and a Windows SDK. Open `wordIndexer.sln`, select **Release | x64**, and build. Alternatively, run this from a Visual Studio Developer PowerShell at the repository root:

```powershell
msbuild wordIndexer.sln /p:Configuration=Release /p:Platform=x64
```

The implementation uses Win32 threads, synchronization, file I/O, and virtual memory APIs. It requires Windows to build and run.

## Run

```powershell
.\x64\Release\wordIndexer.exe <log2_buf_size> <input_file>
```

For example, process an input file with a 1 MiB buffer:

```powershell
.\x64\Release\wordIndexer.exe 20 .\enwiki-all.txt
```

The buffer size is `2^log2_buf_size` bytes. The program selects its worker count from the number of logical processors, capped at the Windows wait-object limit. Large Wikipedia input files are excluded from Git and must be supplied separately.

## Project files

- `wordIndexer/`: tokenizer, hash table, buffer queue, disk reader, workers, and statistics thread.
- `mt/`: Mersenne Twister utilities used to initialize hashing lookup tables.
- `keywords-A.txt` through `keywords-D.txt`: keyword lists for testing.

Performance depends on the input, processor, storage, and build configuration.
