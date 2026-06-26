# Love Lang Tools

VS Code extension for the **Lovelang** programming language (`.love` files).

> 📺 [github.com/PATEL-KRISH-0](https://www.github.com/PATEL-KRISH-0)  
> 🐙 [github.com/PATEL-KRISH-0/lovelang](https://github.com/PATEL-KRISH-0/lovelang)

---

## Features

### Syntax Highlighting
- Keywords: `agar`, `warna`, `jabtak`, `dhadkan`, `ehsaas`, `festival`, `import`, `export`, `bolo`, `typing`, ...
- **Multi-word phrases**: `bas itna hi`, `baby bolo naa`, `barabar hai`, `barabar nahi`, `chhota hai`, `bada hai`, `chhota ya barabar hai`, `bada ya barabar hai`, `tab tak`, `sun na agar`, `warna agar`, `love you baby byeee`, ...
- Word operators: `milan`, `doori`, `intense`, `divide`, `aur`, `ya`, `nahi`
- All built-ins: `dil_se_pucho`, `kismat`, `lambai`, `abs`, `max`, `min`, `pow`, `sqrt`, `clamp`, `lafz_*`, `list_*`, `map_*`, file I/O, `gc_karo`, ...
- Booleans: `sach`, `jhooth`, `null`
- Float and integer literals
- Comments: `//`, `#`, `~~`, `baby ignore karo`, `/* ... */`
- Strings with escape sequences

### IntelliSense
- Snippet completions for all language constructs
- Hover documentation for keywords and built-ins

### Run & Compile Commands
Available in the editor title bar and command palette for `.love` files:

| Command | Description |
|---|---|
| `Lovelang: Run Current File` | Interpret current file |
| `Lovelang: Compile Current File` | Native compile |
| `Lovelang: Compile & Run` | Compile then run binary |
| `Lovelang: Run Last Binary` | Re-run last compiled binary |
| `Lovelang: Settings` | Open visual settings panel |

### Settings Panel
A dedicated webview settings panel accessible from the title bar or command palette.

---

## Install

### From VSIX (local build)

```bash
cd extension
npm run package
code --install-extension love-lang-tools-*.vsix --force
```

### From Marketplace
Search **"Love Lang Tools"** in the VS Code Extensions panel.

---

## Extension Settings

| Setting | Type | Default | Description |
|---|---|---|---|
| `lovelang.runBinaryPath` | string | `./lovelang` | Path to lovelang binary |
| `lovelang.runUseScript` | bool | `true` | Use run script when available |
| `lovelang.runScriptPath` | string | `""` | Override run script path |
| `lovelang.runMode` | string | `romantic` | Output tone mode |
| `lovelang.runTokens` | bool | `false` | Include `--tokens` flag |
| `lovelang.runDebugLove` | bool | `false` | Include `--debug-love` flag |
| `lovelang.runExtraArgs` | string[] | `[]` | Extra CLI arguments |
| `lovelang.runInNewTerminal` | bool | `true` | Open new terminal each run |
| `lovelang.compileTarget` | string | `host` | Target platform: `host`, `macos`, `linux`, `windows` |
| `lovelang.compileOutputPath` | string | `""` | Output path for compiled binary |
| `lovelang.compileRunAfter` | bool | `false` | Auto-run after successful compile |
| `lovelang.compileRunInNewTerminal` | bool | `true` | Open new terminal each compile |
| `lovelang.autoAssociateLoveFiles` | bool | `true` | Auto-switch `.love` files to Lovelang mode |
| `lovelang.forceDecorationHighlighting` | bool | `true` | Fallback decoration coloring for themes |
| `lovelang.suggestions.includeHumanPhrases` | bool | `true` | Include natural-phrase snippets |

---

## Troubleshooting

**Highlighting looks plain?**
1. Run `Developer: Reload Window`
2. Confirm file language mode shows `Lovelang` in the status bar
3. Keep `lovelang.forceDecorationHighlighting = true` in settings

**Run command not finding binary?**
- Set `lovelang.runBinaryPath` to the full path of your `lovelang` binary
- Or use `npm install -g lovelang-cli` and set path to `lovelang`
