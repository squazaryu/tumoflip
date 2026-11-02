import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdir, mkdtemp, readFile, readdir, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repo = fileURLToPath(new URL("../../", import.meta.url));
const packages = path.join(repo, "applications/system/js_app/packages");
const temporary = await mkdtemp(path.join(process.env.SDK_TEST_TEMP_ROOT ?? os.tmpdir(), "sdk-runtime-"));

function run(command, args, cwd, environment = {}) {
  const result = spawnSync(command, args, {
    cwd,
    env: { ...process.env, ...environment },
    stdio: "inherit",
    timeout: 300_000,
  });
  if (result.error) throw result.error;
  assert.equal(result.status, 0, `${command} ${args.join(" ")} failed (${result.signal ?? result.status})`);
}

try {
  console.log(`Packed runtime check: ${process.version}`);
  for (const [directory, name, rootVariable, tests] of [
    ["fz-sdk", "@darkflippers/fz-sdk-ul", "SDK_PACKAGE_ROOT", ["sdk-build.test.mjs"]],
    ["create-fz-app", "@darkflippers/create-fz-app-ul", "WIZARD_PACKAGE_ROOT",
      ["template-files.test.mjs", "template.test.mjs"]],
  ]) {
    const source = path.join(packages, directory);
    const tarball = path.join(temporary, `${directory}.tgz`);
    const consumer = path.join(temporary, directory);
    run("pnpm", ["pack", "--out", tarball], source);
    console.log(`${directory} SHA256 ${createHash("sha256").update(await readFile(tarball)).digest("hex")}`);
    await mkdir(consumer);
    await writeFile(path.join(consumer, "package.json"), JSON.stringify({
      name: `test-${directory}-consumer`, version: "0.0.0", private: true,
    }));
    // A fresh consumer does not inherit the SDK's development overrides/patches.
    const installArgs = ["add", "--ignore-scripts", "--engine-strict"];
    if (process.env.SDK_TEST_PNPM_STORE) installArgs.push("--store-dir", process.env.SDK_TEST_PNPM_STORE);
    run("pnpm", [...installArgs, tarball], consumer);
    run("pnpm", ["audit", "--audit-level", "moderate"], consumer);
    const dependencies = await readdir(path.join(consumer, "node_modules/.pnpm"));
    assert.equal(dependencies.some((entry) =>
      /^(typedoc(?:-material-theme)?|markdown-it|minimatch|brace-expansion|glob|replace-in-file)@/.test(entry)),
    false, `${directory} packed runtime includes documentation or generic glob parsers`);
    run(process.execPath, ["--test", ...tests.map((file) => path.join(source, "tests", file))], consumer, {
      [rootVariable]: path.join(consumer, "node_modules", name),
      SDK_TEST_TEMP_ROOT: temporary,
    });
  }
} finally {
  await rm(temporary, { recursive: true, force: true });
}
