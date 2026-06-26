#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { resolvePlatform, supportedTargets } = require("../lib/platform");
const { buildDownloadUrl } = require("../lib/github");
const { VENDOR_DIR, BIN_DIR, getBinaryPath } = require("../lib/paths");
const { downloadToFile } = require("../lib/downloader");

function info(message) {
  console.log("[lovelang-cli] " + message);
}

function fail(message) {
  console.error("[lovelang-cli] " + message);
  process.exit(1);
}

async function main() {
  if (process.env.LOVELANG_SKIP_DOWNLOAD === "1") {
    info("Skipping binary download (LOVELANG_SKIP_DOWNLOAD=1).");
    return;
  }

  // Resolve platform first — fail fast if unsupported
  let target;
  try {
    target = resolvePlatform();
  } catch (err) {
    fail(
      (err && err.message ? err.message : String(err)) +
        ". Supported targets: " +
        supportedTargets().join(", ")
    );
  }

  const owner   = String(process.env.LOVELANG_GITHUB_OWNER    || "PATEL-KRISH-0").trim();
  const repo    = String(process.env.LOVELANG_GITHUB_REPO     || "lovelang").trim();
  const tag     = String(process.env.LOVELANG_GITHUB_TAG      || "latest").trim();
  const baseUrl = String(process.env.LOVELANG_DOWNLOAD_BASE_URL || "").trim();
  const token   = String(process.env.LOVELANG_GITHUB_TOKEN    || "").trim();
  const force   = process.env.LOVELANG_FORCE_DOWNLOAD === "1";

  const binaryPath = getBinaryPath(target.executableName);

  if (!force && fs.existsSync(binaryPath)) {
    info("Binary already exists at " + binaryPath + ". Use LOVELANG_FORCE_DOWNLOAD=1 to refresh.");
    return;
  }

  const downloadUrl = buildDownloadUrl({ owner, repo, tag, assetName: target.assetName, baseUrl });

  // Ensure vendor/bin directory exists before download
  await fs.promises.mkdir(BIN_DIR, { recursive: true });

  const headers = {};
  if (token) {
    headers.Authorization = "Bearer " + token;
  }

  info("Downloading " + target.assetName + " from " + (tag === "latest" ? "latest release" : tag) + "...");
  await downloadToFile(downloadUrl, binaryPath, { headers });

  if (target.platform !== "win32") {
    await fs.promises.chmod(binaryPath, 0o755);
  }

  // Write install manifest
  const manifestPath = path.join(VENDOR_DIR, "install.json");
  const manifest = {
    owner,
    repo,
    tag,
    downloadUrl,
    platform: target.platform,
    arch: target.arch,
    assetName: target.assetName,
    executableName: target.executableName,
    installedAt: new Date().toISOString()
  };
  await fs.promises.writeFile(manifestPath, JSON.stringify(manifest, null, 2) + "\n", "utf8");

  info("✓ Installed: " + binaryPath);
}

main().catch((err) => {
  fail(err && err.message ? err.message : String(err));
});
