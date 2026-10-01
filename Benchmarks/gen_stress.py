#!/usr/bin/env python3
"""Generate a large, valid Lis program for profiling the COMPILER (not the program).

Why generated: the point is VOLUME. A phase that runs 200 ms instead of 20 ms has an
order of magnitude less relative noise, and scopes too short to matter stop being
dropped by --time-trace-granularity. The knobs map onto different parts of the compiler:

  --types     structs with fields, &mut methods, loops over a Vec field  -> sema + checker
  --funcs     free functions: loops, branches, array indexing           -> parser, MIR, codegen
  --enums     payload enums + exhaustive matches                        -> sema, MIR switch
  --generics  generic fns instantiated at several types                 -> MIRMonomorphization
  --containers  Vec/String inside the structs and in main               -> borrow checker + stdlib

The emitted program COMPILES and RUNS (exit != 0 means the stress program itself broke,
which is also a smoke test). Measure the COMPILE:

  python3 Benchmarks/gen_stress.py --out Benchmarks/stress.lis
  python3 Benchmarks/profile.py Benchmarks/stress.lis -n 3
"""

import argparse


def gen_structs(n_types: int, containers: bool) -> str:
    out = []
    for i in range(n_types):
        out.append(f"struct T{i:03d}")
        out.append("{")
        out.append("    pub a: i32,")
        out.append("    pub b: i32,")
        out.append("    pub c: f64,")
        if containers:
            out.append("    pub v: Vec<i32>,")
        out.append("}")
        out.append("")
        out.append(f"impl T{i:03d}")
        out.append("{")
        out.append("    fn bump(self: &mut Self, k: i32) -> i32")
        out.append("    {")
        out.append("        self.a = self.a + k;")
        out.append("        self.b = self.b - k;")
        out.append("        ret self.a + self.b;")
        out.append("    }")
        out.append("")
        out.append("    fn total(self: &Self) -> i32")
        out.append("    {")
        out.append("        let mut s = 0;")
        if containers:
            out.append("        let mut i = 0;")
            out.append("        while i < self.v.len()")
            out.append("        {")
            out.append("            s = s + self.v[i];")
            out.append("            i = i + 1;")
            out.append("        }")
        out.append("        ret s + self.a - self.b;")
        out.append("    }")
        out.append("}")
        out.append("")
    return "\n".join(out)


def gen_funcs(n_funcs: int, n_types: int, containers: bool) -> str:
    out = []
    for i in range(n_funcs):
        t = i % n_types
        out.append(f"fn f{i:04d}(x: i32) -> i32")
        out.append("{")
        out.append("    let mut acc = x;")
        out.append("    let mut i = 0;")
        lit = ", ".join(str((i + k) % 29) for k in range(8))
        out.append(f"    let arr: [i32; 8] = [{lit}];")
        out.append("    while i < 8")
        out.append("    {")
        out.append("        if arr[i] > 0")
        out.append("        {")
        out.append("            acc = acc + arr[i];")
        out.append("        }")
        out.append("        else")
        out.append("        {")
        out.append("            acc = acc - arr[i];")
        out.append("        }")
        out.append("        i = i + 1;")
        out.append("    }")
        fields = f"a: acc, b: {i % 17}, c: 1.5"
        if containers:
            fields += ", v: Vec<i32>::new()"
        out.append(f"    let mut t{t:03d} = T{t:03d} {{ {fields} }};")
        out.append(f"    let k = t{t:03d}.bump(3);")
        out.append(f"    ret t{t:03d}.total() + k;")
        out.append("}")
        out.append("")
    return "\n".join(out)


