"""The release's NFO (assets/EVOLUTION.NFO): 80 columns (the closing line excepted, kept as written), the
notes paragraph's seven lines and what their first characters spell, the closing line, the credits and the
greetings; and the README, which leaves the NFO's last line to the NFO.

    python3 tests/nfo_test.py <evolution source dir>
"""

import os
import sys

CLOSING = "there is a secret part. the prompt knows the word; so does the directory listing."
CREDITS = "CODE: JOJO · MUSIC: JOYOLO · CONCEPT: JOHN"
QUIET = ("secret", "clap", "applause", "coda", "patient", "easter egg", "hidden part")


def fail(message):
    sys.exit(f"FAILED: {message}")


def main(root):
    with open(os.path.join(root, "assets", "EVOLUTION.NFO"), encoding="utf-8", newline="") as f:
        text = f.read()
    if "\r\n" not in text or "\n" in text.replace("\r\n", ""):
        fail("the NFO's lines end in CR LF, every one")
    lines = text.split("\r\n")
    if lines[-1] != "":
        fail("the NFO ends with a line break")
    lines = lines[:-1]
    if lines[-1] != CLOSING:
        fail(f"the last line is {lines[-1]!r}")
    for number, line in enumerate(lines[:-1], 1):
        if len(line) > 80:
            fail(f"line {number} has {len(line)} columns")
        if line != line.rstrip():
            fail(f"line {number} has trailing blanks")
    if not any(line.strip() == CREDITS for line in lines):
        fail("the credits line is missing")
    if "FUTURE CREW" not in text or text.index("FUTURE CREW") > text.index("APPROXIMATE"):
        fail("the greetings start with Future Crew and end with Approximate")

    start = next(i for i, line in enumerate(lines) if "[ NOTES ]" in line) + 1
    notes = []
    for line in lines[start:]:
        if not line:
            break
        notes.append(line)
    if len(notes) != 7:
        fail(f"the notes have {len(notes)} lines, not 7")
    initials = "".join(line[0] for line in notes)
    if initials != "STAR IT":
        fail(f"the notes' initials read {initials!r}")

    with open(os.path.join(root, "README.md"), encoding="utf-8") as f:
        readme = f.read().lower()
    for word in QUIET:
        if word in readme:
            fail(f"the README mentions {word!r}")
    print(f"nfo: {len(lines)} lines, 7 notes, credits, greetings and the closing line; the README keeps quiet")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: nfo_test.py <evolution source dir>")
    main(sys.argv[1])
