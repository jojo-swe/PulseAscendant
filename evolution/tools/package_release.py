"""Pack a release: dist/EVOLUTION-v<version>-<os>.zip holding, in one EVOLUTION/ folder, the executable, its
compiled shaders, the show's music where the executable looks for it (assets/music/show.wav), the NFO, the
licence and the third-party notices.

    python3 tools/package_release.py --bin build/linux/bin
    python3 tools/package_release.py --bin build/windows/bin --os windows --version 1.0

The zip is reproducible: entries in a fixed order with a fixed date, the executable marked executable. The
music is git-ignored, so tools/prepare_show.py must have written assets/music/show.wav first.
"""

import argparse
import os
import platform
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # evolution/
REPO = os.path.dirname(ROOT)
STAMP = (2026, 10, 24, 0, 0, 0)


def add(archive, source, name, executable=False):
    info = zipfile.ZipInfo(f"EVOLUTION/{name}", date_time=STAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = (0o755 if executable else 0o644) << 16
    with open(source, "rb") as f:
        archive.writestr(info, f.read())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bin", required=True, help="the build's bin directory (Evolution and shaders/)")
    parser.add_argument("--os", choices=("windows", "linux"), default=platform.system().lower())
    parser.add_argument("--version", default="1.0")
    parser.add_argument("--out", default=os.path.join(ROOT, "dist"))
    args = parser.parse_args()

    exe_name = "Evolution.exe" if args.os == "windows" else "Evolution"
    exe = os.path.join(args.bin, exe_name)
    shaders = os.path.join(args.bin, "shaders")
    music = os.path.join(ROOT, "assets", "music", "show.wav")
    for path in (exe, shaders, music):
        if not os.path.exists(path):
            sys.exit(f"missing: {path}")

    files = [(exe, exe_name, True)]
    files += [(os.path.join(shaders, n), f"shaders/{n}", False) for n in sorted(os.listdir(shaders))
              if n.endswith(".spv")]
    files.append((music, "assets/music/show.wav", False))
    files.append((os.path.join(ROOT, "assets", "EVOLUTION.NFO"), "EVOLUTION.NFO", False))
    files.append((os.path.join(REPO, "LICENSE"), "LICENSE", False))
    for folder in (os.path.join(REPO, "licenses"), os.path.join(ROOT, "licenses")):
        files += [(os.path.join(folder, n), f"licenses/{n}", False) for n in sorted(os.listdir(folder))]

    names = [name for _, name, _ in files]
    if len(set(names)) != len(names):
        sys.exit("two files would share a name in the zip")
    os.makedirs(args.out, exist_ok=True)
    target = os.path.join(args.out, f"EVOLUTION-v{args.version}-{args.os}.zip")
    with zipfile.ZipFile(target, "w") as archive:
        for source, name, executable in files:
            add(archive, source, name, executable)
    print(f"{target}: {len(files)} files, {os.path.getsize(target) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
