#!/usr/bin/env python3
"""Run this compiler N times over a (large) source and aggregate --time-trace output.

A single compile of a small file is mostly noise: process start, page faults, the
allocator warming up, and every scope below --time-trace-granularity dropped on the
floor. Two things fix that, and this script does both:

  * a source big enough that each phase runs for hundreds of milliseconds
    (Benchmarks/gen_stress.py), and
  * a warm-up run plus N measured runs whose per-phase durations are SUMMED, with the
    run-to-run spread reported next to every number -- so "is this difference real?"
    is answered by the output, not by faith.

  python3 Benchmarks/profile.py Benchmarks/stress.lis -n 5
  python3 Benchmarks/profile.py Benchmarks/stress.lis -n 5 -o 0      # front end only
  python3 Benchmarks/profile.py Benchmarks/stress.lis -n 5 --merge merged.json

--merge writes one trace containing every run laid out end to end, which is what you
want for the flame graph: same shape, N times the samples.
"""

import argparse
import json
import statistics
import subprocess
import sys
import time
from pathlib import Path


def find_lisc(explicit):
    if explicit:
        return explicit
    for candidate in ("Build/Binaries/lisc.exe", "Build/Binaries/lisc"):
        if Path(candidate).exists():
            return candidate
    sys.exit("cannot find lisc; pass --lisc <path>")


def run_once(lisc, source, obj, trace, opt, granularity):
    cmd = [lisc, source, "-o", str(opt), "--out", str(obj),
           "--time-trace", str(trace), "--time-trace-granularity", str(granularity)]
    start = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc, (time.perf_counter() - start) * 1000.0, cmd


def scopes_of(trace_path):
    data = json.loads(Path(trace_path).read_text(encoding="utf-8"))
    events = data.get("traceEvents", data if isinstance(data, list) else [])
    # LLVM also emits "Total <name>" aggregate entries; counting them would double
    # every phase.
    return [e for e in events if e.get("ph") == "X" and e.get("dur") and not str(e.get("name", "")).startswith("Total ")]


def contains(outer, inner):
    return (outer is not inner
            and inner["ts"] >= outer["ts"]
            and inner["ts"] + inner["dur"] <= outer["ts"] + outer["dur"]
            and (outer["dur"] > inner["dur"] or (outer["dur"] == inner["dur"] and outer["ts"] < inner["ts"])))


def top_level(scopes):
    """The scopes nothing else contains: our passes."""
    return [s for s in scopes if not any(contains(o, s) for o in scopes)]


def self_time(scopes):
    """Duration minus the duration of every DIRECT child scope."""
    parent = {}
    for s in scopes:
        enclosing = [o for o in scopes if contains(o, s)]
        if enclosing:
            parent[id(s)] = min(enclosing, key=lambda o: o["dur"])
    own = {id(s): s["dur"] for s in scopes}
    for s in scopes:
        p = parent.get(id(s))
        if p is not None:
            own[id(p)] -= s["dur"]
    return {s["name"]: own[id(s)] for s in scopes}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source")
    ap.add_argument("-n", type=int, default=5, help="measured runs (a warm-up run is always done first)")
    ap.add_argument("-o", dest="opt", default="2")
    ap.add_argument("--lisc", default=None)
    ap.add_argument("--granularity", type=int, default=0, help="microseconds; 0 keeps every scope")
    ap.add_argument("--merge", default=None, help="write all runs into one trace file")
    ap.add_argument("--top", type=int, default=12, help="how many self-time hotspots to list")
    args = ap.parse_args()

    lisc = find_lisc(args.lisc)
    work = Path("Build/Intermediate/profile")
    work.mkdir(parents=True, exist_ok=True)
    obj = work / "stress.o"

    print(f"lisc: {lisc}")
    print(f"source: {args.source}  -o {args.opt}  runs: 1 warm-up + {args.n}")

    proc, wall, cmd = run_once(lisc, args.source, obj, work / "warmup.json", args.opt, args.granularity)
    if proc.returncode != 0:
        print("warm-up FAILED:\n" + proc.stdout[-2000:] + proc.stderr[-2000:])
        sys.exit(1)
    print(f"warm-up: {wall:.0f} ms wall")

    per_run_wall = []
    per_pass = {}       # name -> [ms per run]
    self_by_run = []
    merged = []
    offset = 0
    for i in range(args.n):
        trace = work / f"run{i}.json"
        proc, wall, cmd = run_once(lisc, args.source, obj, trace, args.opt, args.granularity)
        if proc.returncode != 0:
            print(f"run {i} FAILED:\n" + proc.stdout[-2000:])
            sys.exit(1)
        per_run_wall.append(wall)
        scopes = scopes_of(trace)
        for s in top_level(scopes):
            per_pass.setdefault(s["name"], []).append(s["dur"] / 1000.0)
        self_by_run.append(self_time(scopes))
        if args.merge:
            run_start = min(s["ts"] for s in scopes)
            for s in scopes:
                e = dict(s)
                e["ts"] = s["ts"] - run_start + offset
                merged.append(e)
            offset += max(s["ts"] for s in scopes) - run_start + 1000

    total_per_run = [sum(v[i] for v in per_pass.values()) for i in range(args.n)]
    print(f"\ncompile wall: mean {statistics.mean(per_run_wall):.0f} ms "
          f"(min {min(per_run_wall):.0f}, max {max(per_run_wall):.0f}, "
          f"spread {(max(per_run_wall) - min(per_run_wall)) / statistics.mean(per_run_wall) * 100:.1f}% of mean)")
    print(f"summed pass time: mean {statistics.mean(total_per_run):.0f} ms\n")

    print(f"{'pass':<24}{'mean ms':>9}{'share':>8}{'min':>9}{'max':>9}{'spread':>8}")
    grand = sum(statistics.mean(v) for v in per_pass.values())
    for name, runs in sorted(per_pass.items(), key=lambda kv: -statistics.mean(kv[1])):
        m = statistics.mean(runs)
        spread = (max(runs) - min(runs)) / m * 100 if m else 0.0
        print(f"{name:<24}{m:>9.1f}{m / grand * 100:>7.1f}%{min(runs):>9.1f}{max(runs):>9.1f}{spread:>7.1f}%")

    # Hotspots by SELF time, summed over runs: this is what the widest blocks in the
    # flame graph are, with our own scopes subtracted out.
    agg = {}
    for run in self_by_run:
        for name, dur in run.items():
            agg[name] = agg.get(name, 0.0) + dur / 1000.0
    print(f"\ntop {args.top} by SELF time (summed over {args.n} runs):")
    for name, dur in sorted(agg.items(), key=lambda kv: -kv[1])[:args.top]:
        print(f"  {name:<46}{dur:>9.1f} ms")

    if args.merge:
        Path(args.merge).write_text(json.dumps({"traceEvents": merged}), encoding="utf-8")
        print(f"\nmerged {len(merged)} scopes from {args.n} runs -> {args.merge}")


if __name__ == "__main__":
    main()
