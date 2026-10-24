"""The windowed loop and the per-frame code must never stall the GPU: the frame ring waits on its timeline
semaphore for the one frame it reuses, nothing more. In src/app/app.cpp and under src/render, src/text,
src/retro and src/rt, a vkQueueWaitIdle or vkDeviceWaitIdle, a Context::immediate( submit-and-wait, or a
FrameRing::waitAll( is allowed only on a line marked "// shutdown", "// resize" or "// setup" (built once,
before the first frame). Calls are looked for in the code, not in comments; the marker is the comment."""

import pathlib
import re
import sys

STALL = re.compile(r"\bvk(QueueWaitIdle|DeviceWaitIdle)\b|\bimmediate\(|\bwaitAll\(")
ALLOWED_MARKERS = ("// shutdown", "// resize", "// setup")
SCANNED_DIRS = ("render", "text", "retro", "rt")
SUFFIXES = (".cpp", ".hpp", ".h")


def offending_lines(text):
    """(line number, line) for each stalling call in the code part of a line without an allow-list marker."""
    failures = []
    for number, line in enumerate(text.splitlines(), 1):
        code = line.split("//", 1)[0]
        if STALL.search(code) and not any(marker in line for marker in ALLOWED_MARKERS):
            failures.append((number, line.strip()))
    return failures


def check_the_checker():
    assert offending_lines("    vkQueueWaitIdle(queue);") == [(1, "vkQueueWaitIdle(queue);")]
    assert offending_lines("x\nvkCheck(vkDeviceWaitIdle(device), \"vkDeviceWaitIdle\");")[0][0] == 2
    assert not offending_lines("vkCheck(vkDeviceWaitIdle(device), \"vkDeviceWaitIdle\"); // shutdown")
    assert not offending_lines("vkDeviceWaitIdle(device); // resize: the old image may still be read")
    assert offending_lines("ring.waitAll();") == [(1, "ring.waitAll();")]
    unmarked = "ring_.waitAll(); // the frames in flight"
    assert offending_lines("    " + unmarked) == [(1, unmarked)]
    assert not offending_lines("    ring_.waitAll(); // shutdown")
    assert offending_lines("ctx.immediate([&](VkCommandBuffer cmd) {")[0][0] == 1
    assert not offending_lines("ctx.immediate([&](VkCommandBuffer cmd) { // setup: once")
    assert not offending_lines("    // immediate() waits for the copy; so does waitAll()")  # not a call
    assert not offending_lines("ring.wait(ring.submit());")
    assert not offending_lines("intermediate(x); awaitAll(y);")  # other names


def sources(root):
    app = root / "src" / "app" / "app.cpp"
    if not app.is_file():
        sys.exit(f"missing {app}")
    files = [app]
    for name in SCANNED_DIRS:
        directory = root / "src" / name
        if not directory.is_dir():
            sys.exit(f"missing {directory}")
        files += sorted(path for path in directory.rglob("*") if path.suffix in SUFFIXES)
    return files


def main(root):
    check_the_checker()
    failures = []
    files = sources(root)
    for path in files:
        for number, line in offending_lines(path.read_text(encoding="utf-8")):
            failures.append(f"{path.relative_to(root)}:{number}: {line}")
    if failures:
        sys.exit("a GPU stall outside a shutdown, resize or setup path:\n  " + "\n  ".join(failures))
    print(f"no per-frame GPU stall in {len(files)} files")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: no_wait_idle_test.py <evolution source dir>")
    main(pathlib.Path(sys.argv[1]))
