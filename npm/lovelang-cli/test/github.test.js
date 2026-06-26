"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const { buildDownloadUrl } = require("../lib/github");

test("buildDownloadUrl uses latest release path by default", () => {
  const out = buildDownloadUrl({
    owner: "PATEL-KRISH-0",
    repo: "lovelang",
    assetName: "lovelang-linux-x64"
  });

  assert.equal(
    out,
    "https://github.com/PATEL-KRISH-0/lovelang/releases/latest/download/lovelang-linux-x64"
  );
});

test("buildDownloadUrl uses explicit tag path", () => {
  const out = buildDownloadUrl({
    owner: "PATEL-KRISH-0",
    repo: "lovelang",
    tag: "v1.2.3",
    assetName: "lovelang-darwin-arm64"
  });

  assert.equal(
    out,
    "https://github.com/PATEL-KRISH-0/lovelang/releases/download/v1.2.3/lovelang-darwin-arm64"
  );
});

test("buildDownloadUrl uses baseUrl override", () => {
  const out = buildDownloadUrl({
    baseUrl: "https://cdn.example.com/lovelang/",
    assetName: "lovelang-win32-x64.exe"
  });

  assert.equal(out, "https://cdn.example.com/lovelang/lovelang-win32-x64.exe");
});

test("buildDownloadUrl throws when assetName missing", () => {
  assert.throws(() => buildDownloadUrl({ owner: "PATEL-KRISH-0", repo: "lovelang" }), /assetName is required/);
});

test("buildDownloadUrl throws when owner/repo missing without baseUrl", () => {
  assert.throws(() => buildDownloadUrl({ assetName: "lovelang" }), /owner and repo are required/);
});
