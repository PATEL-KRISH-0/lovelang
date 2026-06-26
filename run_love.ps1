# run_love.ps1 — Run a Lovelang source file on Windows (PowerShell).
#
# Usage:
#   .\run_love.ps1 [file.love] [lovelang options...]
#
# Examples:
#   .\run_love.ps1 examples\01-romantic-hello.love
#   .\run_love.ps1 myfile.love --mode shayari
#   .\run_love.ps1 myfile.love --native --out build\myapp.exe
#   .\run_love.ps1 myfile.love --tokens
#   .\run_love.ps1 myfile.love --debug-love

param(
    [string]$File = "",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── locate binary ─────────────────────────────────────────────────────────────
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

function Find-LovelangBinary {
    # 1. Prefer local build next to this script
    foreach ($name in @("lovelang.exe", "lovelang")) {
        $candidate = Join-Path $ScriptDir $name
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    # 2. Fall back to system PATH (npm global install, etc.)
    $systemBin = Get-Command "lovelang" -ErrorAction SilentlyContinue
    if ($systemBin) {
        return $systemBin.Source
    }

    return $null
}

$Bin = Find-LovelangBinary
if (-not $Bin) {
    Write-Error @"
[ERROR] lovelang binary not found.
        Build it with:  make
        Or install via: npm install -g lovelang-cli
"@
    exit 1
}

# ── resolve file argument ──────────────────────────────────────────────────────
# If no file given, use the default example
if (-not $File) {
    $File = "examples\01-romantic-hello.love"
}

# If it looks like a flag (e.g. --help), pass everything straight through
if ($File.StartsWith("--")) {
    $allArgs = @($File) + $Rest
    & $Bin @allArgs
    exit $LASTEXITCODE
}

# Sanity-check file exists
if (-not (Test-Path $File)) {
    Write-Error "[ERROR] File not found: $File"
    exit 1
}

# ── run ───────────────────────────────────────────────────────────────────────
if ($Rest -and $Rest.Count -gt 0) {
    & $Bin $File @Rest
} else {
    & $Bin $File
}

exit $LASTEXITCODE
