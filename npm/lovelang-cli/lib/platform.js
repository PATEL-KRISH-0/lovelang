"use strict";

const TARGET_MATRIX = Object.freeze({
  darwin: Object.freeze({
    x64: Object.freeze({
      assetName: "lovelang-darwin-x64",
      executableName: "lovelang"
    }),
    arm64: Object.freeze({
      assetName: "lovelang-darwin-arm64",
      executableName: "lovelang"
    })
  }),
  linux: Object.freeze({
    x64: Object.freeze({
      assetName: "lovelang-linux-x64",
      executableName: "lovelang"
    }),
    arm64: Object.freeze({
      assetName: "lovelang-linux-arm64",
      executableName: "lovelang"
    })
  }),
  win32: Object.freeze({
    x64: Object.freeze({
      assetName: "lovelang-win32-x64.exe",
      executableName: "lovelang.exe"
    }),
    arm64: Object.freeze({
      assetName: "lovelang-win32-arm64.exe",
      executableName: "lovelang.exe"
    })
  })
});

function resolvePlatform(platform = process.platform, arch = process.arch) {
  const platformTargets = TARGET_MATRIX[platform];
  if (!platformTargets) {
    throw new Error("Unsupported platform: " + platform);
  }

  const target = platformTargets[arch];
  if (!target) {
    throw new Error("Unsupported architecture for " + platform + ": " + arch);
  }

  return {
    platform,
    arch,
    assetName: target.assetName,
    executableName: target.executableName
  };
}

function supportedTargets() {
  const out = [];
  for (const [platform, arches] of Object.entries(TARGET_MATRIX)) {
    for (const arch of Object.keys(arches)) {
      out.push(platform + "/" + arch);
    }
  }
  return out;
}

module.exports = {
  TARGET_MATRIX,
  resolvePlatform,
  supportedTargets
};
