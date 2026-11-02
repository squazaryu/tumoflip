import assert from "node:assert/strict";
import { cp, mkdtemp, readFile, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath, pathToFileURL } from "node:url";

const packageRoot = process.env.WIZARD_PACKAGE_ROOT
  ? path.resolve(process.env.WIZARD_PACKAGE_ROOT)
  : path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const { replaceTemplatePlaceholders } = await import(
  pathToFileURL(path.join(packageRoot, "template-files.js"))
);
const temporaryRoot = process.env.SDK_TEST_TEMP_ROOT ?? os.tmpdir();

test("creates a project template without unresolved placeholders", async () => {
  const fixtureRoot = await mkdtemp(
    path.join(temporaryRoot, "tumoflip-create-fz-"),
  );
  const projectRoot = path.join(fixtureRoot, "sample-app");

  try {
    await cp(path.join(packageRoot, "template"), projectRoot, {
      recursive: true,
    });
    replaceTemplatePlaceholders(projectRoot, "sample-app");

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

test("does not pull a generic glob/replacement runtime into the wizard", async () => {
  const packageJson = JSON.parse(
    await readFile(path.join(packageRoot, "package.json"), "utf8"),
  );
  assert.equal(packageJson.engines.node, ">=18.12");
  assert.equal(packageJson.dependencies["replace-in-file"], undefined);
  const entry = await readFile(path.join(packageRoot, "index.js"), "utf8");
  assert.match(entry, /replaceTemplatePlaceholders\(name, name\)/);
});
