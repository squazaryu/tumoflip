#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def copy_file(src: Path, dst: Path) -> None:
    if not src.is_file():
        raise FileNotFoundError(f"Missing source file: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    # FAT-formatted Flipper SD cards get noisy AppleDouble files when macOS
    # metadata is preserved. Avoid the platform copyfile API and stream bytes.
    with src.open("rb") as source, dst.open("wb") as target:
        shutil.copyfileobj(source, target)


def copy_tree(src: Path, dst: Path) -> None:
    if not src.is_dir():
        raise FileNotFoundError(f"Missing source directory: {src}")
    dst.mkdir(parents=True, exist_ok=True)
    for item in src.iterdir():
        target = dst / item.name
        if item.is_dir():
            copy_tree(item, target)
        else:
            copy_file(item, target)


def remove_appledouble(path: Path) -> None:
    if not path.exists():
        return
    for item in path.rglob("._*"):
        if item.is_file():
            item.unlink()


def deploy_arf_tools(repo_root: Path, sd_root: Path, build_dir: Path) -> None:
    arf_tools_dir = sd_root / "apps" / "ARF Tools"

    fap_src = repo_root / build_dir / ".extapps" / "proto_pirate.fap"
    copy_file(fap_src, arf_tools_dir / "ProtoPirate.fap")

    arf_tools_src = repo_root / build_dir / ".extapps" / "arf_tools.fap"
    copy_file(arf_tools_src, arf_tools_dir / "ARF Tools.fap")

    app_assets_dst = sd_root / "apps_assets" / "proto_pirate"
    plugins_dst = app_assets_dst / "plugins"
    plugins_dst.mkdir(parents=True, exist_ok=True)

    for plugin in (
        "protopirate_am_plugin.fal",
        "protopirate_fm_plugin.fal",
        "protopirate_emulate_plugin.fal",
        "protopirate_psa_bf_plugin.fal",
    ):
        copy_file(repo_root / build_dir / ".extapps" / plugin, plugins_dst / plugin)

    copy_tree(repo_root / "applications_user" / "protopirate" / "keystore", app_assets_dst)
    remove_appledouble(arf_tools_dir)
    remove_appledouble(app_assets_dst)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Deploy tumoflip ARF Tools SD applications and assets built by fbt."
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="Path to firmware repository root.",
    )
    parser.add_argument(
        "--sd-root",
        type=Path,
        required=True,
        help="Path to Flipper SD /ext root.",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path("build/f7-firmware-C"),
        help="Build directory containing .extapps.",
    )
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    sd_root = args.sd_root.resolve()
    deploy_arf_tools(repo_root, sd_root, args.build_dir)
    print(f"Deployed ARF Tools to {sd_root / 'apps' / 'ARF Tools'}")


if __name__ == "__main__":
    main()
