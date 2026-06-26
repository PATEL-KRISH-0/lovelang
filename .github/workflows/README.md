# GitHub Actions Workflows

## Workflows

| Workflow | Trigger | What it does |
|---|---|---|
| `release-binaries.yml` | Push `v*` tag / manual | Cross-compiles native binaries for all platforms, generates checksums, uploads to GitHub Release |
| `release-vsix.yml` | Push `v*` tag / manual | Packages the VS Code extension `.vsix` and uploads to GitHub Release |
| `publish-npm.yml` | Push `v*` tag / manual | Runs tests, verifies version match, publishes `lovelang-cli` to npm |
| `deploy-website.yml` | Push to `main` (web/src changes) / manual | Builds WASM runtime, deploys full `web/` to GitHub Pages |
| `playground-wasm.yml` | Push to `main` (src changes) / manual | Builds WASM runtime artifact; optionally deploys to GitHub Pages |

## Release Flow (tag push)

Pushing a `v*` tag fires three workflows in parallel:

```
git tag -a v1.1.0 -m "Lovelang v1.1.0"
git push origin v1.1.0
```

1. `release-binaries` — builds for Linux x64/arm64, Windows x64/arm64, macOS x64/arm64
2. `release-vsix` — packages and uploads `love-lang-tools-*.vsix`
3. `publish-npm` — publishes `lovelang-cli` to npm

## Required Secrets

| Secret | Used by | Description |
|---|---|---|
| `NPM_TOKEN` | `publish-npm` | npm automation token with publish rights |

## One-Time Setup

1. **GitHub Pages**: Repo Settings → Pages → Source: `GitHub Actions`
2. **npm token**: Repo Settings → Secrets → Actions → `NPM_TOKEN`
