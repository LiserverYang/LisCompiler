# Benchmarks: profiling the compiler itself

These are **compile-time** benchmarks: the program below is not interesting to run, it
exists so that every phase of the pipeline has enough work for its time to be measured
instead of guessed.

## Quick start

```bash
# 1. generate a stress program (checked in as stress.lis; regenerate to rescale)
python3 Benchmarks/gen_stress.py --out Benchmarks/stress.lis

# 2. profile: 1 warm-up + N measured runs, per-phase table with the run-to-run spread
python3 Benchmarks/profile.py Benchmarks/stress.lis -n 5
python3 Benchmarks/profile.py Benchmarks/stress.lis -n 5 -o 0      # front end only
python3 Benchmarks/profile.py Benchmarks/stress.lis -n 5 --merge m.json

# 3. look at it: open m.json (or Build/Intermediate/profile/run0.json) in
#    chrome://tracing or https://ui.perfetto.dev  -- that IS the flame graph
```

`profile.py` finds `Build/Binaries/lisc.exe` (or `lisc`), calls it with
`--time-trace` at `--time-trace-granularity 0`, and reports per pass: mean, share,
min, max and the spread. The warm-up run exists so the first, cold run does not land in
the statistics.

## Why a generated stress program

Noise is relative: a phase that runs 850 ms has ~3% run-to-run spread, a phase that runs
8 ms has 30%, and a scope under the granularity is not even recorded. `--types/--funcs/
--enums/--generics/--containers` are knobs on *different* parts of the compiler:

| knob | what it stresses |
|---|---|
| `--types` | structs with fields, `&mut Self` methods, loops over a `Vec` field: sema + the borrow checker |
| `--funcs` | free functions with loops, branches and array indexing: parser, MIR, codegen |
| `--enums` | payload enums with exhaustive matches: sema, MIR switch lowering |
| `--generics` | generic functions instantiated at several types: monomorphization |
| `--no-containers` | drops the `Vec` fields, i.e. most of the checker's work |

The program imports `vec` and `string` on purpose: the analyzer and the checker then
also work through the standard library's generic tree.

## Reference numbers (this machine, Debug build, -o 2, 14k lines)

`compile wall: mean 3221 ms (spread 3.1%)`, summed pass time 2759 ms:

| pass | mean | share | spread |
|---|---|---|---|
| Emitter (LLVM's optimiser) | 1019.6 ms | 37.0% | 4.7% |
| **MIRBorrowCheck** | **853.0 ms** | **30.9%** | 2.6% |
| MIRBuilder | 308.9 ms | 11.2% | 0.9% |
| HIRSemanticAnalyzer | 195.4 ms | 7.1% | 7.7% |
| LLVMIRBuilder | 153.5 ms | 5.6% | 4.1% |
| Lexer | 72.8 ms | 2.6% | 0.4% |
| Parser | 63.8 ms | 2.3% | 3.7% |
| MIRMonomorphization | 53.7 ms | 1.9% | 7.1% |
| HIRBuilder | 36.6 ms | 1.3% | 1.8% |

Absolute times are machine- and build-specific; the shape is the point: **of our own
time the borrow checker is the largest single block** (it is a CFG data-flow pass over
every function, run before monomorphization), and the analyzer is next.

The `Emitter` block is LLVM's whole optimisation pipeline: our scope covers it, and LLVM
does not hand its own per-pass scopes to a library user by default. To open it up either
sample (`perf record -g --call-graph dwarf -- lisc Benchmarks/stress.lis`, see the
profiling notes in CLAUDE.md) or wire LLVM's `StandardInstrumentations` /
`TimePassesHandler` into `Emitter::runOptPipeline` for a per-pass text report.

## Sampling with perf (Linux/WSL): two 9p traps

`--time-trace` covers our own phases; to see inside LLVM (the whole `Emitter` block) you
need a sampler. `perf` works in WSL2 (kernel 6.18 here, `perf_event_paranoid=2`), but it
must not touch the Windows mount -- both failures below were measured, not guessed:

- **`--output` has to be on a native filesystem.** Writing `perf.data` into `/mnt/f` (a 9p
  drvfs mount) dies with `failed to write perf data, error: Bad address` and leaves a
  1120-byte stub -- **with or without `--call-graph`**, so it is the output path and not
  the unwinder. The same command with `-o /tmp/perf.data` records 12k samples fine.
- **Run the binary from a native path too.** perf loads symbols/dso from the executable;
  with the binary on `/mnt/f` the report comes back empty. `lisc` looks for `lstdlib`
  next to `argv[0]`, so copy that as well.

```bash
cp Build/Binaries/lisc /tmp/lisc && cp -r Build/Binaries/lstdlib /tmp/
cp Benchmarks/stress.lis /tmp/ && cd /tmp
perf record -o /tmp/p.data --all-user -g --call-graph dwarf -- /tmp/lisc /tmp/stress.lis -o 2 --out /tmp/s.o
git clone --depth 1 https://github.com/brendangregg/FlameGraph /tmp/FlameGraph   # once
perf script -i /tmp/p.data > p.script
perl /tmp/FlameGraph/stackcollapse-perf.pl p.script > p.folded
perl /tmp/FlameGraph/flamegraph.pl --width 1800 p.folded > compile.svg
```

- **`--all-user`** drops the kernel frames, which resolve to nothing in WSL
  (`ffffffffa4... [unknown]`) and only add width to the graph.
- **`--call-graph dwarf` is required to see inside `libLLVM`**: our build is Debug (frame
  pointers present), but the LLVM libraries are a release build without them, so
  `--call-graph fp` unwinds garbage the moment the stack enters LLVM.
- `Failed to open /proc/schedstat` is a harmless WSL warning.
- First result from the 3 s stress compile (12257 samples, user space only): the leaf
  frames are dominated by `std::vector` accessors (`size`, `operator[]`, `_M_data`,
  `~vector` -- at `-O0` every `locals[i]` is a real call), then `llvm::SROAPass::run` /
  `llvm::PromoteMemToReg` and our own `computeLiveness`. That is consistent with the
  `--time-trace` split above (Emitter 37%, MIRBorrowCheck 31%) and is the first thing to
  look at if compile time becomes a problem.

## Is it still valid?

`stress.lis` is a normal program: it must compile, link and exit 0
(`lisc Benchmarks/stress.lis -o 2 --out stress.o && g++ -o stress.exe stress.o && ./stress.exe`).
It is deliberately NOT part of the gtest suite -- a 3 s compile would dominate it --
so re-running the commands above after a language change is how it stays honest.
