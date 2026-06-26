<div align="center">

# ❤️ Lovelang

**A programming language that feels like a love letter — write code in romantic, emotional, conversational language, in English *and* Hindi/Urdu.**

[![License: MIT](https://img.shields.io/badge/License-MIT-pink.svg)](LICENSE)
[![npm](https://img.shields.io/npm/v/lovelang-cli.svg?color=hotpink)](https://www.npmjs.com/package/lovelang-cli)
[![Build](https://img.shields.io/badge/build-passing-brightgreen)](https://github.com/PATEL-KRISH-0/lovelang)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows-blue)](https://github.com/PATEL-KRISH-0/lovelang)

> 📺 YouTube: [github.com/PATEL-KRISH-0](https://www.github.com/PATEL-KRISH-0)  
> 🐙 GitHub: [github.com/PATEL-KRISH-0/lovelang](https://github.com/PATEL-KRISH-0/lovelang)

</div>

---

## What is Lovelang?

Lovelang is a full programming language written in pure C. It lets you express code using romantic, poetic, emotional phrasing — either in English or Hindi/Urdu, or a natural mix of both.

It ships with:

- **A tree-walk interpreter** — run `.love` files instantly
- **A native compiler** — compile directly to ARM64 or x86_64 machine code with zero external tools (no GCC, no LLVM, no nothing)
- **Three output tone modes** — romantic, shayari, toxic
- **A VS Code extension** — syntax highlighting, snippets, hover docs, run buttons
- **An npm CLI wrapper** — install globally and run from anywhere

---

## Install

```bash
npm install -g lovelang-cli
lovelang examples/01-romantic-hello.love
```

Or build from source in one step:

```bash
git clone https://github.com/PATEL-KRISH-0/lovelang
cd lovelang
make
./lovelang examples/01-romantic-hello.love
```

---

## Your First Program

```love
yaad name hai "jaan"

baby bolo naa "hello, " milan name

agar name barabar hai "jaan" toh
  bolo "welcome back, my love"
warna
  bolo "arey, tum kaun ho?"
bas itna hi

love you baby byeee
```

```
hello, jaan
welcome back, my love
```

---

## Speed Benchmark 🔥

> **Algorithm:** while-loop sum of integers 0 → 20,000,000  
> **Host:** Apple Silicon ARM64 · macOS  
> **Runs:** 5 per language, median time reported

| # | Language | Time | vs Fastest |
|---|---|---|---|
| 1 | Rust (-O release) | 9.2 ms | 1.0× |
| 2 | C (clang -O2) | 9.3 ms | 1.0× |
| 3 | Swift (-O) | 14.1 ms | 1.5× |
| 4 | C (clang -O0) | 15.7 ms | 1.7× |
| **5** | **Lovelang (native compiled)** | **41.7 ms** | **4.5×** |
| 6 | Node.js (V8 JIT) | 146.1 ms | 15.9× |
| 7 | Perl | 378.6 ms | 41.1× |
| 8 | Ruby | 418.6 ms | 45.5× |
| 9 | Python 3 | 1388.0 ms | 150.8× |
| 10 | Lovelang (interpreted) | ~1528 ms* | 166× |

> *Interpreter measured at 500K iterations × 40 extrapolated.  
> Lovelang native = pure ARM64 Mach-O, zero external compiler, zero runtime dependency.

**Key comparisons — Lovelang native compiled:**

- **3.5× faster** than Node.js / V8
- **9.1× faster** than Perl
- **10.0× faster** than Ruby
- **33.3× faster** than Python 3
- **37× faster** than its own interpreter
- Only 4.5× behind optimised C/Rust (with no optimizer passes of its own)

Run the benchmark yourself:

```bash
python3 benchmark/run_bench.py
```

---

## Run Modes

```bash
# Interpret (default)
lovelang file.love

# Native compile → binary (no external compiler needed)
lovelang file.love --native --out ./my_program
./my_program

# Cross-compile to x86_64 Linux from ARM64 Mac
LOVELANG_TARGET=linux lovelang file.love --native --out ./app_linux

# Debug / inspection
lovelang file.love --tokens           # print token stream
lovelang file.love --debug-love       # trace variable writes at runtime
lovelang file.love --mode shayari     # change output tone
```

| Target | Architecture | Format |
|---|---|---|
| `macos` | ARM64 | Mach-O 64-bit |
| `linux` | ARM64 + x86_64 | ELF 64-bit |
| `windows` | ARM64 + x86_64 | PE/COFF 64-bit |

---

## Syntax Styles

Lovelang supports three styles that can be freely mixed in the same file.

### Core style (brace-based)

```love
yaad count = 0
agar (count == 0) ye_karo {
  bolo "zero"
} vo_karo {
  bolo "not zero"
}
```

### Natural style (phrase-based, toh/bas itna hi)

```love
yaad count hai 0
agar count barabar hai 0 toh
  bolo "zero"
warna
  bolo "not zero"
bas itna hi
```

### One-liner style

```love
sun na agar count bada hai 0 toh bolo "positive" warna bolo "non-positive"
```

### Can Code Be Written in a Single Line?

**Yes! You can write multiple statements on a single line, but only in the Core Style (brace-based syntax).**

* **Core Style (Brace-based):** The parser reads tokens sequentially and ignores newlines. Blocks are defined using curly braces `{}` and statements are separated by optional semicolons `;`. Thus, code can be completely minified onto a single line.
  *Example:*
  `yaad count = 0; agar (count == 0) ye_karo { bolo "zero"; } vo_karo { bolo "not zero"; } love_byeee();`
* **Natural Style (Phrase-based):** Cannot be written on a single line. The preprocessor translates phrase-based structures (`agar ... toh ... warna ... bas itna hi`) line-by-line using start-of-line and end-of-line checks. Putting them all on a single line will bypass these checks and cause a parsing error.

**What is it useful for?**
1. **Minification:** Reducing source code file sizes for faster transfers or compact storage.
2. **Code Generation:** Writing scripts or compilers that programmatically output Lovelang code without tracking line breaks.
3. **Shell One-Liners:** Easily creating and executing quick snippets directly from terminal commands:
   ```bash
   echo 'yaad x = 10; yaad y = 20; bolo x milan y; love_byeee();' > test.love && lovelang test.love
   ```

---

## Keywords Reference

Below is the standard keyword reference mapping Lovelang constructs to their equivalents in mainstream programming languages like C, JavaScript, and Python.

### Declarations

| Keyword | Meaning | C/JS/Python Equivalent |
|---|---|---|
| `yaad` / `yaad_karo` | Declare mutable variable | `let` / `var` / variable assignment |
| `vada` / `baby yaad rakho` | Declare immutable constant | `const` / read-only constant |

```love
yaad score = 0
yaad_karo level hai 1
vada max_lives = 3
baby yaad rakho color hai "red"
```

---

### Output

| Keyword | Meaning | C/JS/Python Equivalent |
|---|---|---|
| `bolo` | Print value and newline | `printf` / `console.log` / `print` |
| `typing` / `typing...` | Typing indicator animation | Animation sleep + flush stdout |
| `baby bolo na` / `baby bolo naa` | Alias for `bolo` | `printf` / `console.log` / `print` |
| `sun na` | Prefix for one-liner statements | (no-op statement prefix) |

```love
bolo "hello"
bolo 42
baby bolo naa "sup bestie"
typing...
sun na bolo "hey"
```

---

### Conditionals

| Keyword | Meaning | C/JS/Python Equivalent |
|---|---|---|
| `agar` / `bewafa` | If conditional | `if` |
| `warna` | Else conditional | `else` |
| `warna agar` | Else-if chain | `else if` / `elif` |
| `ye_karo` / `vo_karo` | Then / Else block (core style) | `{ ... }` block bounds |
| `toh` | Then (natural style) | `{` block open |
| `bas itna hi` | End block (natural style) | `}` block close |

```love
agar score bada hai 90 toh
  bolo "A grade, bohot acha!"
warna agar score bada hai 75 toh
  bolo "B grade, theek hai"
warna agar score bada hai 60 toh
  bolo "C grade, try harder"
warna
  bolo "fail... rona mat"
bas itna hi
```

---

### Loops

| Keyword | Meaning | C/JS/Python Equivalent |
|---|---|---|
| `jabtak` / `intezaar` | While loop | `while` |
| `tab tak` | Until (closes natural-style condition) | `)` condition close |
| `aage_bado` | Continue | `continue` |
| `bas_karo` | Break | `break` |

```love
yaad i = 0
jabtak i chhota hai 5 tab tak
  agar i barabar hai 3 toh
    aage_bado
  bas itna hi
  bolo i
  i hai i milan 1
bas itna hi
```

Output: `0 1 2 4`

---

### Error Handling

| Keyword | Meaning | C/JS/Python Equivalent |
|---|---|---|
| `koshish` / `propose_karo` | Try block | `try` |
| `dil_jodo (err)` | Catch block | `catch (err)` / `except Exception as err` |
| `dil_tuta` / `dil_tutgaya` | Throw exception | `throw` / `raise` |

```love
koshish {
  yaad x = 10
  dil_tuta "kuch galat ho gaya!"
} dil_jodo (galti) {
  bolo "Pakad liya: " milan galti
}
```

---

### Functions vs. Labeled Blocks

Lovelang has two distinct blocks: **`dhadkan` (true functions)** and **`festival` (inline named blocks)**.

| Keyword | Meaning | C/JS/Python Equivalent |
|---|---|---|
| `dhadkan` | Declare a function | `def` / `function` |
| `ehsaas` | Return a value | `return` |
| `festival` | Declare an inline named block | Labeled block with print statement (**NOT a function**) |

#### Functions (`dhadkan`)
Functions are reusable subroutines. They support named parameters, default values, recursion, and return values using the `ehsaas` keyword. They are **only executed when called** by name.

```love
dhadkan greet(name, mood = "romantic", punct = "!") {
  ehsaas mood milan ": " milan name milan punct
}

bolo greet("baby")
bolo greet("baby", mood = "shayari")
bolo greet("baby", mood = "toxic", punct = "...")
```

#### Inline Blocks (`festival`)
A `festival` block is **NOT a function**. It is an inline named block of code that is **executed immediately and automatically** wherever it is defined. Upon entry, it automatically prints `"festival mode: <name>"` to stdout and runs the body code. It cannot be called later and cannot accept arguments.

```love
festival diwali {
  bolo "diye jalaao, khushiyan manaao"
}
// Outputs immediately:
// festival mode: diwali
// diye jalaao, khushiyan manaao
```

---

### Other Keywords

| Keyword | Meaning | C/JS/Python Equivalent |
|---|---|---|
| `import` | Inline another `.love` file | `#include` / `require` / `import` |
| `export` | Mark declaration as reusable | `export` (cosmetic no-op) |
| `sach` | Boolean true | `true` / `True` |
| `jhooth` | Boolean false | `false` / `False` |
| `null` | Null / absence of value | `NULL` / `null` / `None` |
| `love_byeee()` | Exit program | `exit(0)` |

```love
import "utils.love"
export dhadkan helper(x) { ehsaas x milan 1 }
```

---

## Operators

### Symbol operators

| Operator | Meaning |
|---|---|
| `+ - * / %` | Arithmetic |
| `== != < <= > >=` | Comparison |
| `&& \|\| !` | Logical |
| `=` | Assignment |

### Word operators

| Word form | Symbol equivalent | Notes |
|---|---|---|
| `milan` | `+` | Also string concatenation |
| `doori` | `-` | |
| `intense` | `*` | |
| `divide` | `/` | |
| `barabar hai` / `same hai` / `equal hai` | `==` | |
| `barabar nahi` / `alag hai` | `!=` | |
| `chhota hai` | `<` | |
| `bada hai` | `>` | |
| `chhota ya barabar hai` | `<=` | |
| `bada ya barabar hai` | `>=` | |
| `aur` | `&&` | |
| `ya` | `\|\|` | |
| `nahi` | `!` | |

---

## Comments

```love
// single line — classic programmer style
# shell style — feels like a terminal
~~ wavy style — romantic squiggles
baby ignore karo  this is a natural-language comment
/* block comment — for long thoughts */
```

---

## Built-in Functions

### Input / Output / Time

| Function | Description |
|---|---|
| `dil_se_pucho(prompt?)` / `input(prompt?)` | Read line from stdin |
| `thoda_ruko(ms)` | Sleep for N milliseconds |
| `abhi_time()` | Unix timestamp in seconds |
| `kismat(min, max)` | Random integer in inclusive range |

```love
yaad name = dil_se_pucho("apna naam batao: ")
yaad roll = kismat(1, 6)
thoda_ruko(500)
bolo "aapka number hai: " milan roll
```

---

### Math

| Function | Description |
|---|---|
| `abs(n)` / `mutlak(n)` | Absolute value |
| `max(a, b)` / `bada_wala(a, b)` | Maximum of two values |
| `min(a, b)` / `chhota_wala(a, b)` | Minimum of two values |
| `pow(base, exp)` / `taaqat(base, exp)` | Integer exponentiation |
| `sqrt(n)` / `jadoo(n)` | Integer square root |
| `clamp(val, lo, hi)` | Clamp value to a range |

```love
bolo abs(-42)           // 42
bolo pow(2, 10)         // 1024
bolo clamp(150, 0, 100) // 100
bolo sqrt(144)          // 12
bolo max(7, 13)         // 13
```

---

### String Toolkit

| Function | Description |
|---|---|
| `lambai(s)` / `len(s)` | Length of string, list, or map |
| `lafz_trim(s)` | Trim leading and trailing whitespace |
| `lafz_lower(s)` | Convert to lowercase |
| `lafz_upper(s)` | Convert to uppercase |
| `lafz_contains(s, needle)` | Substring check — returns bool |
| `lafz_replace(s, from, to)` | Replace all occurrences |
| `lafz_split(s, sep)` | Split string into a list |
| `lafz_join(list, sep)` | Join list into a string |

```love
bolo lafz_upper("hello")               // HELLO
bolo lafz_trim("  spaces  ")           // spaces
bolo lafz_contains("lovelang", "love") // sach
yaad parts = lafz_split("a,b,c", ",")
bolo lafz_join(parts, " | ")           // a | b | c
bolo lafz_replace("i love you", "love", "miss")  // i miss you
```

---

### Type System

| Function | Description |
|---|---|
| `type_of(v)` / `kya_type(v)` | Returns type name as string |
| `to_text(v)` / `text_banao(v)` | Convert any value to string |
| `to_int(v)` / `int_banao(v)` | Convert to integer |
| `to_bool(v)` / `bool_banao(v)` | Convert to boolean |

```love
bolo type_of(42)         // int
bolo type_of("hello")    // string
bolo type_of(sach)       // bool
bolo to_text(123)        // "123"
bolo to_int("99")        // 99
```

---

### Lists

```love
yaad names = list_nayi()          // create — also: pyaar_list()
list_daal(names, "Laila")         // append
list_daal(names, "Majnu")
list_daal(names, "Heer")
bolo list_lao(names, 0)           // get by index → Laila
list_set(names, 0, "Mirza")      // set by index
yaad last = list_nikaal(names)    // pop last → Heer
bolo lambai(names)                // length → 2
```

---

### Maps

```love
yaad profile = map_naya()         // create — also: raaz_map()
map_set(profile, "name", "Ranjha")
map_set(profile, "age", 25)
map_set(profile, "city", "Lahore")
bolo map_get(profile, "name")     // Ranjha
bolo map_has(profile, "city")     // sach
bolo map_has(profile, "phone")    // jhooth
yaad keys = map_keys(profile)
map_del(profile, "city")
```

---

### Filesystem

| Function | Description |
|---|---|
| `raasta_hai_kya(path)` / `file_hai_kya(path)` | Check if file exists |
| `dil_khol_ke_padho(path)` / `file_padho(path)` | Read entire file to string |
| `ishq_likhdo(path, text)` / `file_likho(path, text)` | Write (overwrite) file |
| `ishq_joddo(path, text)` / `file_jodo(path, text)` | Append to file |

```love
ishq_likhdo("diary.txt", "day one: met someone special\n")
ishq_joddo("diary.txt", "day two: still thinking about them\n")
bolo dil_khol_ke_padho("diary.txt")
bolo raasta_hai_kya("diary.txt")  // sach
```

---

### Memory

| Function | Description |
|---|---|
| `gc_karo()` / `memory_saaf_karo()` | Trigger garbage collection (native mode) |

---

## Modules

```love
// utils.love
export dhadkan add(a, b) {
  ehsaas a milan b
}

export dhadkan greet(name) {
  ehsaas "aye " milan name milan "!"
}
```

```love
// main.love
import "utils.love"

bolo add(3, 4)        // 7
bolo greet("jaan")    // aye jaan!
```

Rules:
- Imports are resolved relative to the calling file
- Each file is inlined exactly once (automatic deduplication)
- `export` is cosmetic and stripped at parse time — it is just documentation

---

## Output Tone Modes

```bash
lovelang file.love --mode romantic   # default — gentle, warm, loving
lovelang file.love --mode shayari    # poetic Urdu/Hindi couplets
lovelang file.love --mode toxic      # sarcastic, passive-aggressive
```

Examples of the same error in each mode:

| Mode | Sample message |
|---|---|
| romantic | `ek chhoti si problem aa gayi, ruk jao...` |
| shayari | `kuch toot gaya, dil ki tarah...` |
| toxic | `seriously? yeh bhi nahi pata tha?` |

---

## Human-Phrase Aliases (Preprocessor)

These phrases are normalized before parsing — write naturally, the preprocessor handles it:

| You write | Becomes |
|---|---|
| `baby bolo na expr` | `bolo expr` |
| `baby bolo naa expr` | `bolo expr` |
| `baby yaad rakho name hai expr` | `vada name = expr` |
| `yaad karo name = expr` | `yaad_karo name = expr` |
| `pucho prompt` | `dil_se_pucho(prompt)` |
| `dil se pucho prompt` | `dil_se_pucho(prompt)` |
| `thoda ruko ms` | `thoda_ruko(ms)` |
| `typing...` | `typing` |
| `love you baby byeee` | `love_byeee()` |
| `agar cond toh` | `agar (cond) ye_karo {` |
| `warna agar cond toh` | `} vo_karo { agar (cond) ye_karo {` |
| `warna` (alone on a line) | `} vo_karo {` |
| `jabtak cond tab tak` | `jabtak (cond) ye_karo {` |
| `intezaar cond tab tak` | `jabtak (cond) ye_karo {` |
| `bas itna hi` | `}` |
| `shuru` (alone on a line) | `{` |
| `khatam` (alone on a line) | `}` |
| `sun na agar cond toh then warna else` | one-liner if-else |

### Bracket Word Aliases

Never type a bracket again — use words instead:

| Bracket | Word to open | Word to close | Meaning |
|---|---|---|---|
| `( )` | `khol` | `band` | khol = open, band = close |
| `{ }` | `shuru` | `khatam` | shuru = begin, khatam = end |
| `[ ]` | `darwaza` | `andar` | darwaza = door, andar = inside |

```love
// Round brackets — function calls and grouping
yaad result hai abs khol -42 band       // abs(-42)   → 42
yaad x hai khol 3 milan 4 band intense 2  // (3+4)*2  → 14

// Curly brackets — code blocks
dhadkan add khol a, b band
shuru
  ehsaas a milan b
khatam

bolo add khol 10, 20 band               // add(10, 20) → 30

// Loop with word blocks
yaad i hai 0
jabtak i chhota hai 3 tab tak
shuru
  bolo i
  i hai i milan 1
khatam

// Square brackets — list indexing
yaad colors = list_nayi()
list_daal(colors, "laal")
list_daal(colors, "neela")
bolo list_lao khol colors, 0 band       // laal
```

---

## Value Types

| Type | Literals / Notes |
|---|---|
| `int` | 64-bit signed integer. `42`, `-7`, `0` |
| `bool` | `sach` (true) or `jhooth` (false) |
| `string` | `"hello"` or `'world'`. UTF-8. Immutable. |
| `float` | `3.14`, `-0.5`. Double precision. |
| `list` | Dynamic array. Mixed types allowed. |
| `map` | String-keyed hash map. Mixed value types. |
| `null` | Absence of a value. |

---

## Native Compile Mode

Lovelang has its own machine code emitter — zero dependencies, zero external tools.

```bash
# Compile and run
lovelang file.love --native --out ./fast_program
./fast_program

# Cross-compile from ARM64 Mac to x86_64 Linux
LOVELANG_TARGET=linux lovelang file.love --native --out ./app_linux

# Cross-compile to Windows
LOVELANG_TARGET=windows lovelang file.love --native --out ./app_windows
```

What native mode supports:

- All arithmetic, comparisons, logical operators
- All control flow: if / else-if / else, while loops, break, continue
- Functions with up to 6 parameters, default args, recursion
- Strings — heap allocation, concatenation, int/bool/null to string coercion
- Lists and maps (heap-allocated, garbage collected)
- Floats (double precision, IEEE 754)
- File I/O
- Built-in math and string functions
- Garbage collection (conservative mark-and-sweep)
- Cross-compilation for macOS / Linux / Windows on both ARM64 and x86_64

---

## CLI Reference

```
lovelang <file.love> [options]

Options:
  --native              Compile to native binary (no external compiler)
  --out <path>          Output path for native binary
  --emit-c              Emit generated C source (legacy transpile mode)
  --tokens              Print token stream and exit
  --debug-love          Trace variable writes at runtime
  --mode <tone>         Output tone: romantic (default), shayari, toxic
  --help                Show help and exit
```

---

## Project Layout

```
lovelang/
├── src/
│   ├── main.c            CLI entry, preprocessor, human-phrase normalization
│   ├── lexer.c           Tokenizer — keywords, operators, literals
│   ├── parser.c          Recursive descent AST builder
│   ├── compiler.c        Bytecode compiler (for interpreter path)
│   ├── runtime.c         Tree-walk interpreter engine + all built-ins
│   ├── codegen.c         Native ARM64 machine code emitter
│   └── codegen_x64.c     Native x86_64 machine code emitter
├── include/
│   └── love.h            Shared types — tokens, AST nodes, value structs
├── examples/             Runnable .love programs (27+ examples)
│   └── modules/          Module import demos
├── benchmark/
│   ├── bench.c           C reference benchmark
│   ├── bench.js          Node.js benchmark
│   ├── bench.py          Python benchmark
│   ├── bench.rb          Ruby benchmark
│   ├── bench.rs          Rust benchmark
│   ├── bench.swift       Swift benchmark
│   ├── bench.pl          Perl benchmark
│   ├── bench_native.love Lovelang native benchmark (20M iters)
│   ├── bench_interp.love Lovelang interpreter benchmark (500K iters)
│   └── run_bench.py      Benchmark runner script
├── extension/            VS Code syntax extension
├── npm/
│   └── lovelang-cli/ npm CLI wrapper package
├── run_all_tests.sh      Cross-platform test suite (macOS, Linux, Windows)
├── Makefile
└── LICENSE
```

---

## Running Tests

```bash
# Run the full test suite
bash run_all_tests.sh

# The suite covers:
# - Interpreter: basics, control flow, functions, collections, I/O, modules
# - Native compile: ARM64 Mach-O (and x86_64 via Zig cross-compile if available)
# - Error cases: expected failures verified
# - npm CLI: unit tests, pack, offline install, online install
# - Run modes: romantic, shayari, toxic
```

Latest results: **50 passed / 0 failed / 1 skipped** (Zig cross-compile, skipped when zig not in PATH)

---

## VS Code Extension

Install from the marketplace or build it manually:

```bash
cd extension
npm run package
code --install-extension love-lang-tools-*.vsix --force
```

Extension features:

- Syntax highlighting for all keywords, operators, and built-ins
- Multi-word keyword support (`bas itna hi`, `baby bolo naa`, `barabar hai`, etc.)
- IntelliSense snippets for functions, loops, conditions
- Hover documentation on hover over any keyword
- Run / Compile commands in the editor title bar
- Visual settings panel for mode and output options

---

## Lovelang Ecosystem

Here is a summary of the entire Lovelang ecosystem and its features:

| Platform / Tool | Version | Key Features |
|---|---|---|
| **Lovelang Core** | `v1.0.0` | Interpreter & Native Compiler (ARM64/x86_64). No dependencies. Fast execution. Output modes: Romantic, Shayari, Toxic. Aliases and Hindi/Urdu syntax support. |
| **VS Code Extension** | `v1.0.0` | Syntax highlighting, snippet autocompletion, hover docs, integrated "Run/Compile" toolbar buttons, custom visual settings panel. |
| **npm CLI Wrapper** (`lovelang-cli`) | `v1.0.0` | Global npm installation, pre-built binaries for macOS/Linux/Windows, zero-config setup, automatic path resolution. |
| **Official Website** | *Live* | Interactive documentation, embedded syntax highlights, beautiful glowing animated UI, responsive mobile design, "Try it" playground (coming soon). |

---

## License

MIT — see [LICENSE](LICENSE)

If you **modify** Lovelang, you must give credit to the original project (patel krish/lovelang).  
If you **advertise** a product or service built with Lovelang, you must include credit to Lovelang in that advertisement — even in paid ads.

---

<div align="center">

Made with ❤️ by [patel krish](https://github.com/PATEL-KRISH-0)

*"Code with feeling."*

</div>

---

## Developer

**Patel Krish**
- **Email:** patelkrish7433@gmail.com
- **Portfolio:** [patelkrish.tech](https://patelkrish.tech)
- **LinkedIn:** [https://www.linkedin.com/in/patelkrish0/](https://www.linkedin.com/in/patelkrish0/)
- 💼 **Looking for a job** (MERN Stack and Backend Development)