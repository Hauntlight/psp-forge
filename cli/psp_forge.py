"""PSP-Forge CLI Orchestrator.

Commands:
  psp-forge init <name> [--template 2d|3d]
  psp-forge cook [--force]
  psp-forge build [--release|--debug|--docker]
  psp-forge run [--emulator <path>]
  psp-forge clean
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from .config import load_config, create_default_config
from .cookers.texture import cook_texture
from .cookers.mesh import cook_mesh
from .cookers.audio import cook_audio

# Directory where psp-forge is located
FORGE_ROOT = Path(__file__).resolve().parent.parent
TEMPLATES_DIR = FORGE_ROOT / "cli" / "templates"
RUNTIME_DIR = FORGE_ROOT / "runtime"


def get_pspdev_path() -> str:
    """Returns PSPDEV directory path, defaulting to /usr/local/pspdev."""
    return os.environ.get("PSPDEV", "/usr/local/pspdev")


def setup_env() -> dict:
    """Sets up environment with PSPDEV/bin in PATH."""
    env = os.environ.copy()
    pspdev = get_pspdev_path()
    env["PSPDEV"] = pspdev
    bin_path = os.path.join(pspdev, "bin")
    if bin_path not in env.get("PATH", ""):
        env["PATH"] = f"{bin_path}:{env.get('PATH', '')}"
    return env


def cmd_init(args):
    """Initializes a new PSP-Forge project."""
    project_name = args.name
    template_type = args.template.lower()

    if template_type in ["2d", "2d_starter"]:
        src_template = TEMPLATES_DIR / "2d_starter"
    elif template_type in ["3d", "3d_runner"]:
        src_template = TEMPLATES_DIR / "3d_runner"
    else:
        print(f"[-] Error: Unknown template '{args.template}'. Choose '2d' or '3d'.")
        sys.exit(1)

    target_dir = Path(project_name).resolve()
    if target_dir.exists() and any(target_dir.iterdir()):
        print(f"[-] Error: Target directory '{project_name}' already exists and is not empty.")
        sys.exit(1)

    print(f"[+] Creating PSP project '{project_name}' with template '{template_type}'...")
    shutil.copytree(src_template, target_dir, dirs_exist_ok=True)

    # Customize psp.toml
    config_file = target_dir / "psp.toml"
    if config_file.exists():
        content = config_file.read_text(encoding="utf-8")
        content = content.replace("my_psp_game", project_name)
        title = project_name.replace("_", " ").title()
        content = content.replace("PSP Game Title", title)
        config_file.write_text(content, encoding="utf-8")

    # Generate .vscode/c_cpp_properties.json for IDE intellisense / clangd
    vscode_dir = target_dir / ".vscode"
    vscode_dir.mkdir(exist_ok=True)
    pspdev = get_pspdev_path()
    vscode_config = {
        "configurations": [
            {
                "name": "PSP",
                "includePath": [
                    "${workspaceFolder}/**",
                    f"{RUNTIME_DIR}/include/**",
                    f"{pspdev}/psp/include/**",
                    f"{pspdev}/psp/sdk/include/**"
                ],
                "defines": [
                    "PSP=1",
                    "__PSP__=1",
                    "_PSP=1",
                    "__psp__=1"
                ],
                "compilerPath": f"{pspdev}/bin/psp-gcc",
                "cStandard": "c99",
                "intelliSenseMode": "linux-gcc-x86"
            }
        ],
        "version": 4
    }
    (vscode_dir / "c_cpp_properties.json").write_text(json.dumps(vscode_config, indent=4), encoding="utf-8")

    print(f"[+] Project '{project_name}' initialized successfully!")
    print(f"    Next steps:")
    print(f"      cd {project_name}")
    print(f"      psp-forge cook")
    print(f"      psp-forge build")
    print(f"      psp-forge run")


def cmd_cook(args):
    """Scans and converts source assets into PSP binary formats."""
    try:
        cfg = load_config()
    except FileNotFoundError as e:
        print(f"[-] Error: {e}")
        sys.exit(1)

    input_dir = Path(cfg["assets"]["input_dir"])
    output_dir = Path(cfg["assets"]["output_dir"])

    if not input_dir.exists():
        print(f"[*] No asset directory found at '{input_dir}', skipping asset cooking.")
        return

    output_dir.mkdir(parents=True, exist_ok=True)
    force = getattr(args, "force", False)

    cooked_count = 0
    skipped_count = 0

    for root, _, files in os.walk(input_dir):
        rel_root = Path(root).relative_to(input_dir)
        target_sub = output_dir / rel_root
        target_sub.mkdir(parents=True, exist_ok=True)

        for fname in files:
            src_file = Path(root) / fname
            ext = src_file.suffix.lower()

            if ext in [".png", ".jpg", ".jpeg", ".bmp", ".tga"]:
                dst_file = target_sub / f"{src_file.stem}.tex"
                if not force and dst_file.exists() and dst_file.stat().st_mtime >= src_file.stat().st_mtime:
                    skipped_count += 1
                    continue
                print(f"[+] Cooking texture: {src_file} -> {dst_file}")
                cook_texture(str(src_file), str(dst_file), format_type="8888", swizzle=True)
                cooked_count += 1

            elif ext in [".obj"]:
                dst_file = target_sub / f"{src_file.stem}.p3d"
                if not force and dst_file.exists() and dst_file.stat().st_mtime >= src_file.stat().st_mtime:
                    skipped_count += 1
                    continue
                print(f"[+] Cooking mesh: {src_file} -> {dst_file}")
                cook_mesh(str(src_file), str(dst_file))
                cooked_count += 1

            elif ext in [".wav", ".mp3", ".ogg", ".flac", ".m4a"]:
                dst_file = target_sub / f"{src_file.stem}.snd"
                if not force and dst_file.exists() and dst_file.stat().st_mtime >= src_file.stat().st_mtime:
                    skipped_count += 1
                    continue
                print(f"[+] Cooking audio: {src_file} -> {dst_file}")
                cook_audio(str(src_file), str(dst_file), target_rate=44100, force_stereo=True)
                cooked_count += 1

    print(f"[+] Asset cooking complete: {cooked_count} cooked, {skipped_count} up-to-date.")


def cmd_build(args):
    """Compiles the PSP project and creates EBOOT.PBP."""
    if getattr(args, "clean", False):
        cmd_clean(args)

    # Ensure assets are cooked first
    cmd_cook(args)

    build_type = "Debug" if getattr(args, "debug", False) else "Release"
    use_docker = getattr(args, "docker", False)

    if use_docker:
        print("[+] Building with official PSPDEV Docker container...")
        cwd = os.getcwd()
        docker_cmd = [
            "docker", "run", "--rm",
            "-v", f"{cwd}:/work",
            "-w", "/work",
            "pspdev/pspdev:latest",
            "bash", "-c",
            f"psp-cmake -B build -DCMAKE_BUILD_TYPE={build_type} && cmake --build build"
        ]
        res = subprocess.run(docker_cmd)
        if res.returncode != 0:
            print("[-] Docker build failed.")
            sys.exit(res.returncode)
    else:
        env = setup_env()
        psp_cmake = shutil.which("psp-cmake", path=env["PATH"])
        if not psp_cmake:
            print("[-] Error: 'psp-cmake' not found in PATH or PSPDEV/bin.")
            print("    Make sure PSPDEV is installed or use --docker.")
            sys.exit(1)

        extra_flags = []
        if getattr(args, "error_handler", False):
            extra_flags.append("-DFORGE_ENABLE_ERROR_HANDLER=1")

        print(f"[+] Configuring project with psp-cmake ({build_type})...")
        cfg_cmd = ["psp-cmake", "-B", "build", f"-DCMAKE_BUILD_TYPE={build_type}"] + extra_flags
        res = subprocess.run(cfg_cmd, env=env)
        if res.returncode != 0:
            print("[-] Configuration failed.")
            sys.exit(res.returncode)

        print("[+] Compiling project...")
        build_cmd = ["cmake", "--build", "build"]
        res = subprocess.run(build_cmd, env=env)
        if res.returncode != 0:
            print("[-] Build failed.")
            sys.exit(res.returncode)

    eboot_candidates = [
        Path("build/EBOOT.PBP"),
        Path("EBOOT.PBP")
    ]
    eboot_found = next((p for p in eboot_candidates if p.exists()), None)
    if eboot_found:
        print(f"\n[★] SUCCESS: PSP EBOOT generated at '{eboot_found}' ({eboot_found.stat().st_size} bytes)")
    else:
        print("[!] Warning: Build succeeded, but EBOOT.PBP was not found in expected paths.")


def cmd_run(args):
    """Launches the compiled EBOOT on PPSSPP emulator."""
    try:
        cfg = load_config()
    except FileNotFoundError:
        cfg = {"deploy": {"emulator_bin": "PPSSPPQt"}}

    eboot_candidates = [
        Path("build/EBOOT.PBP"),
        Path("EBOOT.PBP")
    ]
    eboot_path = next((p for p in eboot_candidates if p.exists()), None)
    if not eboot_path:
        print("[-] Error: EBOOT.PBP not found. Run 'psp-forge build' first.")
        sys.exit(1)

    # Locate emulator
    emulator = None
    if getattr(args, "emulator", None):
        emu_arg = Path(args.emulator).expanduser().resolve()
        if emu_arg.is_dir():
            # User passed a directory, look for common executable names inside
            sub_candidates = ["ppsspp", "PPSSPP.AppImage", "PPSSPPQt", "PPSSPPSDL"]
            for sc in sub_candidates:
                target = emu_arg / sc
                if target.is_file() and os.access(target, os.X_OK):
                    emulator = str(target)
                    break
            if not emulator:
                print(f"[-] Error: Directory '{args.emulator}' specified, but no executable emulator found inside.")
                print(f"    Expected one of: {sub_candidates}")
                sys.exit(1)
        elif emu_arg.is_file():
            if not os.access(emu_arg, os.X_OK):
                print(f"[-] Error: File '{emu_arg}' is not executable. Run 'chmod +x {emu_arg}'.")
                sys.exit(1)
            emulator = str(emu_arg)
        else:
            found = shutil.which(args.emulator)
            if found:
                emulator = found
            else:
                print(f"[-] Error: Specified emulator '{args.emulator}' not found.")
                sys.exit(1)
    else:
        configured_emu = cfg["deploy"].get("emulator_bin", "ppsspp")
        candidates = [configured_emu, "ppsspp", "PPSSPP.AppImage", "PPSSPPQt", "PPSSPPSDL"]
        for emu_name in candidates:
            cand_path = Path(emu_name).expanduser()
            if cand_path.is_file() and os.access(cand_path, os.X_OK):
                emulator = str(cand_path.resolve())
                break
            found = shutil.which(emu_name)
            if found:
                emulator = found
                break

    if not emulator:
        # Check flatpak
        flatpak_bin = shutil.which("flatpak")
        if flatpak_bin:
            res = subprocess.run(["flatpak", "info", "org.ppsspp.PPSSPP"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if res.returncode == 0:
                emulator = "flatpak run org.ppsspp.PPSSPP"

    if not emulator:
        print("[-] PPSSPP emulator binary not found automatically in PATH.")
        print(f"    You can manually load the generated EBOOT file in PPSSPP:")
        print(f"    File: {eboot_path.resolve()}")
        print(f"    Or specify emulator path with: psp-forge run --emulator /path/to/ppsspp")
        return

    print(f"[+] Launching emulator: {emulator} {eboot_path}")
    if "flatpak run" in emulator:
        cmd = ["flatpak", "run", "org.ppsspp.PPSSPP", str(eboot_path.resolve())]
    else:
        cmd = [emulator, str(eboot_path.resolve())]
    subprocess.Popen(cmd)


def cmd_clean(args):
    """Cleans build and temporary files."""
    build_dir = Path("build")
    if build_dir.exists():
        print(f"[+] Removing '{build_dir}'...")
        shutil.rmtree(build_dir)
    print("[+] Clean complete.")


def main():
    parser = argparse.ArgumentParser(
        prog="psp-forge",
        description="PSP-Forge: Modern Dev Suite & Micro-Engine for Sony PSP"
    )
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # init
    p_init = subparsers.add_parser("init", help="Initialize a new PSP game project")
    p_init.add_argument("name", help="Name of the project")
    p_init.add_argument("--template", choices=["2d", "3d"], default="2d", help="Starter template (2d or 3d)")

    # cook
    p_cook = subparsers.add_parser("cook", help="Compile and optimize graphics, mesh, and audio assets")
    p_cook.add_argument("--force", action="store_true", help="Force recompilation of all assets")

    # build
    p_build = subparsers.add_parser("build", help="Compile C source and package EBOOT.PBP")
    p_build.add_argument("--clean", action="store_true", help="Remove build directory before compiling")
    p_build.add_argument("--release", action="store_true", help="Build with optimizations (default)")
    p_build.add_argument("--debug", action="store_true", help="Build with debug symbols")
    p_build.add_argument("--error-handler", action="store_true", help="Enable hardware blue-screen error handler on crash")
    p_build.add_argument("--docker", action="store_true", help="Build inside official PSPDEV Docker container")
    p_build.add_argument("--force", action="store_true", help="Force rebuild of cooked assets")

    # run
    p_run = subparsers.add_parser("run", help="Launch compiled game on PPSSPP emulator")
    p_run.add_argument("--emulator", help="Path to PPSSPP executable")

    # clean
    p_clean = subparsers.add_parser("clean", help="Remove build directory and cache files")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    commands = {
        "init": cmd_init,
        "cook": cmd_cook,
        "build": cmd_build,
        "run": cmd_run,
        "clean": cmd_clean,
    }

    commands[args.command](args)


if __name__ == "__main__":
    main()
