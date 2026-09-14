"""Finds the built programs and the environment they need to run.

On windows the programs load dlls that do not sit next to them, mosquitto among them,
so running them straight out of the build directory fails before main is reached. The
environment meson devenv sets up has those directories in it, so that is what every
process the tests start is given.
"""

import os
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

_ASSIGNMENT = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)="(.*)"$')
_REFERENCE = re.compile(r"\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?")


class BuildError(RuntimeError):
    pass


def _expand(value, env):
    return _REFERENCE.sub(lambda m: env.get(m.group(1), ""), value)


def devenv_environment(build_dir):
    """The environment meson devenv sets up, or None when it cannot be asked for it."""
    meson = shutil.which("meson")
    if meson is None:
        return None
    try:
        result = subprocess.run(
            [meson, "devenv", "-C", str(build_dir), "--dump"],
            capture_output=True, text=True, errors="replace", timeout=180,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None

    env = dict(os.environ)
    assigned = False
    for line in result.stdout.splitlines():
        match = _ASSIGNMENT.match(line.strip())
        if match is None:
            continue
        env[match.group(1)] = _expand(match.group(2), env)
        assigned = True
    return env if assigned else None


def fallback_environment(build_dir):
    """Used when meson is not on the path. The dll directories are added by hand."""
    env = dict(os.environ)
    if os.name != "nt":
        return env
    directories = [str(build_dir)]
    for pattern in ("vcpkg_installed/*/bin", "vcpkg_installed/*/debug/bin"):
        directories += [str(p) for p in sorted(ROOT.glob(pattern)) if p.is_dir()]
    env["PATH"] = os.pathsep.join(directories + [env.get("PATH", "")])
    return env


class Build:
    """The programs in one meson build directory."""

    def __init__(self, build_dir):
        self.dir = Path(build_dir).resolve()
        if not self.dir.is_dir():
            raise BuildError(f"build directory not found: {self.dir}")
        self.env = devenv_environment(self.dir)
        self.from_devenv = self.env is not None
        if self.env is None:
            self.env = fallback_environment(self.dir)

    def exe(self, name):
        path = self.dir / (name + (".exe" if os.name == "nt" else ""))
        if not path.is_file():
            raise BuildError(f"{path} is missing, build it first")
        return path

    def run(self, name, args, timeout=120, cwd=None):
        return subprocess.run(
            [str(self.exe(name))] + [str(a) for a in args],
            env=self.env, cwd=str(cwd or ROOT),
            capture_output=True, text=True, errors="replace", timeout=timeout,
        )

    def popen(self, name, args, stdout, cwd=None):
        return subprocess.Popen(
            [str(self.exe(name))] + [str(a) for a in args],
            env=self.env, cwd=str(cwd or ROOT),
            stdout=stdout, stderr=subprocess.STDOUT,
        )
