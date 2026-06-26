# lovelang-cli

**Lovelang** — a programming language with romantic, Hindi/Urdu, and natural-language syntax. Write code that reads like poetry.

> 📺 YouTube: [github.com/PATEL-KRISH-0](https://www.github.com/PATEL-KRISH-0)  
> 🐙 GitHub: [github.com/PATEL-KRISH-0/lovelang](https://github.com/PATEL-KRISH-0/lovelang)

This npm package downloads the correct native Lovelang binary for your platform and exposes the `lovelang` CLI command.

## Install

```bash
npm install -g lovelang-cli
```

## Usage

```bash
lovelang examples/01-romantic-hello.love
lovelang examples/03-loop-love-count.love --native --out ./loop-fast
./loop-fast
```

```bash
lovelang --help
```

## Quick Example

```love
yaad name hai "jaan"

baby bolo naa "hello, " milan name

agar name barabar hai "jaan" toh
  bolo "welcome back"
warna
  bolo "who are you?"
bas itna hi

love you baby byeee
```

## How It Works

1. **postinstall** — detects your platform (`darwin`, `linux`, `win32`) and arch (`x64`, `arm64`)
2. Builds the correct GitHub release URL and downloads the binary into `vendor/bin/`
3. The `lovelang` bin script forwards CLI args to the downloaded binary
4. If the binary is missing at runtime, it is fetched automatically before executing

## Supported Platforms

| Platform | Arch |
|---|---|
| macOS | arm64, x64 |
| Linux | arm64, x64 |
| Windows | arm64, x64 |

Binary names follow the pattern: `lovelang-<platform>-<arch>[.exe]`

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `LOVELANG_GITHUB_OWNER` | `PATEL-KRISH-0` | GitHub owner |
| `LOVELANG_GITHUB_REPO` | `lovelang` | GitHub repository |
| `LOVELANG_GITHUB_TAG` | `latest` | Release tag to download |
| `LOVELANG_DOWNLOAD_BASE_URL` | — | Override full base URL for binary download |
| `LOVELANG_GITHUB_TOKEN` | — | Auth token for private/rate-limited downloads |
| `LOVELANG_SKIP_DOWNLOAD` | — | Set to `1` to skip postinstall download |
| `LOVELANG_FORCE_DOWNLOAD` | — | Set to `1` to re-download even if binary exists |
| `LOVELANG_BIN_PATH` | — | Override path to a custom local binary |
| `LOVELANG_TARGET` | host | Cross-compile target: `macos`, `linux`, `windows` |

## Development

```bash
npm run clean   # remove downloaded binary
npm test        # run package tests
```

## License

MIT
