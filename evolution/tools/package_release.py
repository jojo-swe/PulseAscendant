"""Pack a release: dist/EVOLUTION-v<version>-<os>.zip holding, in one EVOLUTION/ folder, the executable, its
compiled shaders, the show's music where the executable looks for it (assets/music/show.wav), the NFO, the
licence (LICENSE) and the notices and third-party licences (licenses/), all from this project's own folder.

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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # the project root (tools/..)
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
    licence = os.path.join(ROOT, "LICENSE")
    notices = os.path.join(ROOT, "licenses")
    nfo = os.path.join(ROOT, "assets", "EVOLUTION.NFO")
    for path in (exe, shaders, music, licence, notices, nfo):
        if not os.path.exists(path):
            sys.exit(f"missing: {path}")

    files = [(exe, exe_name, True)]
    files += [(os.path.join(shaders, n), f"shaders/{n}", False) for n in sorted(os.listdir(shaders))
              if n.endswith(".spv")]
    files.append((music, "assets/music/show.wav", False))
    files.append((nfo, "EVOLUTION.NFO", False))
    files.append((licence, "LICENSE", False))
    files += [(os.path.join(notices, n), f"licenses/{n}", False) for n in sorted(os.listdir(notices))
              if os.path.isfile(os.path.join(notices, n))]

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
