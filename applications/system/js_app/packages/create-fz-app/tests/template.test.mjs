import assert from "node:assert/strict";
import { cp, mkdtemp, readFile, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { replaceInFileSync } from "replace-in-file";

const packageRoot = path.resolve(fileURLToPath(new URL("..", import.meta.url)));

test("creates a project template without unresolved placeholders", async () => {
  const fixtureRoot = await mkdtemp(
    path.join(os.tmpdir(), "tumoflip-create-fz-"),
  );
  const projectRoot = path.join(fixtureRoot, "sample-app");

  try {
    await cp(path.join(packageRoot, "template"), projectRoot, {
      recursive: true,
    });
    replaceInFileSync({
      files: `${projectRoot}/**/*`,
      from: /<app_name>/g,
      to: "sample-app",
    });

    const packageJson = JSON.parse(
      await readFile(path.join(projectRoot, "package.json"), "utf8"),
    );
    const config = await readFile(
      path.join(projectRoot, "fz-sdk.config.json5"),
      "utf8",
    );

    assert.equal(packageJson.name, "sample-app");
    assert.match(packageJson.scripts.build, /@darkflippers\/fz-sdk-ul/);
    assert.doesNotMatch(config, /<app_name>/);
    assert.match(config, /dist\/sample-app\.js/);
  } finally {
    await rm(fixtureRoot, { recursive: true, force: true });
  }
});

test("pins the audited template dependency", async () => {
  const packageJson = JSON.parse(
    await readFile(path.join(packageRoot, "package.json"), "utf8"),
  );
  assert.equal(packageJson.dependencies["replace-in-file"], "8.4.0");
});
