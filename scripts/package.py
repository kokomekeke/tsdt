import subprocess
import shutil
import logging
import argparse
import sys
from pathlib import Path
from datetime import datetime

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger("build")

def get_repo_identifier():
    try:
        repo_dir = Path(".")
        out = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=repo_dir,
            stderr=subprocess.DEVNULL,
        )
        return out.decode().strip()
    except Exception:
        return datetime.now().strftime("%Y%m%d_%H%M%S")



def run(cmd, cwd=None):
    logger.info("RUN: %s", " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


class BuildType:
    name = "base"

    def meson_args(self):
        return []


class Debug(BuildType):
    name = "debug"

    def meson_args(self):
        return ["--buildtype=debug"]


class Release(BuildType):
    name = "release"

    def meson_args(self):
        return ["--buildtype=release"]


class Platform:
    name = "base"

    def meson_flags(self) -> list[str]: ...
    def executables(self) -> list[str]: ...


class LinuxPlatform(Platform):
    name = "linux"

    def meson_flags(self):
        return [
            "-Db_lto=true",
            "-DSSG:use_oneapi=true",
            # these paths are container specific. TODO: get them from env
            "-DIntelOneAPI:oneAPIDevPath=/opt/intel/oneapi",
        ]
    def executables(self):
        return [
            "scg",
            "scgserver",
            "scgcommander",
            "mqtt_publisher",
            "mqtt_subscriber",
        ]


class WindowsPlatform(Platform):
    name = "windows"


    def meson_flags(self):
        vcpkg_dir = "S:/temp/3rdparty/scg_vcpkg_deps/vcpkg_installed/x64-windows"
        return [
            "-Db_vscrt=static_from_buildtype",
            "-DSSG:use_oneapi=true",
            "-DSSG:have_usrp=true",
            f"-Dcmake_prefix_path={vcpkg_dir}",
            f"-Ddevenv_paths={vcpkg_dir}/bin",
            # these paths are container specific. TODO: get them from env
            "-DIntelOneAPI:oneAPIDevPath=S:/temp/3rdparty/Intel oneAPI",
            "-DUHD:SgxExtRoot=S:/temp/3rdparty/sgx-ext-20240726",
        ]

    def executables(self):
        return [
            "scg.exe",
            "scgserver.exe",
            "scgcommander.exe",
            "mqtt_publisher.exe",
            "mqtt_subscriber.exe",
            "*.dll"
        ]


class MacOSPlatform(Platform):
    name = "macos"

    def meson_flags(self):
        return [
            "-DSSG:use_oneapi=false",
        ]
    def executables(self):
        return [
            "scg",
            "scgserver",
            "scgcommander",
            "mqtt_publisher",
            "mqtt_subscriber",
        ]


class BuildContext:
    def __init__(self, *, platform, build_type, id, outdir):
        self.platform: Platform = platform
        self.build_type = build_type
        self.id = id
        self.outdir = Path(outdir)

    @property
    def name(self):
        return f"{self.id}_{self.platform.name}_{self.build_type.name}"

    @property
    def pkg_dir(self):
        return self.outdir / self.name

    @property
    def build_root(self):
        return Path("build") / f"{self.platform.name}_{self.build_type.name}"




def meson_setup(ctx: BuildContext):
    run([
        sys.executable, "-m", "mesonbuild.mesonmain",
        "setup",
        "--wipe",
        str(ctx.build_root),
        *ctx.build_type.meson_args(),
        *ctx.platform.meson_flags(),
    ])


def meson_compile(ctx: BuildContext):
    run([sys.executable, "-m", "mesonbuild.mesonmain", "compile", "-C", str(ctx.build_root)])

def copy_assets(ctx):
    pass

def copy_executables(ctx: BuildContext):
    for pattern in ctx.platform.executables():
        # hacky, should be explicit.
        paths = ctx.build_root.rglob(pattern) if "*" in pattern else [ctx.build_root / pattern]
        for src in paths:
            if src.is_file():
                dst = ctx.pkg_dir / src.name
                shutil.copy2(src, dst)
                logger.info("Copied %s -> %s", src, dst)

    # Bundle vcpkg DLLs on Windows from LargeShare
    if ctx.platform.name == "windows":
        vcpkg_bin = Path("S:/temp/3rdparty/scg_vcpkg_deps/vcpkg_installed/x64-windows/bin")
        if vcpkg_bin.exists():
            for dll in vcpkg_bin.glob("*.dll"):
                dst = ctx.pkg_dir / dll.name
                shutil.copy2(dll, dst)
                logger.info("Copied vcpkg DLL %s -> %s", dll, dst)


def run_pipeline(ctx: BuildContext):
    if ctx.build_root.exists():
        shutil.rmtree(ctx.build_root)
    if ctx.pkg_dir.exists():
        shutil.rmtree(ctx.pkg_dir)

    ctx.pkg_dir.mkdir(parents=True, exist_ok=True)

    meson_setup(ctx)
    meson_compile(ctx)
    copy_executables(ctx)
    copy_assets(ctx)


def infer_platform():
    import platform as py_platform
    sys = py_platform.system().lower()

    if sys == "linux":
        return "linux"
    if sys == "windows":
        return "windows"
    if sys == "darwin":
        return "macos"

    raise RuntimeError(f"Unknown platform: {sys}")

def main():
    parser = argparse.ArgumentParser()
    detected_platform = infer_platform()

    parser.add_argument("--platform", choices=["linux", "windows", "macos"])
    parser.add_argument("--type", required=True, choices=["debug", "release"])
    parser.add_argument("--outdir", default="builds", help="Output directory for packaged artifacts")

    args = parser.parse_args()
    platform_name = args.platform or detected_platform
    logger.info("Detected platform: %s", detected_platform)
    logger.info("Using platform: %s", platform_name)
    logger.info("Output directory: %s", args.outdir)

    platform_map = {
        "linux": LinuxPlatform(),
        "windows": WindowsPlatform(),
        "macos": MacOSPlatform(),
    }

    build_type_map = {
        "debug": Debug(),
        "release": Release(),
    }

    id = get_repo_identifier()
    ctx = BuildContext(
        platform=platform_map[platform_name],
        build_type=build_type_map[args.type],
        id=id,
        outdir=args.outdir
    )

    run_pipeline(ctx)


if __name__ == "__main__":
    main()
