const { readFileSync } = require("node:fs");
const { join } = require("node:path");

exports.generateNotes = (_config, { cwd, nextRelease }) => {
  const source = readFileSync(join(cwd, "xmake.lua"), "utf8");
  const matches = [...source.matchAll(/^local version = "(\d+\.\d+\.\d+)"$/gm)];
  if (matches.length !== 1) {
    throw new Error(
      "Expected exactly one upstream numeric version in xmake.lua",
    );
  }
  const upstream = matches[0][1];
  return `Fork release **pt-v${nextRelease.version}**, based on upstream [v${upstream}](https://github.com/alandtse/devbench/releases/tag/v${upstream}).\n\nRuntime version: \`${upstream}+pt.${nextRelease.version}\`.`;
};
