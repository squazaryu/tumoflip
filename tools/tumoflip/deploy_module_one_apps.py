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


def remove_appledouble(path: Path) -> None:
    if not path.exists():
        return
    for item in path.rglob("._*"):
        if item.is_file():
            item.unlink()


def ensure_subghz_hopping_presets(sd_root: Path) -> None:
    settings_path = sd_root / "subghz" / "assets" / "setting_user"
    presets = ("AM650", "FM476", "FM12K")
    settings_path.parent.mkdir(parents=True, exist_ok=True)

    if settings_path.exists():
        content = settings_path.read_text(encoding="utf-8")
        if any(
            line.lstrip().startswith("Hopping_Preset:") for line in content.splitlines()
        ):
            return
        separator = "" if content.endswith("\n") else "\n"
        addition = "\n# Presets used for preset and combined hopping\n" + "".join(
            f"Hopping_Preset: {preset}\n" for preset in presets
        )
        settings_path.write_text(content + separator + addition, encoding="utf-8")
        return

    content = (
        "Filetype: Flipper SubGhz Setting File\n"
        "Version: 1\n"
        "Add_standard_frequencies: true\n\n"
        "# Presets used for preset and combined hopping\n"
        + "".join(f"Hopping_Preset: {preset}\n" for preset in presets)
    )
    settings_path.write_text(content, encoding="utf-8")


def deploy_arf_tools(repo_root: Path, sd_root: Path, build_dir: Path) -> None:
    arf_tools_dir = sd_root / "apps" / "ARF Tools"
    modules_dir = sd_root / "apps_data" / "arf_subghz_full" / "modules"

    arf_subghz_full_src = repo_root / build_dir / ".extapps" / "arf_subghz_full.fap"
    copy_file(arf_subghz_full_src, arf_tools_dir / "arf_subghz_full.fap")
    copy_file(
        repo_root / build_dir / ".extapps" / "arf_frequency_analyzer.fap",
        arf_tools_dir / "arf_frequency_analyzer.fap",
    )

    for appid in (
        "arf_keeloq",
        "arf_counter_bf",
        "arf_car_emulate",
        "arf_psa_decrypt",
        "arf_subghz_standard",
        "arf_status",
        "proto_pirate",
        "rolljam",
        "subghz_bruteforcer",
    ):
        copy_file(
            repo_root / build_dir / ".extapps" / f"{appid}.fap",
            modules_dir / f"{appid}.fap",
        )

    for old_app in arf_tools_dir.glob("*.fap"):
        if old_app.name not in {"arf_frequency_analyzer.fap", "arf_subghz_full.fap"}:
            old_app.unlink()
    stale_analyzer = modules_dir / "arf_frequency_analyzer.fap"
    if stale_analyzer.exists():
        stale_analyzer.unlink()

    ensure_subghz_hopping_presets(sd_root)
    remove_appledouble(arf_tools_dir)
    remove_appledouble(modules_dir)


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
