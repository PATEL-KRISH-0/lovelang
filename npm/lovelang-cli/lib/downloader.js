"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { Readable } = require("node:stream");
const { pipeline } = require("node:stream/promises");

async function downloadToFile(url, destinationPath, options = {}) {
  if (typeof fetch !== "function") {
    throw new Error("Global fetch is not available. Use Node.js 18.17+.");
  }

  const response = await fetch(url, {
    method: "GET",
    headers: options.headers || {},
    redirect: "follow"
  });

  if (!response.ok) {
    throw new Error("Download failed with status " + response.status + " from " + url);
  }

  if (!response.body) {
    throw new Error("Download response did not include a body");
  }

  await fs.promises.mkdir(path.dirname(destinationPath), { recursive: true });
  const tmpPath = destinationPath + ".tmp";
  const output = fs.createWriteStream(tmpPath);

  try {
    await pipeline(Readable.fromWeb(response.body), output);
    await fs.promises.rename(tmpPath, destinationPath);
  } catch (err) {
    await fs.promises.rm(tmpPath, { force: true });
    throw err;
  }
}

module.exports = {
  downloadToFile
};
