"""Configuration parser and validator for psp.toml."""

import os
import tomllib
from typing import Any, Dict


DEFAULT_CONFIG_TEMPLATE = """[project]
name = "{name}"
title = "{title}"
id = "{id}"
version = "1.0.0"

[build]
entry = "src/main.c"
opt_level = "-O2"
alloc_heap_mb = 20

[assets]
input_dir = "assets"
output_dir = "build/assets"
autowatch = true

[deploy]
emulator_bin = "PPSSPPQt"
icon = "assets/icon0.png"
background = "assets/pic1.png"
"""


def load_config(config_path: str = "psp.toml") -> Dict[str, Any]:
    """Loads and validates a psp.toml configuration file."""
    if not os.path.exists(config_path):
        raise FileNotFoundError(f"Configuration file '{config_path}' not found.")

    with open(config_path, "rb") as f:
        cfg = tomllib.load(f)

    # Defaults
    if "project" not in cfg:
        cfg["project"] = {}
    cfg["project"].setdefault("name", "psp_game")
    cfg["project"].setdefault("title", "PSP Game")
    cfg["project"].setdefault("id", "ULUS99999")
    cfg["project"].setdefault("version", "1.0.0")

    if "build" not in cfg:
        cfg["build"] = {}
    cfg["build"].setdefault("entry", "src/main.c")
    cfg["build"].setdefault("opt_level", "-O2")
    cfg["build"].setdefault("alloc_heap_mb", 20)

    if "assets" not in cfg:
        cfg["assets"] = {}
    cfg["assets"].setdefault("input_dir", "assets")
    cfg["assets"].setdefault("output_dir", "build/assets")
    cfg["assets"].setdefault("autowatch", True)

    if "deploy" not in cfg:
        cfg["deploy"] = {}
    cfg["deploy"].setdefault("emulator_bin", "PPSSPPQt")

    return cfg


def create_default_config(
    output_path: str,
    name: str,
    title: str = None,
    game_id: str = "ULUS99999"
) -> str:
    """Creates a new psp.toml file with default settings."""
    if not title:
        title = name.replace("_", " ").title()

    content = DEFAULT_CONFIG_TEMPLATE.format(
        name=name,
        title=title,
        id=game_id
    )

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(content)

    return output_path
