"""Print the show's chapter list for a video description: one `M:SS TITLE` line per chapter from
`Evolution --show-info`, the first at 0:00. A chapter shorter than MIN_SECONDS joins the one after it on one
line (`0:00 BOOT / FLYBY`), since video sites ignore chapter lists with markers closer than that. Times are
rounded down to the second, so a marker never comes after its chapter has started.

    python3 tools/video_chapters.py --exe build/linux/bin/Evolution
    python3 tools/video_chapters.py --info show.json
"""

import argparse
import json
import subprocess
import sys

MIN_SECONDS = 10.0


def stamp(seconds):
    whole = int(seconds)
    return f"{whole // 60}:{whole % 60:02d}"


def chapter_lines(chapters):
    lines, group = [], []
    for chapter in chapters:
        group.append(chapter)
        if chapter["duration"] < MIN_SECONDS and chapter is not chapters[-1]:
            continue
        lines.append(f"{stamp(group[0]['start'])} {' / '.join(c['title'] for c in group)}")
        group = []
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--exe", help="the Evolution executable (runs --show-info)")
    source.add_argument("--info", help="a file holding --show-info's JSON")
    args = parser.parse_args()
    if args.exe:
        text = subprocess.run([args.exe, "--show-info"], stdout=subprocess.PIPE, check=True).stdout
    else:
        with open(args.info, "rb") as f:
            text = f.read()
    lines = chapter_lines(json.loads(text)["chapters"])
    if not lines[0].startswith("0:00 "):
        sys.exit("the first chapter does not start at 0:00")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
