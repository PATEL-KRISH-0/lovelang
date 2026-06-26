# Lovelang — Complete Syntax Reference

> Every keyword, operator, expression, statement, and built-in function.
> Written for humans who feel things. ❤️

---

## Table of Contents

1. [Program Structure](#1-program-structure)
2. [Comments](#2-comments)
3. [Variables](#3-variables)
4. [Constants](#4-constants)
5. [Types and Literals](#5-types-and-literals)
6. [Operators](#6-operators)
7. [Output — Print Statements](#7-output--print-statements)
8. [Conditionals](#8-conditionals)
9. [Loops](#9-loops)
10. [Loop Control — Break and Continue](#10-loop-control--break-and-continue)
11. [Functions](#11-functions)
12. [Festival Blocks](#12-festival-blocks)
13. [Stop — Program Exit](#13-stop--program-exit)
14. [Human-Mode Aliases](#14-human-mode-aliases)
15. [Run Modes](#15-run-modes)
16. [Collections — Lists](#16-collections--lists)
17. [Collections — Maps](#17-collections--maps)
18. [String Built-ins](#18-string-built-ins)
19. [Math Built-ins](#19-math-built-ins)
20. [Type Conversion and Inspection](#20-type-conversion-and-inspection)
21. [Input and I/O](#21-input-and-io)
22. [Filesystem](#22-filesystem)
23. [Time and Random](#23-time-and-random)
24. [Modules — Import and Export](#24-modules--import-and-export)
25. [CLI Reference](#25-cli-reference)
26. [Complete Keyword Index](#26-complete-keyword-index)

---

## 1. Program Structure

A Lovelang program is a sequence of statements read top-to-bottom. There is no
`main()` function — execution begins at the first line of the file.

```love
~~ This is a Lovelang program

yaad name hai "Laila"
bolo "hello, " milan name

love you baby byeee
```

- Statements are separated by newlines. Semicolons are optional.
- Whitespace (spaces, tabs) is insignificant inside expressions.
- Indentation is optional but strongly encouraged for readability.
- The program ends when `love you baby byeee` is executed, or when the last
  line is reached. Either way, a farewell message is printed based on `--mode`.

---

## 2. Comments

Five comment styles — pick the one that feels right:

```love
// C-style single line

# shell-style single line

~~ wavy romantic style (most popular in .love files)

baby ignore karo  natural language style

/* block comment
   can span
   multiple lines */
```

All comments are stripped before parsing. They have zero effect on execution.

---

## 3. Variables

Declare mutable variables with `yaad` (remember):

```love
yaad score hai 0
yaad name hai "Ranjha"
yaad price hai 99.9
yaad active hai sach
yaad pending hai null
```

**Reassign** without a keyword — just use `hai` again:

```love
score hai score milan 10
name hai "Laila"
active hai jhooth
```

**`yaad_karo`** is a natural-sounding alias for `yaad`:

```love
yaad_karo steps hai 0
yaad_karo message hai "tum hi ho meri raah"
```

### Rules

- Variable names: letters, digits, underscores. Must start with a letter or `_`.
- Case-sensitive: `Name` and `name` are different variables.
- Undeclared variable access → runtime error.
- Variables are scoped to their declaration block (function, loop, if-block).

---

## 4. Constants

Declare immutable values with `vada` (promise):

```love
vada pi hai 3.14159
vada author hai "patel krish"
vada max_lives hai 3
```

Attempting to reassign a constant → runtime error.

**`baby yaad rakho`** is the human-mode alias for `vada`:

```love
baby yaad rakho piya hai "forever"
baby yaad rakho jannat hai "right here"
```

---

## 5. Types and Literals

### Integer

```love
yaad n hai 42
yaad neg hai -7
yaad big hai 1000000
```

### Float

```love
yaad pi hai 3.14159
yaad temp hai -0.5
yaad rate hai 1.075
```

### String

Delimited by `"` or `'`. Escape sequences: `\n`, `\t`, `\\`, `\"`, `\'`.

```love
yaad greeting hai "salam duniya"
yaad reply hai 'offline nahi hoon'
yaad multiline hai "pehli line\ndoosri line"
```

### Boolean

```love
yaad online hai sach     ~~ true
yaad broken hai jhooth   ~~ false
```

### Null

```love
yaad reply hai null
```

### Type display

```love
bolo type_of(42)        ~~ int
bolo type_of(3.14)      ~~ float
bolo type_of("hi")      ~~ string
bolo type_of(sach)      ~~ bool
bolo type_of(null)      ~~ null
bolo type_of(list_nayi()) ~~ list
bolo type_of(map_naya())  ~~ map
```

---

## 6. Operators

### Arithmetic (word operators)

| Word | Symbol | Example | Result |
|---|---|---|---|
| `milan` | `+` | `5 milan 3` | `8` |
| `doori` | `-` | `10 doori 4` | `6` |
| `intense` | `*` | `6 intense 7` | `42` |
| `divide` | `/` | `20 divide 4` | `5` |
| `baaki` | `%` | `17 baaki 5` | `2` |

Symbol operators (`+`, `-`, `*`, `/`, `%`) are also accepted — Lovelang supports
both. Word operators are preferred in natural style programs.

### String concatenation

`milan` works on strings too:

```love
bolo "hello " milan "jaan"        ~~ hello jaan
bolo name milan " se pyaar hai"
bolo "score: " milan to_text(42)  ~~ always convert numbers first
```

### Comparison

| Word | Symbol | Meaning |
|---|---|---|
| `barabar hai` | `==` | equal |
| `barabar nahi` | `!=` | not equal |
| `bada hai` | `>` | greater than |
| `chhota hai` | `<` | less than |
| `bada ya barabar hai` | `>=` | greater than or equal |
| `chhota ya barabar hai` | `<=` | less than or equal |

```love
agar score bada ya barabar hai 90 toh
  bolo "A grade"
bas itna hi
```

### Logical

| Word | Symbol | Meaning |
|---|---|---|
| `aur` | `&&` | logical AND |
| `ya` | `\|\|` | logical OR |
| `nahi` | `!` | logical NOT |

```love
agar age bada hai 18 aur has_job toh
  bolo "eligible"
bas itna hi

agar mood barabar hai "happy" ya kismat(1,2) barabar hai 1 toh
  bolo "positive day"
bas itna hi

bolo nahi jhooth   ~~ sach
```

### Operator precedence (highest to lowest)

| Level | Operators |
|---|---|
| 6 (highest) | unary `nahi` / `-` |
| 5 | `intense`, `divide`, `baaki` |
| 4 | `milan`, `doori` |
| 3 | `bada hai`, `chhota hai`, `barabar hai`, … |
| 2 | `aur` |
| 1 (lowest) | `ya` |

Use parentheses to override precedence:

```love
yaad result hai (2 milan 3) intense 4   ~~ 20
```

---

## 7. Output — Print Statements

Lovelang has multiple ways to say "print" — each carries a slightly different
emotional weight:

| Syntax | Feeling |
|---|---|
| `bolo expr` | direct, matter-of-fact |
| `baby bolo na expr` | soft, loving request |
| `baby bolo naa expr` | even softer, extra affectionate |
| `typing...` | playful (looks like you're typing) |
| `Baby_bolo_na expr` | capitalised alias, same as bolo |

All of these print `expr` followed by a newline to stdout.

```love
bolo "salam duniya"
baby bolo na "kya haal hai"
baby bolo naa "tum aaye — dil bhar aaya 💌"
typing... "sending this message..."
```

Multiple values on the same `bolo` — concatenate with `milan`:

```love
yaad age hai 22
bolo "I am " milan to_text(age) milan " years old"
```

---

## 8. Conditionals

### Full form (multi-line)

```love
agar condition toh
  ~~ if body
warna agar other_condition toh
  ~~ else-if body
warna
  ~~ else body
bas itna hi
```

- `agar` = "if"
- `toh` = "then"
- `warna` = "otherwise / else"
- `warna agar` = "otherwise if / else if"
- `bas itna hi` = "that's enough / end"

Any number of `warna agar` chains are supported.

```love
agar score bada ya barabar hai 90 toh
  bolo "A"
warna agar score bada ya barabar hai 75 toh
  bolo "B"
warna agar score bada ya barabar hai 50 toh
  bolo "C"
warna
  bolo "F"
bas itna hi
```

### One-liner form

```love
sun na agar condition toh statement warna statement
```

- `sun na agar` = "listen, if" — triggers the one-liner parser.
- No `bas itna hi` needed.
- No `warna agar` chains — exactly one if/else branch.

```love
sun na agar x bada hai 0 toh bolo "positive" warna bolo "not positive"
sun na agar online toh baby bolo na "woh online hai!" warna bolo "offline 😔"
```

### Nested conditionals

```love
agar a bada hai b toh
  agar a bada hai c toh
    bolo "a is biggest"
  warna
    bolo "c is bigger than a"
  bas itna hi
warna
  bolo "b >= a"
bas itna hi
```

---

## 9. Loops

### `intezaar … tab tak` (natural style)

`intezaar` = "wait" — keep going until the condition is false.

```love
yaad i hai 0
intezaar i chhota hai 10 tab tak
  bolo i
  i hai i milan 1
bas itna hi
```

### `jabtak … tab tak` (core style)

Same semantics, different keyword. `jabtak` = "as long as".

```love
yaad n hai 100
jabtak n bada hai 0 tab tak
  baby bolo na n
  n hai n doori 1
bas itna hi
```

### Nested loops

```love
yaad row hai 1
intezaar row chhota ya barabar hai 5 tab tak
  yaad col hai 1
  intezaar col chhota ya barabar hai 5 tab tak
    bolo to_text(row) milan " x " milan to_text(col)
    col hai col milan 1
  bas itna hi
  row hai row milan 1
bas itna hi
```

> **Loop guard:** The interpreter stops a loop after 10,000,000 iterations to
> prevent infinite loops. Use `--native` mode for compute-heavy loops.

---

## 10. Loop Control — Break and Continue

### `bas_karo` — break

Exit the innermost loop immediately:

```love
yaad i hai 0
intezaar i chhota hai 100 tab tak
  agar i barabar hai 7 toh
    bas_karo
  bas itna hi
  i hai i milan 1
bas itna hi
bolo "stopped at:" milan to_text(i)
```

### `aage_bado` — continue

Skip to the next iteration:

```love
yaad i hai 0
intezaar i chhota hai 10 tab tak
  agar i intense 2 barabar hai 0 toh
    i hai i milan 1
    aage_bado
  bas itna hi
  bolo i   ~~ only odd numbers
  i hai i milan 1
bas itna hi
```

> **Important:** When using `aage_bado`, make sure to increment your counter
> before calling it, otherwise you'll create an infinite loop.

---

## 11. Functions

### Declaration

```love
dhadkan function_name(param1, param2) {
  ~~ body
  ehsaas return_value
}
```

- `dhadkan` = "heartbeat" — the function declaration keyword.
- `ehsaas` = "feeling" — the return keyword.
- A function without `ehsaas` returns `null`.
- Functions are values and can be stored in variables.

```love
dhadkan add(a, b) {
  ehsaas a milan b
}

dhadkan greet(name) {
  bolo "salam, " milan name milan " 💌"
  ehsaas sach
}
```

### Default parameters

```love
dhadkan confess(name, feeling = "pyaar", emoji = "❤️") {
  bolo name milan ": " milan feeling milan " " milan emoji
  ehsaas null
}

confess("Laila")                         ~~ uses both defaults
confess("Ranjha", "intezaar")            ~~ overrides feeling
confess("Heer", "dard", "🌧️")            ~~ overrides both
```

### Named arguments

Pass arguments by name in any order:

```love
dhadkan describe(name, age, city) {
  bolo name milan ", " milan to_text(age) milan ", " milan city
  ehsaas null
}

describe("Laila", city = "Delhi", age = 22)
describe(age = 30, name = "Ranjha", city = "Jhang")
```

Mix positional and named — positional must come first:

```love
describe("Heer", 22, city = "Sial")
```

### Recursion

```love
dhadkan factorial(n) {
  agar n chhota ya barabar hai 1 toh
    ehsaas 1
  bas itna hi
  ehsaas n intense factorial(n doori 1)
}

bolo factorial(10)   ~~ 3628800
```

### Closures

Functions capture their enclosing scope at definition time:

```love
yaad base hai 100

dhadkan add_to_base(n) {
  ehsaas base milan n
}

bolo add_to_base(50)   ~~ 150
base hai 200
bolo add_to_base(50)   ~~ 250
```

### Calling functions

```love
yaad result hai add(10, 20)
greet("Majnu")
describe("Heer", 22, "Lahore")
```

---

## 12. Festival Blocks

Named scope blocks — no parameters, no return value. Execute immediately.

```love
festival startup {
  bolo "initialising..."
  yaad version hai "1.0.0"
  bolo "Lovelang " milan version milan " ready ❤️"
}

festival cleanup {
  bolo "bye-bye, saving state..."
}
```

Use them for grouping related setup/teardown logic.

---

## 13. Stop — Program Exit

```love
love you baby byeee
```

Stops program execution immediately. Any code after this line does not run.
Prints a farewell message styled by the active `--mode`.

Aliases:
```love
love_byeee()        ~~ function call form
byeee               ~~ short form
```

Mode farewell messages:
- `romantic` → `"dil se bidaai... ❤️"`
- `toxic` → `"jaao, jo karna tha kar liya ❤️"`
- `shayari` → `"woh baat khatam hui jo shuru hi na hoti to achha tha 🌙"`

---

## 14. Human-Mode Aliases

The preprocessor expands these phrases before lexing. Use them to write code
that reads like conversation.

| Human phrase | Canonical form | Meaning |
|---|---|---|
| `baby bolo na` | `bolo` | print |
| `baby bolo naa` | `bolo` | print (softer) |
| `Baby_bolo_na` | `bolo` | print (capitalised) |
| `typing...` | `bolo` | print (playful) |
| `baby yaad rakho` | `vada` | constant |
| `baby_yad_rakho` | `vada` | constant |
| `baby ignore karo` | `//` | comment |
| `love you baby byeee` | `love_byeee()` | stop |
| `bas itna hi` | `}` (end block) | end if/loop/function |
| `sun na agar` | `agar` (one-liner) | one-liner if |
| `warna agar` | `else if` | else if |
| `yaad_karo` | `yaad` | mutable variable |

---

## 15. Run Modes

Pass `--mode` flag to change the emotional tone of output:

```bash
lovelang hello.love --mode romantic   # default
lovelang hello.love --mode toxic
lovelang hello.love --mode shayari
```

Mode affects:
- The farewell message from `love you baby byeee`
- Error message phrasing
- Some built-in output decorators

Modes do NOT affect control flow, values, or computation.

In the playground (`playground.html`), the mode selector changes the `--mode`
argument passed to `callMain`.

---

## 16. Collections — Lists

Dynamic arrays of any `LoveValue` type.

### Create

```love
yaad my_list hai list_nayi()
```

### Append

```love
list_daal(my_list, "pyaar")
list_daal(my_list, 42)
list_daal(my_list, sach)
```

### Access (zero-indexed)

```love
bolo list_lao(my_list, 0)    ~~ first item
bolo list_lao(my_list, 2)    ~~ third item
```

### Update

```love
list_set(my_list, 0, "naya value")
```

### Remove last item (pop)

```love
yaad last hai list_nikaal(my_list)
```

### Length

```love
bolo lambai(my_list)
bolo len(my_list)   ~~ alias
```

### Iterate

```love
yaad i hai 0
intezaar i chhota hai lambai(my_list) tab tak
  baby bolo na list_lao(my_list, i)
  i hai i milan 1
bas itna hi
```

### Error cases

- `list_lao(lst, i)` where `i >= lambai(lst)` → runtime error.
- `list_nikaal(lst)` on empty list → runtime error.

---

## 17. Collections — Maps

String-keyed hash maps. Keys must be strings; values can be any type.

### Create

```love
yaad profile hai map_naya()
```

### Set / update

```love
map_set(profile, "name", "Heer")
map_set(profile, "age", 22)
map_set(profile, "active", sach)
```

### Get

```love
bolo map_get(profile, "name")    ~~ "Heer"
bolo map_get(profile, "missing") ~~ null (no error)
```

### Check existence

```love
agar map_has(profile, "age") toh
  bolo "age field found"
bas itna hi
```

### Get all keys

```love
yaad keys hai map_keys(profile)   ~~ returns a list of strings
bolo lambai(keys)
```

### Common pattern — iterate map

```love
yaad keys hai map_keys(profile)
yaad i hai 0
intezaar i chhota hai lambai(keys) tab tak
  yaad k hai list_lao(keys, i)
  bolo k milan ": " milan to_text(map_get(profile, k))
  i hai i milan 1
bas itna hi
```

---

## 18. String Built-ins

All string functions take strings and return new strings or other values.
They never modify the original string.

### `lambai(s)` / `len(s)`

```love
bolo lambai("hello")    ~~ 5
bolo len("pyaar")       ~~ 5
```

### `lafz_upper(s)`

```love
bolo lafz_upper("hello")    ~~ HELLO
```

### `lafz_lower(s)`

```love
bolo lafz_lower("WORLD")    ~~ world
```

### `lafz_trim(s)`

Removes leading and trailing whitespace.

```love
bolo lafz_trim("  salam  ")   ~~ "salam"
```

### `lafz_contains(s, sub)`

Returns `sach` or `jhooth`.

```love
bolo lafz_contains("pyaar mohabbat", "mohabbat")   ~~ sach
bolo lafz_contains("hello", "xyz")                  ~~ jhooth
```

### `lafz_replace(s, old, new)`

Replaces **all** occurrences.

```love
bolo lafz_replace("I miss you I miss you", "miss", "love")
~~ "I love you I love you"
```

### `lafz_split(s, delimiter)`

Returns a list of strings.

```love
yaad parts hai lafz_split("a,b,c,d", ",")
bolo lambai(parts)            ~~ 4
bolo list_lao(parts, 1)       ~~ "b"
```

### `lafz_join(list, separator)`

Joins a list of strings with a separator.

```love
yaad words hai list_nayi()
list_daal(words, "pyaar")
list_daal(words, "dard")
list_daal(words, "sukoon")
bolo lafz_join(words, " aur ")
~~ "pyaar aur dard aur sukoon"
```

### `milan` for concatenation

```love
yaad full hai "tum" milan " " milan "ho" milan " meri" milan " duniya"
bolo full   ~~ "tum ho meri duniya"
```

---

## 19. Math Built-ins

### `abs(n)`

Absolute value.

```love
bolo abs(-42)    ~~ 42
bolo abs(7)      ~~ 7
```

### `max_val(a, b)`

```love
bolo max_val(10, 20)    ~~ 20
```

### `min_val(a, b)`

```love
bolo min_val(10, 20)    ~~ 10
```

### `pow_val(base, exp)`

```love
bolo pow_val(2, 10)    ~~ 1024
bolo pow_val(3, 3)     ~~ 27
```

### `sqrt_val(n)`

Returns float.

```love
bolo sqrt_val(144)    ~~ 12
bolo sqrt_val(2)      ~~ 1.41421...
```

### `clamp_val(value, min, max)`

Keeps a value within `[min, max]`.

```love
bolo clamp_val(150, 0, 100)    ~~ 100
bolo clamp_val(-5, 0, 100)     ~~ 0
bolo clamp_val(50, 0, 100)     ~~ 50
```

---

## 20. Type Conversion and Inspection

### `to_text(v)` / `to_str(v)`

Convert any value to string.

```love
bolo to_text(42)       ~~ "42"
bolo to_text(3.14)     ~~ "3.14"
bolo to_text(sach)     ~~ "sach"
bolo to_text(null)     ~~ "null"
```

### `to_int(v)`

Convert string or float to integer.

```love
bolo to_int("42")     ~~ 42
bolo to_int(3.9)      ~~ 3 (truncates)
bolo to_int("007")    ~~ 7
```

### `to_float(v)` / `to_num(v)`

```love
bolo to_float("3.14")    ~~ 3.14
bolo to_float(7)         ~~ 7.0
```

### `to_bool(v)`

```love
bolo to_bool(1)       ~~ sach
bolo to_bool(0)       ~~ jhooth
bolo to_bool("yes")   ~~ sach
bolo to_bool("")      ~~ jhooth
bolo to_bool(null)    ~~ jhooth
```

### `type_of(v)`

Returns a string: `"int"`, `"float"`, `"string"`, `"bool"`, `"null"`,
`"list"`, `"map"`, `"func"`.

```love
bolo type_of(42)           ~~ int
bolo type_of("hi")         ~~ string
bolo type_of(list_nayi())  ~~ list
```

---

## 21. Input and I/O

### `dil_se_pucho(prompt)` / `pucho(prompt)` / `input(prompt)`

Read a line from stdin. Returns the string (without trailing newline).

```love
yaad name hai dil_se_pucho("tumhara naam: ")
bolo "salam, " milan name

yaad age hai to_int(pucho("teri umar: "))
bolo "tum " milan to_text(age) milan " saal ke ho"
```

> Interactive input is not supported in the playground (WASM) or when stdin
> is not a TTY (e.g. CI pipelines). Uncomment input lines when running locally.

---

## 22. Filesystem

### `ishq_likhdo(path, content)` / `file_likho(path, content)`

Write (overwrite) a file.

```love
ishq_likhdo("letter.txt", "pehla khatt 💌\n")
file_likho("letter.txt", "naya khatt\n")
```

### `ishq_joddo(path, content)` / `file_jodo(path, content)`

Append to a file.

```love
ishq_joddo("letter.txt", "doosri line\n")
file_jodo("letter.txt", "teesri line\n")
```

### `dil_khol_ke_padho(path)` / `file_padho(path)`

Read full file contents as a string.

```love
yaad contents hai dil_khol_ke_padho("letter.txt")
bolo contents

yaad text hai file_padho("data.txt")
```

### `raasta_hai_kya(path)` / `file_hai_kya(path)`

Check if a file exists. Returns `sach` or `jhooth`.

```love
agar raasta_hai_kya("config.txt") toh
  bolo "config found"
warna
  bolo "config missing"
bas itna hi
```

---

## 23. Time and Random

### `abhi_time()` / `time_abhi()`

Returns current Unix timestamp as float (seconds since epoch).

```love
yaad t0 hai abhi_time()
~~ ... do some work ...
yaad t1 hai abhi_time()
bolo "elapsed: " milan to_text(t1 doori t0) milan "s"
```

### `thoda_ruko(ms)` / `ruko(ms)`

Sleep for `ms` milliseconds.

```love
bolo "waiting..."
thoda_ruko(500)
bolo "done"
```

### `kismat(min, max)` / `random(min, max)`

Return a random integer in `[min, max]` inclusive.

```love
yaad dice hai kismat(1, 6)
bolo "you rolled: " milan to_text(dice)

yaad lucky hai kismat(1, 100)
bolo lucky
```

---

## 24. Modules — Import and Export

### `import "path"`

Inlines the target file's code at the point of the import statement. Relative
paths are resolved first from the importing file's directory, then from CWD.

```love
import "lib/utils.love"
import "examples/modules/lib/utils.love"
```

Safe to import the same file multiple times — deduplicated automatically.

### `export`

Marks a function as public API. Cosmetic only — no access control enforced.

```love
~~ In lib/utils.love:
export dhadkan greet(name) {
  bolo "salam, " milan name
  ehsaas null
}

~~ In main file:
import "lib/utils.love"
greet("Laila")
```

---

## 25. CLI Reference

```
Usage: lovelang <file.love> [options]

Options:
  --mode romantic|toxic|shayari   Set output emotional mode (default: romantic)
  --native                        Compile to native binary instead of interpreting
  --out <path>                    Output path for native binary (default: ./a.out)
  --cc <compiler>                 C compiler for native mode (default: cc)
  --tokens                        Debug: print token stream and exit
  --ast                           Debug: print AST and exit
  --version                       Print version and exit
  --help                          Print this help and exit

Native compile example:
  lovelang hello.love --native --out ./hello
  ./hello

Cross-compile with Zig:
  lovelang hello.love --native --out ./hello-linux --cc "zig cc -target x86_64-linux-gnu"

Run modes:
  lovelang hello.love --mode shayari
  lovelang hello.love --mode toxic
```

### Shell scripts

| Script | Platform | Usage |
|---|---|---|
| `run_love.sh` | macOS / Linux | `bash run_love.sh hello.love --mode shayari` |
| `run_love.cmd` | Windows CMD | `run_love.cmd hello.love` |
| `run_love.ps1` | PowerShell | `.\run_love.ps1 hello.love` |

The shell scripts discover the binary in: script's directory → PATH → helpful error.

---

## 26. Complete Keyword Index

### Statement keywords

| Keyword | Meaning |
|---|---|
| `yaad` | Declare mutable variable |
| `yaad_karo` | Alias of `yaad` |
| `vada` | Declare immutable constant |
| `baby yaad rakho` | Alias of `vada` |
| `hai` | Assignment / equality operator |
| `bolo` | Print (output) |
| `agar` | If |
| `toh` | Then (closes condition) |
| `warna` | Else |
| `warna agar` | Else if |
| `sun na agar` | One-liner if |
| `bas itna hi` | End block (if / loop) |
| `intezaar` | While loop (natural) |
| `jabtak` | While loop (core) |
| `tab tak` | Closes loop condition |
| `dhadkan` | Function declaration |
| `ehsaas` | Return |
| `festival` | Named scope block |
| `import` | Import a .love file |
| `export` | Mark as public |
| `aage_bado` | Continue (skip iteration) |
| `bas_karo` | Break (exit loop) |
| `love you baby byeee` | Stop program |
| `love_byeee()` | Stop (function form) |

### Value keywords

| Keyword | Value |
|---|---|
| `sach` | `true` |
| `jhooth` | `false` |
| `null` | `null` |

### Arithmetic operators

| Keyword | Operation |
|---|---|
| `milan` | add / concat |
| `doori` | subtract |
| `intense` | multiply |
| `divide` | divide |
| `baaki` | modulo |

### Comparison operators

| Keyword | Operation |
|---|---|
| `barabar hai` | equal to |
| `barabar nahi` | not equal to |
| `bada hai` | greater than |
| `chhota hai` | less than |
| `bada ya barabar hai` | greater than or equal |
| `chhota ya barabar hai` | less than or equal |

### Logical operators

| Keyword | Operation |
|---|---|
| `aur` | AND |
| `ya` | OR |
| `nahi` | NOT |

### Comment styles

```
// single line
# single line  
~~ single line
baby ignore karo  single line
/* multi line */
```

---

*Lovelang v1.0.0 — Built with ❤️ and C11 by patel krish*
*github.com/PATEL-KRISH-0/lovelang — Lovelang Attribution License v1.0*
