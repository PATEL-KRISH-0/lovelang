#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const { VENDOR_DIR } = require("../lib/paths");

async function main() {
  await fs.promises.rm(VENDOR_DIR, { recursive: true, force: true });
  console.log("[lovelang-cli] Removed " + VENDOR_DIR);
}

main().catch((err) => {
  console.error("[lovelang-cli] Clean failed: " + (err && err.message ? err.message : String(err)));
  process.exit(1);
});
