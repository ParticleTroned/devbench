const assert = require("node:assert/strict");
const {
  existsSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} = require("node:fs");
const { tmpdir } = require("node:os");
const { basename, dirname, join, resolve } = require("node:path");
const test = require("node:test");

const { generateNotes } = require("../scripts/fork-release-notes.cjs");
const repositoryRoot = resolve(__dirname, "..");

function fixture(t, source) {
  const cwd = mkdtempSync(join(tmpdir(), "devbench-fork-release-"));
  writeFileSync(join(cwd, "xmake.lua"), source);
  t.after(() => {
    assert.equal(dirname(resolve(cwd)), resolve(tmpdir()));
    assert.ok(basename(cwd).startsWith("devbench-fork-release-"));
    rmSync(cwd, { recursive: true, force: true });
  });
  return cwd;
}

function pluginOptions(config, name) {
  const entry = config.plugins.find(
    (plugin) => Array.isArray(plugin) && plugin[0] === name,
  );
  assert.ok(entry, `Expected release plugin ${name}`);
  return entry[1];
}

test("an upstream update changes the base without changing fork identity", (t) => {
  const cwd = fixture(t, 'local version = "1.18.1"\n');
  const nextRelease = { version: "1.16.0" };
  const initial = generateNotes({}, { cwd, nextRelease });
  assert.ok(initial.includes("Fork release **pt-v1.16.0**"));
  assert.ok(initial.includes("/alandtse/devbench/releases/tag/v1.18.1"));
  assert.ok(initial.includes("`1.18.1+pt.1.16.0`"));

  writeFileSync(join(cwd, "xmake.lua"), 'local version = "1.19.0"\n');
  const updated = generateNotes({}, { cwd, nextRelease });
  assert.ok(updated.includes("Fork release **pt-v1.16.0**"));
  assert.ok(updated.includes("/alandtse/devbench/releases/tag/v1.19.0"));
  assert.ok(updated.includes("`1.19.0+pt.1.16.0`"));
  assert.ok(!updated.includes("1.18.1"));
});

test("release notes accept Windows line endings", (t) => {
  const source =
    '-- project\r\nlocal version = "1.18.1"\r\nset_version(version)\r\n';
  const cwd = fixture(t, source);
  const notes = generateNotes({}, { cwd, nextRelease: { version: "2.0.0" } });
  assert.ok(notes.includes("`1.18.1+pt.2.0.0`"));
  assert.equal(readFileSync(join(cwd, "xmake.lua"), "utf8"), source);
});

test("release notes reject missing, malformed, or ambiguous upstream versions", async (t) => {
  const cases = {
    missing: "set_project('devbench')\n",
    incomplete: 'local version = "1.18"\n',
    nonnumeric: 'local version = "one.18.1"\n',
    metadata: 'local version = "1.18.1+pt.1.16.0"\n',
    prerelease: 'local version = "1.18.1-beta"\n',
    duplicate: 'local version = "1.18.1"\nlocal version = "1.19.0"\n',
  };
  for (const [name, source] of Object.entries(cases)) {
    await t.test(name, (subtest) => {
      const cwd = fixture(subtest, source);
      assert.throws(
        () => generateNotes({}, { cwd, nextRelease: { version: "1.16.0" } }),
        /Expected exactly one upstream numeric version in xmake\.lua/,
      );
    });
  }
});

test("fork releases tag source commits without version commits", () => {
  const upstream = JSON.parse(
    readFileSync(join(repositoryRoot, ".releaserc"), "utf8"),
  );
  const fork = JSON.parse(
    readFileSync(join(repositoryRoot, ".releaserc.fork.json"), "utf8"),
  );

  assert.equal(upstream.tagFormat ?? "v${version}", "v${version}");
  assert.equal(fork.tagFormat, "pt-v${version}");
  assert.equal(
    fork.repositoryUrl,
    "https://github.com/ParticleTroned/devbench.git",
  );
  assert.deepEqual(pluginOptions(upstream, "@semantic-release/git").assets, [
    "xmake.lua",
  ]);
  const forkPlugins = fork.plugins.map((plugin) =>
    Array.isArray(plugin) ? plugin[0] : plugin,
  );
  assert.ok(!forkPlugins.includes("@semantic-release/git"));
  assert.ok(!forkPlugins.includes("@google/semantic-release-replace-plugin"));
  assert.equal(existsSync(join(repositoryRoot, "FORK_VERSION")), false);

  const upstreamFiles = pluginOptions(
    upstream,
    "@google/semantic-release-replace-plugin",
  ).replacements.flatMap((replacement) => replacement.files);
  assert.deepEqual(upstreamFiles, ["xmake.lua"]);
});

test("release builds fetch tags and use the new tag as their ref", () => {
  const releaseWorkflow = readFileSync(
    join(repositoryRoot, ".github/workflows/release.yaml"),
    "utf8",
  );
  const buildWorkflow = readFileSync(
    join(repositoryRoot, ".github/workflows/_build.yaml"),
    "utf8",
  );
  assert.match(
    releaseWorkflow,
    /ref: \${{ needs\.semantic-release\.outputs\.new_release_git_tag }}/,
  );
  assert.match(buildWorkflow, /fetch-depth: 0/);
});
