"use strict";

const path = require("node:path");

const PACKAGE_ROOT = path.resolve(__dirname, "..");
const VENDOR_DIR = path.join(PACKAGE_ROOT, "vendor");
const BIN_DIR = path.join(VENDOR_DIR, "bin");

function getBinaryPath(executableName) {
  return path.join(BIN_DIR, executableName);
}

module.exports = {
  PACKAGE_ROOT,
  VENDOR_DIR,
  BIN_DIR,
  getBinaryPath
};
