"use strict";

function trimSlashes(value) {
  return String(value || "").replace(/^\/+|\/+$/g, "");
}

function buildDownloadUrl(options) {
  const opts = options || {};
  const assetName = String(opts.assetName || "").trim();
  if (!assetName) {
    throw new Error("assetName is required");
  }

  const baseUrl = String(opts.baseUrl || "").trim();
  if (baseUrl) {
    return baseUrl.replace(/\/+$/, "") + "/" + assetName;
  }

  const owner = trimSlashes(opts.owner);
  const repo = trimSlashes(opts.repo);
  if (!owner || !repo) {
    throw new Error("owner and repo are required when baseUrl is not set");
  }

  const tag = String(opts.tag || "latest").trim();
  const releasePath =
    tag && tag !== "latest"
      ? "download/" + encodeURIComponent(tag)
      : "latest/download";

  return (
    "https://github.com/" +
    owner +
    "/" +
    repo +
    "/releases/" +
    releasePath +
    "/" +
    assetName
  );
}

module.exports = {
  buildDownloadUrl
};
