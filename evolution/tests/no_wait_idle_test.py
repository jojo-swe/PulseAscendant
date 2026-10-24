"""The windowed loop and the renderer must never stall the GPU per frame: the frame ring waits on its timeline
semaphore for the one frame it reuses, nothing more. A vkQueueWaitIdle or vkDeviceWaitIdle in src/app/app.cpp
or under src/render/ is allowed only on a line marked "// shutdown" or "// resize"."""

import pathlib
import re
import sys

WAIT_IDLE = re.compile(r"\bvk(QueueWaitIdle|DeviceWaitIdle)\b")
ALLOWED_MARKERS = ("// shutdown", "// resize")


def offending_lines(text):
    """(line number, line) for each wait-idle call without an allow-list marker."""
    return [(number, line.strip()) for number, line in enumerate(text.splitlines(), 1)
            if WAIT_IDLE.search(line) and not any(marker in line for marker in ALLOWED_MARKERS)]


def check_the_checker():
    assert offending_lines("    vkQueueWaitIdle(queue);") == [(1, "vkQueueWaitIdle(queue);")]
    assert offending_lines("x\nvkCheck(vkDeviceWaitIdle(device), \"vkDeviceWaitIdle\");")[0][0] == 2
    assert not offending_lines("vkCheck(vkDeviceWaitIdle(device), \"vkDeviceWaitIdle\"); // shutdown")
    assert not offending_lines("vkDeviceWaitIdle(device); // resize: the old image may still be read")
    assert not offending_lines("ring.waitAll();")


def sources(root):
    app = root / "src" / "app" / "app.cpp"
    if not app.is_file():
        sys.exit(f"missing {app}")
    files = [app]
    render = root / "src" / "render"
    if render.is_dir():
        files += sorted(path for path in render.rglob("*") if path.suffix in (".cpp", ".hpp", ".h"))
    return files


def main(root):
    check_the_checker()
    failures = []
    for path in sources(root):
        for number, line in offending_lines(path.read_text(encoding="utf-8")):
            failures.append(f"{path.relative_to(root)}:{number}: {line}")
    if failures:
        sys.exit("wait-idle outside a shutdown or resize path:\n  " + "\n  ".join(failures))
    print(f"no per-frame wait-idle in {len(sources(root))} files")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: no_wait_idle_test.py <evolution source dir>")
    main(pathlib.Path(sys.argv[1]))
