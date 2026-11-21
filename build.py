"""Build the SA_EE DLL into dist/ with 32-bit MinGW-w64."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parent
OBJ = ROOT / "obj"
DIST = ROOT / "dist"


def main():
    env = os.environ.copy()
    toolchain = env.get("MOE_MINGW")
    if not toolchain and env.get("LOCALAPPDATA"):
        portable = Path(env["LOCALAPPDATA"]) / "MOEE-BuildTools" / "mingw32" / "bin"
        if (portable / "g++.exe").is_file():
            toolchain = str(portable)
    if toolchain:
        env["PATH"] = toolchain + os.pathsep + env.get("PATH", "")
    def tool(name):
        search = toolchain if toolchain else env.get("PATH")
        exe = shutil.which(name, path=search)
        if not exe:
            sys.exit(f"{name} not found. Set MOE_MINGW to the i686 MinGW-w64 bin directory.")
        return exe

    compiler = tool("g++")
    windres = tool("windres")
    target = subprocess.check_output([compiler, "-dumpmachine"], env=env, text=True).strip()
    if target != "i686-w64-mingw32":
        sys.exit(f"Expected i686-w64-mingw32, got {target}")
    OBJ.mkdir(exist_ok=True)
    DIST.mkdir(exist_ok=True)

    source_dirs = sorted(p for p in (ROOT / "src").iterdir() if p.is_dir())
    includes = ["-I" + str(path) for path in source_dirs]
    flags = [
        "-O2", "-Wall", "-fno-exceptions", "-fno-rtti",
        "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
        "-fno-builtin-memset", "-fno-builtin-memcpy", "-fno-builtin-memmove",
        "-fno-builtin-strlen", "-fno-builtin-strcmp",
    ]

    def compile_source(source):
        target_file = OBJ / source.relative_to(ROOT / "src").with_suffix(".o")
        target_file.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [compiler, *flags, *includes, "-c", str(source), "-o", str(target_file)],
            cwd=ROOT, env=env, check=True,
        )
        return str(target_file)

    sources = sorted((ROOT / "src").rglob("*.cpp"))
    with ThreadPoolExecutor(max_workers=min(6, os.cpu_count() or 1)) as pool:
        objects = list(pool.map(compile_source, sources))
    resource = OBJ / "icons.res.o"
    subprocess.run(
        [windres, "-i", "src/render/icons.rc", "-o", str(resource)],
        cwd=ROOT, env=env, check=True,
    )
    dll = DIST / "SA_EE.dll"
    subprocess.run(
        [compiler, "-shared", "-o", str(dll), *objects, str(resource),
         "-nostdlib", "-lkernel32", "-luser32", "-lgdi32", "-limm32", "-lgcc", "-s",
         "-Wl,--enable-stdcall-fixup", "-Wl,--no-insert-timestamp", "-Wl,--entry,_DllMain@12",
         f"-Wl,-Map={OBJ / 'SA_EE.map'}"],
        cwd=ROOT, env=env, check=True,
    )
    print(f"Built {dll}")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
