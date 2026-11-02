import assert from "node:assert/strict";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from "node:fs";
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

function fixture(run) {
  const root = mkdtempSync(path.join(process.env.SDK_TEST_TEMP_ROOT ?? os.tmpdir(), "sdk-template-"));
  try { run(root); } finally { rmSync(root, { recursive: true, force: true }); }
}

test("template root is a literal path, not a bracket/brace glob", () => {
  fixture((root) => {
    const project = path.join(root, "project[1]{x}");
    mkdirSync(path.join(project, "nested"), { recursive: true });
    writeFileSync(path.join(project, "nested/file.txt"), "<app_name> <app_name>");
    const decoy = path.join(root, "project1x");
    mkdirSync(decoy);
    writeFileSync(path.join(decoy, "file.txt"), "<app_name>");
    replaceTemplatePlaceholders(project, "sample-$&-ёж");
    assert.equal(readFileSync(path.join(project, "nested/file.txt"), "utf8"), "sample-$&-ёж sample-$&-ёж");
    assert.equal(readFileSync(path.join(decoy, "file.txt"), "utf8"), "<app_name>");
  });
});

test("template replacement preserves surrounding binary bytes", () => {
  fixture((root) => {
    writeFileSync(path.join(root, "asset.bin"), Buffer.concat([
      Buffer.from([0xff, 0, 0xfe]), Buffer.from("<app_name>"), Buffer.from([0x80]),
    ]));
    replaceTemplatePlaceholders(root, "app");
    assert.deepEqual(readFileSync(path.join(root, "asset.bin")),
      Buffer.concat([Buffer.from([0xff, 0, 0xfe]), Buffer.from("app"), Buffer.from([0x80])]));
  });
});

test("hidden entries and links are not followed or rewritten", () => {
  fixture((root) => {
    const project = path.join(root, "project");
    mkdirSync(path.join(project, ".hidden"), { recursive: true });
    const outside = path.join(root, "outside.txt");
    writeFileSync(outside, "<app_name>");
    writeFileSync(path.join(project, ".hidden/config"), "<app_name>");
    symlinkSync(outside, path.join(project, "link.txt"));
    replaceTemplatePlaceholders(project, "app");
    assert.equal(readFileSync(outside, "utf8"), "<app_name>");
    assert.equal(readFileSync(path.join(project, ".hidden/config"), "utf8"), "<app_name>");
    assert.throws(() => replaceTemplatePlaceholders(path.join(project, "link.txt"), "app"));
  });
});

test("a symlink root cannot redirect writes outside the project", () => {
  fixture((root) => {
    const target = path.join(root, "target");
    mkdirSync(target);
    writeFileSync(path.join(target, "file.txt"), "<app_name>");
    symlinkSync(target, path.join(root, "project"));
    assert.throws(() => replaceTemplatePlaceholders(path.join(root, "project"), "app"));
    assert.equal(readFileSync(path.join(target, "file.txt"), "utf8"), "<app_name>");
  });
});

test("invalid arguments fail without modifying a template file", () => {
  fixture((root) => {
    const file = path.join(root, "file.txt");
    writeFileSync(file, "<app_name>");
    assert.throws(() => replaceTemplatePlaceholders(root, null), TypeError);
    assert.throws(() => replaceTemplatePlaceholders(file, "app"), TypeError);
    assert.equal(readFileSync(file, "utf8"), "<app_name>");
  });
});