def gen_enums(n_enums: int) -> str:
    out = []
    for i in range(n_enums):
        out.append(f"enum E{i:03d}")
        out.append("{")
        out.append("    A(i32),")
        out.append("    B(f64),")
        out.append("    C,")
        out.append("}")
        out.append("")
        out.append(f"fn match{i:03d}(e: E{i:03d}) -> i32")
        out.append("{")
        out.append("    match e")
        out.append("    {")
        # Patterns are BARE variant names in this language (`Some(v)`, not
        # `Option::Some(v)`); construction uses the qualified form. Those two are
        # different syntaxes and the parser rejects the qualified one in a pattern.
        out.append("        A(n) => n,")
        out.append("        B(f) => f as i32,")
        out.append("        C => 0,")
        out.append("    }")
        out.append("}")
        out.append("")
    return "\n".join(out)


def gen_generics(n_generics: int) -> str:
    out = [
        "fn idg<T>(x: T) -> T",
        "{",
        "    ret x;",
        "}",
        "",
        "fn pickg<T>(a: T, b: T, take: bool) -> T",
        "{",
        "    if take",
        "    {",
        "        ret a;",
        "    }",
        "    ret b;",
        "}",
        "",
    ]
    for i in range(n_generics):
        out.append(f"fn useg{i:03d}(x: i32) -> i32")
        out.append("{")
        out.append("    let a = idg(x);")
        out.append("    let b = idg(1.5);")
        out.append("    let c = pickg(a, x + 1, true);")
        out.append("    let d = pickg(2.5, b, false);")
        out.append("    ret c + (d as i32);")
        out.append("}")
        out.append("")
    return "\n".join(out)


def gen_main(n_funcs: int, n_enums: int, n_generics: int, step: int) -> str:
    out = ["fn main() -> i32", "{", "    let mut acc = 0;"]
    for i in range(0, n_funcs, step):
        out.append(f"    acc = acc ^ f{i:04d}(acc + {i % 23});")
    for i in range(n_enums):
        out.append(f"    acc = acc + match{i:03d}(E{i:03d}::A(acc));")
    if n_generics:
        stride = max(1, n_generics // 16)
        for i in range(0, n_generics, stride):
            out.append(f"    acc = acc + useg{i:03d}(acc % 97);")
    out += [
        "    let mut v: Vec<i32> = Vec<i32>::new();",
        "    let mut j = 0;",
        "    while j < 64",
        "    {",
        "        v.push(acc + j);",
        "        j = j + 1;",
        "    }",
        "    let mut s: String = String::new();",
        "    s.push_char('x');",
        "    let sl = s.len();",
        "    // Consume everything into a global so nothing can be optimized away, and",
        "    // still exit 0: 'it compiled AND ran' is then a single check.",
        "    G_SUM = acc + v.len() + sl;",
        "    ret 0;",
        "}",
        "",
    ]
    return "\n".join(out)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="Benchmarks/stress.lis")
    ap.add_argument("--types", type=int, default=120)
    ap.add_argument("--funcs", type=int, default=400)
    ap.add_argument("--enums", type=int, default=40)
    ap.add_argument("--generics", type=int, default=60)
    ap.add_argument("--step", type=int, default=1, help="call only every Nth function from main")
    ap.add_argument("--no-containers", action="store_true", help="drop the Vec field and the container tail")
    args = ap.parse_args()

    containers = not args.no_containers
    text = "\n".join([
        "// GENERATED by Benchmarks/gen_stress.py -- do not edit by hand.",
        "//",
        "// A compile-time stress test: every phase of the pipeline gets enough work that a",
        "// 50us time-trace scope is a rounding error rather than the measurement.",
        "impt vec { Vec };",
        "impt string { String };",
        "",
        "// Where the generated work is consumed (a global is the language's shared",
        "// mutable state; globals take a literal initializer and no `mut`).",
        "let G_SUM: i32 = 0;",
        "",
    ])
    text += "\n" + gen_structs(args.types, containers)
    text += "\n" + gen_funcs(args.funcs, args.types, containers)
    text += "\n" + gen_enums(args.enums)
    text += "\n" + gen_generics(args.generics)
    text += "\n" + gen_main(args.funcs, args.enums, args.generics, args.step)

    with open(args.out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {args.out}: {len(text.splitlines())} lines, {len(text)} bytes")


if __name__ == "__main__":
    main()
