import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const packageRoot = process.env.SDK_PACKAGE_ROOT
  ? path.resolve(process.env.SDK_PACKAGE_ROOT)
  : path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const temporaryRoot = process.env.SDK_TEST_TEMP_ROOT ?? os.tmpdir();
const sdkPath = path.join(packageRoot, "sdk.js");

async function buildFixture(minify) {
  const fixtureRoot = await mkdtemp(path.join(temporaryRoot, "tumoflip-js-sdk-"));
  const distDir = path.join(fixtureRoot, "dist");
  await mkdir(distDir);

  await writeFile(
    path.join(distDir, "index.js"),
    'import * as storage from "@darkflippers/fz-sdk-ul/storage";\n' +
      "export const storageModule = storage;\n",
  );
  await writeFile(
    path.join(fixtureRoot, "tsconfig.json"),
    JSON.stringify({
      compilerOptions: { module: "CommonJS", target: "ES2015" },
    }),
  );
  await writeFile(
    path.join(fixtureRoot, "fz-sdk.config.json5"),
    `{
            build: {
                output: "dist/app.js",
                minify: ${minify},
                enforceSdkVersion: true,
            },
        }`,
  );

  const result = spawnSync(process.execPath, [sdkPath, "build"], {
    cwd: fixtureRoot,
    encoding: "utf8",
  });

  try {
    assert.equal(
      result.status,
      0,
      `SDK build failed:\n${result.stdout}\n${result.stderr}`,
    );
    return await readFile(path.join(distDir, "app.js"), "utf8");
  } finally {
    await rm(fixtureRoot, { recursive: true, force: true });
  }
}

for (const minify of [false, true]) {
  test(`builds a ${minify ? "minified" : "readable"} compatible bundle`, async () => {
    const output = await buildFixture(minify);

    assert.match(output, /^checkSdkCompatibility\(1, 0\);/);
    assert.match(output, /let exports = \{\};/);
    assert.match(output, /@flipperdevices\/fz-sdk\/storage/);
    assert.doesNotMatch(output, /@darkflippers\/fz-sdk-ul\/storage/);
  });
}

test("pins the audited esbuild release", async () => {
  const packageJson = JSON.parse(
    await readFile(path.join(packageRoot, "package.json"), "utf8"),
  );
  assert.equal(packageJson.dependencies.esbuild, "0.28.1");
});

test("keeps documentation parsers out of the Node 18 SDK runtime", async () => {
  const packageJson = JSON.parse(
    await readFile(path.join(packageRoot, "package.json"), "utf8"),
  );
  assert.equal(packageJson.engines.node, ">=18.12");
  assert.equal(packageJson.dependencies.typedoc, undefined);
  assert.equal(packageJson.dependencies["typedoc-material-theme"], undefined);
});
