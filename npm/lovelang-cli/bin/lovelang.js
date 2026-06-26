#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");
const { resolvePlatform } = require("../lib/platform");
const { getBinaryPath } = require("../lib/paths");
const { buildDownloadUrl } = require("../lib/github");
const { downloadToFile } = require("../lib/downloader");

function fail(message) {
  console.error("[lovelang-cli] " + message);
  process.exit(1);
}

async function ensureBinary(binaryPath, options = {}) {
  if (fs.existsSync(binaryPath)) {
    return binaryPath;
  }

  if (options.isOverridePath) {
    fail(
      "Lovelang binary not found at LOVELANG_BIN_PATH location: " +
        binaryPath +
        ". Update LOVELANG_BIN_PATH to a valid executable path or unset it."
    );
  }

  if (process.env.LOVELANG_SKIP_DOWNLOAD === "1") {
    fail(
      "Lovelang binary missing at " +
        binaryPath +
        ". LOVELANG_SKIP_DOWNLOAD=1 is set, so auto-download is disabled. " +
        "Reinstall without LOVELANG_SKIP_DOWNLOAD or run: npm rebuild lovelang-cli"
    );
  }

  let target;
  try {
    target = resolvePlatform();
  } catch (err) {
    fail(err && err.message ? err.message : String(err));
  }

  const owner = String(process.env.LOVELANG_GITHUB_OWNER || "PATEL-KRISH-0").trim();
  const repo = String(process.env.LOVELANG_GITHUB_REPO || "lovelang").trim();
  const tag = String(process.env.LOVELANG_GITHUB_TAG || "latest").trim();
  const baseUrl = String(process.env.LOVELANG_DOWNLOAD_BASE_URL || "").trim();
  const token = String(process.env.LOVELANG_GITHUB_TOKEN || "").trim();

  const downloadUrl = buildDownloadUrl({
    owner,
    repo,
    tag,
    assetName: target.assetName,
    baseUrl
  });

  const headers = {};
  if (token) {
    headers.Authorization = "Bearer " + token;
  }

  console.log("[lovelang-cli] Binary missing. Downloading " + target.assetName + "...");
  try {
    await downloadToFile(downloadUrl, binaryPath, { headers });
    if (target.platform !== "win32") {
      await fs.promises.chmod(binaryPath, 0o755);
    }
  } catch (err) {
    fail(
      "Failed to auto-download Lovelang binary from " +
        downloadUrl +
        ". " +
        (err && err.message ? err.message : String(err))
    );
  }

  return binaryPath;
}

function resolveBinaryPath() {
  const override = String(process.env.LOVELANG_BIN_PATH || "").trim();
  if (override) {
    return {
      path: path.isAbsolute(override) ? override : path.resolve(process.cwd(), override),
      isOverridePath: true
    };
  }

  const target = resolvePlatform();
  return {
    path: getBinaryPath(target.executableName),
    isOverridePath: false
  };
}

async function main() {
  const resolved = resolveBinaryPath();
  const readyBinaryPath = await ensureBinary(resolved.path, {
    isOverridePath: resolved.isOverridePath
  });

  const child = spawn(readyBinaryPath, process.argv.slice(2), {
    stdio: "inherit"
  });

  child.on("error", (err) => {
    fail("Failed to run Lovelang binary: " + (err && err.message ? err.message : String(err)));
  });

  child.on("exit", (code) => {
    process.exit(code == null ? 1 : code);
  });
}

main().catch((err) => {
  fail(err && err.message ? err.message : String(err));
});
