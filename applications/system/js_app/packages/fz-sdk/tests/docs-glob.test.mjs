import assert from "node:assert/strict";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { createRequire } from "node:module";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { pathToFileURL } from "node:url";

const require = createRequire(import.meta.url);
const typedocRoot = path.dirname(require.resolve("typedoc/package.json"));
// This regression owns the one dependency adapter; it intentionally exercises
// the published TypeDoc implementation, not a copied matching algorithm.
const { glob, createGlobString, normalizePath } = await import(
  pathToFileURL(path.join(typedocRoot, "dist/node-utils.js"))
);

async function fixture(run) {
  const root = await mkdtemp(path.join(process.env.SDK_TEST_TEMP_ROOT ?? os.tmpdir(), "sdk-glob-"));
  try {
    for (const relative of ["alpha/index.d.ts", "alpha/nested/extra.d.ts",
      "beta/index.d.ts", "gamma/index.d.ts", "node_modules/pkg/index.d.ts"]) {
      const file = path.join(root, relative);
      await mkdir(path.dirname(file), { recursive: true });
      await writeFile(file, "export interface Example {}\n");
    }
    await run(normalizePath(root));
  } finally {
    await rm(root, { recursive: true, force: true });
  }
}

test("TypeDoc traverses partial matches before collecting nested declarations", async () => {
  await fixture(async (root) => {
    const files = glob(createGlobString(root, "alpha/**/*.d.ts"), root).sort();
    assert.deepEqual(files, [`${root}/alpha/index.d.ts`, `${root}/alpha/nested/extra.d.ts`]);
  });
});

test("TypeDoc keeps brace alternatives and rejects unrelated directories", async () => {
  await fixture(async (root) => {
    assert.deepEqual(glob(createGlobString(root, "{alpha,beta}/index.d.ts"), root).sort(),
      [`${root}/alpha/index.d.ts`, `${root}/beta/index.d.ts`]);
  });
});

test("TypeDoc excludes node_modules unless the entry point explicitly includes it", async () => {
  await fixture(async (root) => {
    assert.equal(glob(createGlobString(root, "**/index.d.ts"), root).length, 3);
    assert.deepEqual(glob(createGlobString(root, "node_modules/**/*.d.ts"), root),
      [`${root}/node_modules/pkg/index.d.ts`]);
  });
});

test("TypeDoc keeps legacy traversal for negated patterns and empty matchers", async () => {
  await fixture(async (root) => {
    const pattern = `!${createGlobString(root, "{alpha,beta}/**/index.d.ts")}`;
    assert.deepEqual(glob(pattern, root), [`${root}/alpha/nested/extra.d.ts`]);
    assert.deepEqual(glob("#comment", root), []);
    assert.deepEqual(glob("", root), []);
  });
});
