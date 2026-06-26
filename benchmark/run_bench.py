#!/usr/bin/env python3
"""
Lovelang Language Speed Benchmark
Task A (compiled): sum 0..20,000,000 via while loop
Task B (interpreter): sum 0..500,000 (extrapolated to 20M for fair display)
"""
import subprocess, time, os

BOLD  = "\033[1m"
GREEN = "\033[92m"
CYAN  = "\033[96m"
YEL   = "\033[93m"
RED   = "\033[91m"
DIM   = "\033[2m"
RST   = "\033[0m"
PINK  = "\033[95m"
MAG   = "\033[35m"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BM   = os.path.join(ROOT, "benchmark")

RUNS = 5   # runs per language, take median

# ─── Runner ────────────────────────────────────────────────────────────────────
def run(cmd, runs=RUNS):
    times = []
    for _ in range(runs):
        t0 = time.perf_counter()
        r  = subprocess.run(cmd, shell=True, capture_output=True, timeout=120)
        t1 = time.perf_counter()
        if r.returncode != 0:
            err = (r.stderr or r.stdout).decode().strip()
            return None, err[:80]
        times.append((t1 - t0) * 1000)
    times.sort()
    return times[len(times)//2], None

# ─── Bar chart ──────────────────────────────────────────────────────────────────
def bar(ms, max_ms, width=32):
    if ms is None:
        return f"{RED}{'─'*width}{RST}"
    filled = max(1, int(round((ms / max_ms) * width)))
    empty  = width - filled
    return "█" * filled + f"{DIM}" + "░" * empty + f"{RST}"

def speed_color(ms, fastest_ms):
    ratio = ms / fastest_ms
    if ratio < 1.5:  return GREEN
    if ratio < 5:    return CYAN
    if ratio < 20:   return YEL
    return RED

# ─── Entries ───────────────────────────────────────────────────────────────────
# Each entry: (label, cmd, note, group, N_scale)
# N_scale: multiply measured ms by this to normalise to 20M iterations
N20M  = 1.0          # already runs 20M
N500K = 40.0         # 500K × 40 = 20M  (for extrapolation)

entries = [
    # ── Lovelang ──────────────────────────────────────────────────────
    ("Lovelang  (native compiled)",
     f"{BM}/bench_love",
     "💚 direct ARM64 Mach-O",  "love", N20M),

    ("Lovelang  (interpreted)",
     f"{ROOT}/lovelang {BM}/bench_interp.love",
     "💚 tree-walk interpreter (×40 extrapolated)", "love", N500K),

    # ── Compiled (systems) ────────────────────────────────────────────
    ("C         (clang -O2)",
     f"{BM}/bench_c",
     "⚡ optimised native",      "compiled", N20M),

    ("C         (clang -O0)",
     f"{BM}/bench_c_noopt",
     "no optimisation",          "compiled", N20M),

    ("Rust      (-O release)",
     f"{BM}/bench_rust",
     "⚡ optimised native",      "compiled", N20M),

    ("Swift     (-O)",
     f"{BM}/bench_swift",
     "⚡ optimised native",      "compiled", N20M),

    # ── JIT ───────────────────────────────────────────────────────────
    ("Node.js   (V8 JIT)",
     f"node {BM}/bench.js",
     "🔥 JIT-compiled JS",       "jit", N20M),

    # ── Scripted ──────────────────────────────────────────────────────
    ("Perl",
     f"perl {BM}/bench.pl",
     "scripted",                 "scripted", N20M),

    ("Ruby",
     f"ruby {BM}/bench.rb",
     "scripted",                 "scripted", N20M),

    ("Python 3",
     f"python3 {BM}/bench.py",
     "scripted",                 "scripted", N20M),
]

# ─── Run all ───────────────────────────────────────────────────────────────────
print()
print(f"{BOLD}{'═'*72}{RST}")
print(f"{BOLD}  🔥  LOVELANG SPEED BENCHMARK vs The World{RST}")
print(f"{BOLD}  Algorithm : while-loop sum of integers  0 → 20,000,000{RST}")
print(f"{BOLD}  Host      : Apple Silicon ARM64 · macOS{RST}")
print(f"{BOLD}  Runs      : {RUNS} per language · median time reported{RST}")
print(f"{BOLD}{'═'*72}{RST}")
print()
print(f"  {DIM}Measuring {len(entries)} languages…{RST}")
print()

results = []
for label, cmd, note, group, scale in entries:
    sys_ms, err = run(cmd)
    norm_ms = (sys_ms * scale) if sys_ms is not None else None
    tag = f"  {DIM}({note}){RST}" if note else ""
    tick = f"{GREEN}✓{RST}" if sys_ms is not None else f"{RED}✗{RST}"
    disp = f"{sys_ms:8.1f} ms" if sys_ms is not None else f"  {'FAILED':>8}"
    print(f"  {tick} {label:<34} {disp}{tag}")
    results.append({
        "label": label, "group": group, "note": note,
        "raw_ms": sys_ms, "ms": norm_ms, "err": err, "scale": scale
    })

# ─── Sort ──────────────────────────────────────────────────────────────────────
ok   = sorted([r for r in results if r["ms"] is not None], key=lambda r: r["ms"])
fail = [r for r in results if r["ms"] is None]

max_ms    = max(r["ms"] for r in ok) if ok else 1
fastest   = ok[0]["ms"] if ok else 1
love_nat  = next((r for r in ok if "native" in r["label"].lower() and r["group"]=="love"), None)

# ─── Results table ─────────────────────────────────────────────────────────────
print()
print(f"{BOLD}{'═'*72}{RST}")
print(f"{BOLD}  📊  RESULTS  (fastest → slowest, lower = better){RST}")
print(f"{BOLD}{'═'*72}{RST}")
print()
print(f"  {'#':<3} {'Language':<34} {'Time (ms)':>10}  {'vs #1':>8}  Bar")
print(f"  {'─'*3} {'─'*34} {'─'*10}  {'─'*8}  {'─'*32}")

for i, r in enumerate(ok):
    ms    = r["ms"]
    mult  = ms / fastest
    col   = speed_color(ms, fastest)
    raw   = r["raw_ms"]
    sc    = r["scale"]
    # show actual measured time; if scaled, show extrapolated too
    if sc != 1.0:
        time_str = f"{raw:7.1f}ms*"
    else:
        time_str = f"{ms:8.1f}ms"

    chart = bar(ms, max_ms)
    star  = ""
    if r["group"] == "love" and "native" in r["label"].lower():
        star = f"  {PINK}{BOLD}← Lovelang native{RST}"
    elif r["group"] == "love":
        star = f"  {MAG}← Lovelang interp{RST}"

    is_love = r["group"] == "love"
    b = BOLD if is_love else ""
    e = RST  if is_love else ""

    print(f"  {col}{i+1:<3}{RST} {b}{r['label']:<34}{e} {col}{time_str:>10}{RST}  {mult:>7.1f}×  {chart}{star}")

for r in fail:
    print(f"  {RED}ERR{RST} {r['label']:<34} {'FAILED':>10}  {'─':>8}  {RED}{(r['err'] or '')[:32]}{RST}")

print(f"\n  {DIM}* Interpreted benchmark runs 500K iters (×40 extrapolated for fair comparison){RST}")

# ─── Key comparisons ───────────────────────────────────────────────────────────
print()
print(f"{BOLD}{'═'*72}{RST}")
print(f"{BOLD}  📈  KEY COMPARISONS (Lovelang native compiled){RST}")
print(f"{BOLD}{'═'*72}{RST}")
print()

def compare(a, b_label_fragment):
    if a is None: return
    b = next((r for r in ok if b_label_fragment.lower() in r["label"].lower()), None)
    if b is None: return
    if a["ms"] < b["ms"]:
        ratio = b["ms"] / a["ms"]
        col, word = GREEN, "faster"
    else:
        ratio = a["ms"] / b["ms"]
        col, word = YEL, "slower"
    print(f"  {col}▶{RST}  vs  {b['label']:<32}  {col}{BOLD}{ratio:5.1f}× {word}{RST}")

compare(love_nat, "clang -O2")
compare(love_nat, "clang -O0")
compare(love_nat, "rust")
compare(love_nat, "swift")
compare(love_nat, "node")
compare(love_nat, "ruby")
compare(love_nat, "perl")
compare(love_nat, "python")

# compiled vs interpreted
love_interp = next((r for r in ok if "interpreted" in r["label"].lower()), None)
if love_nat and love_interp:
    ratio = love_interp["ms"] / love_nat["ms"]
    print()
    print(f"  {GREEN}▶{RST}  Compiled Lovelang vs Interpreted Lovelang  {GREEN}{BOLD}{ratio:.0f}× faster{RST}")

print()
print(f"  {DIM}Note: All compiled languages include process startup time in measurement.{RST}")
print(f"  {DIM}      Lovelang native = pure ARM64 Mach-O, no C compiler, no runtime.{RST}")
print()
print(f"{BOLD}{'═'*72}{RST}")
print()
