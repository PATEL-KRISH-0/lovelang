# Lovelang — Technical Internals

> Deep dive into how Lovelang works under the hood: pipeline, data structures,
> memory model, codegen, and everything in between.

---

## Table of Contents

1. [Project Layout](#1-project-layout)
2. [Build System](#2-build-system)
3. [Execution Pipeline](#3-execution-pipeline)
4. [Preprocessor](#4-preprocessor)
5. [Lexer (`lexer.c`)](#5-lexer-lexerc)
6. [Parser (`parser.c`)](#6-parser-parserc)
7. [Runtime Interpreter (`runtime.c`)](#7-runtime-interpreter-runtimec)
8. [Native Compiler (`compiler.c`)](#8-native-compiler-compilerc)
9. [x86-64 Code Generator (`codegen_x64.c`)](#9-x86-64-code-generator-codegen_x64c)
10. [ARM64 Code Generator (`codegen.c`)](#10-arm64-code-generator-codegenc)
11. [Value Representation](#11-value-representation)
12. [Memory Management](#12-memory-management)
13. [Symbol Table & Scoping](#13-symbol-table--scoping)
14. [Function Call Mechanism](#14-function-call-mechanism)
15. [Collections: Lists and Maps](#15-collections-lists-and-maps)
16. [String Handling](#16-string-handling)
17. [Module System](#17-module-system)
18. [Error Handling](#18-error-handling)
19. [WebAssembly Target](#19-webassembly-target)
20. [VS Code Extension Internals](#20-vs-code-extension-internals)
21. [npm Package Internals](#21-npm-package-internals)
22. [CI/CD Pipeline](#22-cicd-pipeline)
23. [Adding a New Keyword](#23-adding-a-new-keyword)
24. [Adding a New Builtin Function](#24-adding-a-new-builtin-function)
25. [Performance Notes](#25-performance-notes)

---

## 1. Project Layout

```
lovelang/
├── src/
│   ├── main.c          — Entry point, flag parsing, pipeline orchestration
│   ├── lexer.c         — Tokeniser: source text → token stream
│   ├── parser.c        — Recursive-descent parser: tokens → AST
│   ├── runtime.c       — Tree-walk interpreter: AST → execution
│   ├── compiler.c      — Native compile driver: AST → C → binary
│   ├── codegen.c       — ARM64 assembly emitter
│   └── codegen_x64.c   — x86-64 assembly emitter
├── include/
│   └── love.h          — Shared types, macros, forward declarations
├── extension/          — VS Code extension (Node.js)
│   ├── extension.js    — Activation, commands, run runner
│   ├── package.json    — Extension manifest
│   └── syntaxes/
│       └── love.tmLanguage.json — TextMate grammar
├── npm/
│   └── lovelang-cli/ — npm CLI wrapper (auto-downloads binary)
│       ├── bin/lovelang.js     — CLI entry point
│       ├── lib/                — platform, downloader, github, paths
│       └── scripts/postinstall.js — downloads binary on npm install
├── web/                — GitHub Pages website
│   ├── index.html
│   ├── docs.html
│   ├── playground.html
│   └── js/             — Site JS (index.js, docs.js, playground.js)
├── examples/           — Example programs (see SYNTAX.md)
│   ├── basics/
│   ├── control-flow/
│   ├── functions/
│   ├── collections/
│   ├── io/
│   ├── modules/
│   ├── native/
│   ├── advanced/
│   ├── errors/
│   └── showcase/
├── .github/workflows/  — CI/CD automation
├── Makefile
├── run_all_tests.sh
├── run_love.sh / .cmd / .ps1
└── README.md / SYNTAX.md / TECHNICAL.md
```

---

## 2. Build System

### Makefile targets

| Target | What it does |
|---|---|
| `make` | Build `lovelang` binary (default) |
| `make clean` | Remove object files and binary |
| `make install` | Copy binary to `/usr/local/bin` |

### Compiler flags used

```makefile
CC     = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Iinclude
SRCS   = src/main.c src/lexer.c src/parser.c src/runtime.c \
         src/compiler.c src/codegen.c src/codegen_x64.c
```

All source files are compiled together. There are no separate `.o` intermediates
in the simple Makefile — a single `cc` invocation compiles the whole project. This
keeps the build fast and dependency-free.

### Cross-compile with Zig

The release CI uses `zig cc` as a drop-in C compiler to produce binaries for all
six targets without needing per-platform runners:

```bash
zig cc -target x86_64-linux-gnu   -std=c11 -O2 -Iinclude src/*.c -o lovelang-linux-x64
zig cc -target aarch64-linux-gnu  -std=c11 -O2 -Iinclude src/*.c -o lovelang-linux-arm64
zig cc -target x86_64-macos       -std=c11 -O2 -Iinclude src/*.c -o lovelang-darwin-x64
zig cc -target aarch64-macos      -std=c11 -O2 -Iinclude src/*.c -o lovelang-darwin-arm64
zig cc -target x86_64-windows-gnu -std=c11 -O2 -Iinclude src/*.c -o lovelang-win32-x64.exe
zig cc -target aarch64-windows-gnu -std=c11 -O2 -Iinclude src/*.c -o lovelang-win32-arm64.exe
```

---

## 3. Execution Pipeline

```
 Source file (.love)
       │
       ▼
 ┌─────────────┐
 │ Preprocessor│  Expand human-mode aliases (baby bolo na → bolo)
 │  (main.c)   │  Normalise multi-word phrases before lexing
 └─────────────┘
       │
       ▼
 ┌─────────────┐
 │   Lexer     │  Convert source text to token stream
 │ (lexer.c)   │  Token: {type, value, line_number}
 └─────────────┘
       │
       ▼
 ┌─────────────┐
 │   Parser    │  Recursive-descent. Produces AST.
 │ (parser.c)  │  AST node: {type, children[], value, line}
 └─────────────┘
       │ AST
       ├──────────────────────────────────────┐
       ▼                                      ▼
 ┌─────────────┐                      ┌─────────────────┐
 │  Runtime    │  Tree-walk           │  Native Compile │
 │ (runtime.c) │  interpret           │  (compiler.c)   │
 └─────────────┘                      └─────────────────┘
       │                                      │
       ▼                                      ├── ARM64 (codegen.c)
  Stdout / Stderr                             └── x86-64 (codegen_x64.c)
                                                     │
                                                     ▼
                                              Native Binary
```

The `--native` flag switches from the runtime branch to the native compile branch.
Both share the same preprocessor, lexer, and parser output.

---

## 4. Preprocessor

**Location:** `main.c` — `preprocess()` function

The preprocessor runs before lexing. It performs text-level substitutions on the
raw source so that human-mode alias phrases are transformed into canonical tokens
that the lexer recognises.

### How it works

The preprocessor is a single-pass string replacement over the source. It handles:

1. **Multi-word phrase normalisation** — e.g. `baby bolo na` → `bolo`,
   `bas itna hi` → end-of-block marker, `love you baby byeee` → stop token.
2. **Order matters** — longer/more-specific phrases are matched before shorter
   ones to prevent partial matches. The phrase table is sorted by length
   (longest first).
3. **Line preservation** — newlines are kept intact so line numbers stay accurate
   in error messages.

### Phrase table (partial)

| Human phrase | Canonical token |
|---|---|
| `baby bolo na` | `bolo` |
| `baby bolo naa` | `bolo` |
| `Baby_bolo_na` | `bolo` |
| `typing...` | `bolo` |
| `baby yaad rakho` | `vada` |
| `baby_yad_rakho` | `vada` |
| `baby ignore karo` | `//` (comment) |
| `love you baby byeee` | `love_byeee()` |
| `bas itna hi` | `}` (block end) |
| `sun na agar` | `agar` (one-liner if) |
| `warna agar` | `else if` |

---

## 5. Lexer (`lexer.c`)

**Entry:** `lex(source, &token_count)` → returns `Token[]`

### Token types

```c
typedef enum {
  TOK_INT, TOK_FLOAT, TOK_STRING, TOK_BOOL, TOK_NULL,
  TOK_IDENT,
  TOK_YAAD, TOK_VADA, TOK_HAI, TOK_BOLO, TOK_AGAR, TOK_TOH,
  TOK_WARNA, TOK_BAS, TOK_DHADKAN, TOK_EHSAAS,
  TOK_INTEZAAR, TOK_JABTAK, TOK_TAB_TAK,
  TOK_MILAN, TOK_DOORI, TOK_INTENSE, TOK_DIVIDE, TOK_BAAKI,
  TOK_BARABAR, TOK_BADA, TOK_CHHOTA, TOK_YA, TOK_AUR, TOK_NAHI,
  TOK_SACH, TOK_JHOOTH,
  TOK_IMPORT, TOK_EXPORT, TOK_FESTIVAL,
  TOK_AAGE_BADO, TOK_BAS_KARO,
  TOK_LBRACE, TOK_RBRACE, TOK_LPAREN, TOK_RPAREN,
  TOK_COMMA, TOK_DOT, TOK_EQUALS, TOK_SEMICOLON,
  TOK_EOF
} TokenType;
```

### Lexer approach

- Manual character-by-character scan (no regex, no external library).
- Keywords are identified by string comparison against a static keyword table
  after scanning an identifier.
- Numbers: integer by default; float when a `.` digit sequence is detected.
- Strings: delimited by `"` or `'`, with `\n`, `\t`, `\\` escape support.
- Comments (`//`, `#`, `~~`, `baby ignore karo`, `/* */`) are stripped during
  lexing and do not produce tokens.
- Each token carries a `line` field populated during the scan for error reporting.

---

## 6. Parser (`parser.c`)

**Entry:** `parse(tokens, token_count)` → returns `ASTNode*` (root)

### AST node structure

```c
typedef struct ASTNode {
  NodeType         type;
  struct ASTNode  *children[MAX_CHILDREN];  // up to 8 children
  int              child_count;
  char             value[MAX_VALUE_LEN];    // literal value string
  int              line;                    // source line (for errors)
} ASTNode;
```

### Node types (partial)

| Node type | Meaning |
|---|---|
| `NODE_PROGRAM` | Root: list of statements |
| `NODE_VAR_DECL` | `yaad name hai expr` |
| `NODE_CONST_DECL` | `vada name hai expr` |
| `NODE_ASSIGN` | `name hai expr` |
| `NODE_BOLO` | Print statement |
| `NODE_IF` | `agar … toh … warna … bas itna hi` |
| `NODE_WHILE` | `intezaar … tab tak … bas itna hi` |
| `NODE_FUNC_DEF` | `dhadkan name(params) { … }` |
| `NODE_FUNC_CALL` | `name(args)` |
| `NODE_RETURN` | `ehsaas expr` |
| `NODE_IMPORT` | `import "path"` |
| `NODE_EXPORT` | `export dhadkan …` |
| `NODE_FESTIVAL` | `festival name { … }` |
| `NODE_BREAK` | `bas_karo` |
| `NODE_CONTINUE` | `aage_bado` |
| `NODE_BINARY_OP` | `a op b` (milan, doori, barabar hai, …) |
| `NODE_UNARY_OP` | `nahi expr` |
| `NODE_LITERAL` | int / float / string / bool / null |
| `NODE_IDENT` | Variable reference |

### Parsing strategy

Recursive-descent, one function per grammar rule. Operator precedence is handled
by structuring the call chain:

```
parse_expr()
  → parse_or()
    → parse_and()
      → parse_comparison()
        → parse_addition()
          → parse_multiplication()
            → parse_unary()
              → parse_primary()
```

**Word operators** map to the same precedence levels as their symbolic equivalents:

| Word operator | Symbolic | Precedence level |
|---|---|---|
| `milan` | `+` | addition |
| `doori` | `-` | addition |
| `intense` | `*` | multiplication |
| `divide` | `/` | multiplication |
| `baaki` | `%` | multiplication |
| `barabar hai` | `==` | comparison |
| `barabar nahi` | `!=` | comparison |
| `bada hai` | `>` | comparison |
| `chhota hai` | `<` | comparison |
| `bada ya barabar hai` | `>=` | comparison |
| `chhota ya barabar hai` | `<=` | comparison |
| `aur` | `&&` | logical AND |
| `ya` | `\|\|` | logical OR |
| `nahi` | `!` | unary NOT |

---

## 7. Runtime Interpreter (`runtime.c`)

**Entry:** `interpret(root_node, env, mode)` → executes the program

### Approach

Tree-walk interpreter. Each `eval(node, env)` call pattern-matches on `node->type`
and recursively evaluates children.

### Value representation

See [Section 11](#11-value-representation) for full details.

### Environment (scope)

```c
typedef struct Env {
  char      names[MAX_VARS][MAX_NAME];
  LoveValue values[MAX_VARS];
  int       count;
  struct Env *parent;            // lexical parent scope
} Env;
```

- `push_scope(env)` creates a child scope.
- `pop_scope()` destroys it.
- Variable lookup walks up the chain via `parent` pointers.
- Assignment resolves in the nearest scope that owns the binding
  (no implicit global promotion).

### Loop guard

A hard iteration limit (`LOOP_GUARD = 10_000_000`) prevents infinite loops from
hanging the process. When a loop body executes more than this many iterations,
the runtime raises a friendly error. In native compiled mode there is no such
guard — the binary runs at full speed.

### Mode system

`mode` is a string (`"romantic"`, `"toxic"`, `"shayari"`) stored in the root env
and used by `love_byeee()` and some built-in output functions to colour their
phrasing. The mode does not affect control flow or values — only the strings
printed by the runtime farewell and error decorators.

### Built-in function dispatch

Built-ins are resolved in `eval_call()` before user-defined function lookup:

```c
if (strcmp(name, "bolo")        == 0) return builtin_bolo(args, env);
if (strcmp(name, "lambai")      == 0) return builtin_lambai(args);
if (strcmp(name, "list_nayi")   == 0) return builtin_list_nayi();
if (strcmp(name, "list_daal")   == 0) return builtin_list_daal(args);
// ... (60+ builtins follow)
```

---

## 8. Native Compiler (`compiler.c`)

**Entry:** `compile_to_native(root, output_path, cc_override)`

The native compiler emits a C source file from the AST, then calls a C compiler
(`cc` by default, overridable via `--cc`) to produce a standalone binary. No
assembly is hand-written for the compilation path — `codegen.c` and
`codegen_x64.c` contain experimental direct assembly emitters used for
research/benchmarking.

### Emit strategy

```
AST → emit_c(node) → temp.c file → cc temp.c -O2 -o output
```

`emit_c()` is a recursive function that outputs C99 for each AST node:

| AST node | Emitted C |
|---|---|
| `NODE_VAR_DECL` | `LoveVal name = expr;` |
| `NODE_ASSIGN` | `name = expr;` |
| `NODE_BOLO` | `love_print(expr);` |
| `NODE_IF` | `if (to_bool(cond)) { … } else { … }` |
| `NODE_WHILE` | `while (to_bool(cond)) { … }` |
| `NODE_FUNC_DEF` | `LoveVal name(LoveVal p1, …) { … }` |
| `NODE_RETURN` | `return expr;` |
| `NODE_BINARY_OP` | `love_add(a, b)` / `love_sub(a, b)` / etc. |
| `NODE_FUNC_CALL` | `name(a1, a2, …)` |

The emitted C file includes a small runtime header (`love_runtime.h`) that is
embedded as a string literal in `compiler.c`. This header provides:
- `LoveVal` tagged union type
- `love_print()`, `love_add()`, `love_sub()`, … helpers
- List and map implementations (resizable array + open-addressing hash map)
- `main()` wrapper that calls the emitted `_love_main()`

### Temporary file handling

```c
char tmp_src[PATH_MAX];
snprintf(tmp_src, sizeof(tmp_src), "%s/love_emit_XXXXXX.c", tmpdir());
int fd = mkstemps(tmp_src, 2);
// write emitted C...
close(fd);
// invoke cc
snprintf(cmd, sizeof(cmd), "%s %s -O2 -o %s", cc, tmp_src, output_path);
system(cmd);
unlink(tmp_src);   // clean up
```

---

## 9. x86-64 Code Generator (`codegen_x64.c`)

**Status:** Experimental / research. Covers: integer literals, variables,
`bolo`, `milan` (add), `doori` (sub), `intezaar` loops, basic `agar` branches.

**Entry:** `codegen_x64(root, output_path)`

Emits raw x86-64 machine code into an ELF (Linux) or Mach-O (macOS) binary
without going through a C compiler. Uses Linux `write(1, …)` syscall for output
and `exit(0)` syscall to terminate.

### Register allocation (naive)

| Register | Use |
|---|---|
| `rax` | Accumulator / return value |
| `rbx` | Saved variable (call-preserved) |
| `rcx` | Loop counter |
| `rdi`, `rsi`, `rdx` | Syscall args |
| `rsp` | Stack pointer |

Values are stored on the stack frame. The code generator tracks each variable's
`rbp` offset in a simple `vars[]` table.

### Instruction encoding (partial)

All instructions are emitted as raw bytes into a `uint8_t buf[]` buffer:

```c
// MOV rax, imm64
emit_byte(0x48); emit_byte(0xB8); emit_i64(value);

// ADD rax, rbx
emit_byte(0x48); emit_byte(0x01); emit_byte(0xD8);

// SYSCALL
emit_byte(0x0F); emit_byte(0x05);
```

Numbers are converted to ASCII by a hand-rolled `itoa` routine also emitted
into the binary's `.text` section.

---

## 10. ARM64 Code Generator (`codegen.c`)

**Status:** Experimental / research. Covers the same subset as x86-64 codegen.

**Entry:** `codegen_arm64(root, output_path)`

Emits AArch64 instructions as 4-byte words into a Mach-O (macOS Apple Silicon)
or ELF binary.

### Register convention

| Register | Use |
|---|---|
| `x0` | Accumulator / first arg / return |
| `x1`–`x7` | Arguments / temporaries |
| `x19`–`x28` | Callee-saved (variables) |
| `x29` | Frame pointer |
| `x30` | Link register (return address) |
| `sp` | Stack pointer |

### Instruction encoding

AArch64 is fixed-width (4 bytes per instruction). Encoding follows the ARM
Architecture Reference Manual:

```c
// MOV x0, #imm16
emit_u32(0xD2800000 | (imm16 << 5) | 0);  // MOVZ x0, imm

// ADD x0, x0, x1
emit_u32(0x8B010000);

// STP x29, x30, [sp, #-16]!  (save frame)
emit_u32(0xA9BF7BFD);

// RET
emit_u32(0xD65F03C0);
```

---

## 11. Value Representation

All Lovelang values use a tagged union `LoveValue` (in the interpreter) and
`LoveVal` (in emitted C code):

```c
typedef enum {
  TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_BOOL, TYPE_NULL,
  TYPE_LIST, TYPE_MAP, TYPE_FUNC
} LoveType;

typedef struct LoveValue {
  LoveType type;
  union {
    long long    i;    // TYPE_INT
    double       f;    // TYPE_FLOAT
    char        *s;    // TYPE_STRING (heap-allocated, null-terminated)
    int          b;    // TYPE_BOOL (0 = jhooth, 1 = sach)
    LoveList    *list; // TYPE_LIST
    LoveMap     *map;  // TYPE_MAP
    LoveFn      *fn;   // TYPE_FUNC (closure + param list)
  };
} LoveValue;
```

### Truth semantics (`to_bool`)

| Value | Truth |
|---|---|
| `0` (int) | `jhooth` |
| non-zero int | `sach` |
| `0.0` (float) | `jhooth` |
| `""` (empty string) | `jhooth` |
| non-empty string | `sach` |
| `sach` | `sach` |
| `jhooth` | `jhooth` |
| `null` | `jhooth` |
| any list | `sach` |
| any map | `sach` |

---

## 12. Memory Management

### Interpreter

- Strings: heap-allocated via `malloc`/`strdup`. Freed when the owning `LoveValue`
  is overwritten or the scope is destroyed.
- Lists: `LoveList` contains a `LoveValue*` buffer grown with `realloc`.
- Maps: `LoveMap` uses open-addressing with linear probe. Load factor triggers
  rehash at 75% capacity.
- AST nodes: allocated with `malloc` during parsing, freed after interpretation
  completes via `free_ast(root)`.
- Environments: stack-allocated `Env` structs (not heap), so scope push/pop is O(1).

### Native compiled output

The emitted C runtime header uses the same `malloc`/`realloc`/`free` strategy.
There is no garbage collector in the native output — the program runs linearly
and all memory is reclaimed by the OS on exit. For long-running programs this
is sufficient; for programs that create many objects in loops, a future version
may add arena allocation.

---

## 13. Symbol Table & Scoping

Lovelang uses **lexical scoping** with dynamic binding resolution. Each `Env`
frame holds up to `MAX_VARS` (currently 256) bindings. The chain is:

```
Global Env
  └── Function scope
        └── Loop scope
              └── If-block scope
```

Lookup walks up until found or global env is exhausted (→ undefined variable error).

Functions capture their **definition-time** env pointer as a closure, enabling
proper lexical scoping even when functions are passed as values.

---

## 14. Function Call Mechanism

### User-defined functions (`dhadkan`)

1. At call site: evaluate all argument expressions in the caller's env.
2. Look up function definition in env chain → retrieves `LoveFn*` struct.
3. Create a new `Env` child of the **function's closure env** (not the caller's env).
4. Bind each parameter name to the corresponding argument value.
5. For default parameters: if argument is missing, evaluate the default expr
   in the closure env.
6. For named arguments: match by parameter name, then fill remaining positionally.
7. Execute the function body. If `ehsaas expr` (return) is hit, unwind with
   a return signal carrying the value.
8. Destroy the function scope, return the value to the caller.

### Festival blocks (`festival name { … }`)

A named scope block with no parameters. Executes immediately when encountered.
No return value. Useful for grouping initialization logic.

### Built-in functions

Directly dispatched in C — no scope creation, no AST nodes. Arguments arrive
as `LoveValue[]`.

---

## 15. Collections: Lists and Maps

### Lists

```c
typedef struct {
  LoveValue *items;
  int        count;
  int        capacity;
} LoveList;
```

- `list_nayi()` → allocates with capacity 8, returns `TYPE_LIST` value.
- `list_daal(lst, v)` → appends; doubles capacity when full.
- `list_lao(lst, i)` → O(1) index access; error if out of bounds.
- `list_set(lst, i, v)` → O(1) write; error if out of bounds.
- `list_nikaal(lst)` → removes and returns last element; error if empty.
- `lambai(lst)` → returns `count` as `TYPE_INT`.

### Maps

```c
typedef struct {
  char     **keys;
  LoveValue *values;
  int        count;
  int        capacity;
  int       *occupied;
} LoveMap;
```

- Open-addressing hash map, string keys.
- Hash function: FNV-1a 32-bit.
- Initial capacity 16; rehash at 75% load.
- `map_naya()` → creates empty map.
- `map_set(m, k, v)` → insert or update.
- `map_get(m, k)` → lookup; returns `null` if missing.
- `map_has(m, k)` → returns `TYPE_BOOL`.
- `map_keys(m)` → returns `TYPE_LIST` of key strings.

---

## 16. String Handling

All strings are **heap-allocated, null-terminated C strings**. The `LoveValue.s`
field is a `char*` to this storage.

String concatenation via `milan` on two strings:
```c
// runtime.c
LoveValue love_concat(LoveValue a, LoveValue b) {
  size_t la = strlen(a.s), lb = strlen(b.s);
  char *out = malloc(la + lb + 1);
  memcpy(out, a.s, la);
  memcpy(out + la, b.s, lb + 1);
  return make_string(out);
}
```

String built-ins all operate on a copy or return new heap strings:

| Built-in | Implementation |
|---|---|
| `lafz_upper(s)` | `toupper()` loop on copy |
| `lafz_lower(s)` | `tolower()` loop on copy |
| `lafz_trim(s)` | skips leading/trailing whitespace |
| `lafz_contains(s, sub)` | `strstr()` |
| `lafz_replace(s, old, new)` | manual find-and-replace loop |
| `lafz_split(s, delim)` | tokenise → new `LoveList` |
| `lafz_join(list, sep)` | concatenate with separator |
| `len(s)` / `lambai(s)` | `strlen()` |

---

## 17. Module System

`import "path/to/file.love"` inlines the target file's AST into the current
program's AST before interpretation begins.

### Deduplication

A global `imported_paths[]` set tracks which files have been imported. A second
`import` of the same path is silently ignored. This prevents circular import
infinite loops and duplicate definitions.

### Resolution

Paths are resolved:
1. Relative to the **importing file's directory** first.
2. Then relative to the **CWD** (where `lovelang` was invoked).

### `export` keyword

`export` is a decorator, not a real access modifier. It marks functions as
"public API" for documentation and tooling purposes. The runtime treats
`export dhadkan foo() { … }` identically to `dhadkan foo() { … }`.

---

## 18. Error Handling

All runtime errors follow this pattern:

```c
void love_error(const char *fmt, int line, ...) {
  fprintf(stderr, "\n❤️ Lovelang Error (line %d):\n   ", line);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n\n");
  exit(1);
}
```

Error categories:
- **Parse errors** — unexpected token, missing `toh`, unclosed block, etc.
- **Runtime errors** — undefined variable, wrong arg count, type mismatch.
- **Collection errors** — out-of-bounds index, pop from empty list.
- **Import errors** — file not found.
- **Overflow errors** — loop guard exceeded.

All errors include the line number and a helpful human-readable message in the
desi style of the language.

---

## 19. WebAssembly Target

Built by the `playground-wasm.yml` and `deploy-website.yml` workflows using
Emscripten (`emcc`). Only the interpreter files are compiled — no native codegen:

```bash
emcc -std=c11 -O3 -Iinclude \
  src/main.c src/lexer.c src/parser.c src/runtime.c \
  -o web/dist/lovelang.js \
  -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 \
  -s EXPORT_NAME=createLovelangModule \
  -s FORCE_FILESYSTEM=1 -s NO_EXIT_RUNTIME=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS='["_main"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS","callMain"]'
```

The playground JS (`web/js/playground.js`) writes user code to the WASM virtual
filesystem via `Module.FS.writeFile("/playground_input.love", source)` then
calls `Module.callMain(["/playground_input.love", "--mode=romantic"])`.

---

## 20. VS Code Extension Internals

**Entry:** `extension/extension.js` — `activate(context)`

### Activation events

- `onLanguage:lovelang` — activates when any `.love` file is opened.

### Commands registered

| Command ID | What it does |
|---|---|
| `lovelang.runFile` | Runs current `.love` file in integrated terminal |
| `lovelang.showSettings` | Opens Settings webview panel |
| `lovelang.showTokens` | Runs with `--tokens` flag, shows debug output |

### Binary resolution (extension.js)

```js
function findBinary(config) {
  // 1. Check lovelang.binaryPath setting (user override)
  const override = config.get("binaryPath");
  if (override) return override;

  // 2. Check PATH via which/where
  const fromPath = which("lovelang");
  if (fromPath) return fromPath;

  // 3. Check npm global bin
  const npmBin = resolveNpmBin("lovelang");
  if (npmBin) return npmBin;

  // 4. Show error
  vscode.window.showErrorMessage("Lovelang binary not found. Install via npm or set lovelang.binaryPath.");
  return null;
}
```

### TextMate grammar

`extension/syntaxes/love.tmLanguage.json` defines scopes for:
- Multi-word keyword phrases (longest match first)
- Single-word keywords
- String literals (double and single quoted)
- Numeric literals (int and float)
- Comments (all five styles)
- Function names (`dhadkan foo`)
- Built-in function calls

---

## 21. npm Package Internals

### Binary discovery at install time (`scripts/postinstall.js`)

1. Detect `process.platform` + `process.arch`.
2. Map to asset name via `lib/platform.js` `TARGET_MATRIX`.
3. Create `vendor/bin/` directory.
4. Download asset from GitHub releases using native `fetch` (Node 18+).
5. `chmod 0o755` on non-Windows.
6. Write `vendor/install.json` manifest.

### Binary discovery at runtime (`bin/lovelang.js`)

1. Check `LOVELANG_BIN_PATH` env var (override).
2. Check `vendor/bin/lovelang[.exe]`.
3. If missing, auto-download (same flow as postinstall).
4. `spawn(binaryPath, process.argv.slice(2), { stdio: "inherit" })`.

### Environment variables

| Variable | Default | Effect |
|---|---|---|
| `LOVELANG_BIN_PATH` | `""` | Override binary path |
| `LOVELANG_SKIP_DOWNLOAD` | `""` | `"1"` = skip download |
| `LOVELANG_FORCE_DOWNLOAD` | `""` | `"1"` = re-download even if exists |
| `LOVELANG_GITHUB_OWNER` | `PATEL-KRISH-0` | GitHub org |
| `LOVELANG_GITHUB_REPO` | `lovelang` | GitHub repo |
| `LOVELANG_GITHUB_TAG` | `latest` | Release tag |
| `LOVELANG_DOWNLOAD_BASE_URL` | `""` | Override full download URL |
| `LOVELANG_GITHUB_TOKEN` | `""` | Auth token for private releases |

---

## 22. CI/CD Pipeline

### Release flow (tag push `v*`)

```
git push tag v1.0.0
    │
    ├── release-binaries.yml  →  6 native binaries + checksums → GitHub Release
    ├── release-vsix.yml      →  love-lang-tools-1.0.0.vsix  → GitHub Release
    └── publish-npm.yml       →  lovelang-cli@1.0.0      → npm
```

### Continuous deploy (push to main)

```
git push main
    │
    ├── deploy-website.yml   →  WASM build + full web/ → GitHub Pages
    └── playground-wasm.yml  →  WASM artifact (deploy optional)
```

### Concurrency groups

All workflows use concurrency groups to prevent duplicate runs:
```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true   # cancel stale for deploys
  cancel-in-progress: false  # never cancel releases mid-flight
```

---

## 23. Adding a New Keyword

Example: adding `todaa` (break in half = integer division `//`).

**Step 1 — Lexer** (`lexer.c`):
```c
// In keyword table:
{ "todaa", TOK_TODAA },
```

**Step 2 — Token type** (`love.h`):
```c
TOK_TODAA,
```

**Step 3 — Parser** (`parser.c`):
```c
// In parse_multiplication():
if (current_token_is(TOK_TODAA)) {
  consume(TOK_TODAA);
  ASTNode *right = parse_unary();
  return make_binary_op(NODE_INT_DIV, left, right);
}
```

**Step 4 — Runtime** (`runtime.c`):
```c
case NODE_INT_DIV: {
  LoveValue l = eval(node->children[0], env);
  LoveValue r = eval(node->children[1], env);
  if (r.i == 0) love_error("todaa by zero", node->line);
  return make_int(l.i / r.i);
}
```

**Step 5 — Native compiler** (`compiler.c`):
```c
case NODE_INT_DIV:
  fprintf(out, "love_int_div(");
  emit_c(node->children[0], out);
  fprintf(out, ", ");
  emit_c(node->children[1], out);
  fprintf(out, ")");
  break;
```

**Step 6 — Grammar** (`extension/syntaxes/love.tmLanguage.json`):
```json
{ "match": "\\btodaa\\b", "name": "keyword.operator.lovelang" }
```

---

## 24. Adding a New Builtin Function

Example: adding `utha_le(list, index)` — "pick up from list at index" (alias for `list_lao`).

**Step 1 — Runtime** (`runtime.c`):
```c
// In eval_call() builtin dispatch block:
if (strcmp(name, "utha_le") == 0) {
  // same as list_lao
  return builtin_list_lao(args, arg_count, line);
}
```

**Step 2 — Docs** — Add to `SYNTAX.md` built-in reference table.

**Step 3 — Extension completion** — Add to the `completionItems` array in
`extension.js` so IntelliSense suggests it.

---

## 25. Performance Notes

### Interpreter

- Tree-walk is ~100–500× slower than a bytecode VM for compute-heavy workloads.
- String-heavy programs pay allocation cost for every concatenation.
- The loop guard (`10M iterations`) is hit in tight loops — use `--native` for
  compute-intensive programs.

### Native compiled

- No interpreter overhead — runs at native C speed.
- `O2` optimisation by default; add `--cc "cc -O3"` for maximum speed.
- No GC pauses — allocation is `malloc`-based, freed on exit.
- Integer loops: essentially the same speed as hand-written C.

### WASM

- Emscripten `-O3` compiles to near-native speed inside the browser sandbox.
- `ALLOW_MEMORY_GROWTH=1` allows the WASM heap to expand dynamically.
- `callMain()` re-entry is supported — each Run in the playground creates a
  fresh FS file and calls `main()` again without reloading the module.

### Benchmark results (M2 MacBook Air, 1M count loop)

| Mode | Time |
|---|---|
| Interpreter | ~0.35 s |
| Native (`-O2`) | ~0.001 s |
| Native (`-O3`) | ~0.0008 s |
| WASM (browser) | ~0.004 s |
