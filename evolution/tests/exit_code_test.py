"""Check Evolution's process exit codes: 2 with a readable message when the GPU lacks a requirement,
1 for bad usage, 0 for a run that works. EVO_TEST_REQUIRE_DEVICE_EXTENSION makes the context require an
extension no driver has, so any machine looks unsupported.

With --windowed (needs xvfb-run) it checks only the window mode instead: the same message and exit code 2,
before any window opens."""

import os
import subprocess
import sys

MISSING = "VK_EVO_nope"
MESSAGE = "EVOLUTION needs a Vulkan 1.3 GPU with ray tracing"


def run(exe, args, extra_env=None, prefix=()):
    env = dict(os.environ)
    env.pop("EVO_TEST_REQUIRE_DEVICE_EXTENSION", None)
    env.update(extra_env or {})
    result = subprocess.run([*prefix, exe, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
                            timeout=120)
    stderr = result.stderr
    if prefix:  # xvfb-run sends the command's stderr to its stdout
        stderr += result.stdout
    return result.returncode, stderr.decode(errors="replace")


def expect(exe, args, code, stderr_parts=(), extra_env=None, prefix=()):
    actual, stderr = run(exe, args, extra_env, prefix)
    label = " ".join(args)
    if actual != code:
        sys.stderr.write(stderr)
        sys.exit(f"'{label}': expected exit code {code}, got {actual}")
    for part in stderr_parts:
        if part not in stderr:
            sys.stderr.write(stderr)
            sys.exit(f"'{label}': stderr lacks '{part}'")
    print(f"'{label}' exits {code}")


def main(exe):
    unsupported = {"EVO_TEST_REQUIRE_DEVICE_EXTENSION": MISSING}
    expect(exe, ["--device-info"], 2, (MESSAGE, MISSING), unsupported)
    expect(exe, ["--render-stream", "--duration", "1", "--width", "16", "--height", "16"], 2,
           (MESSAGE, MISSING), unsupported)
    expect(exe, ["--render-stream"], 1, ("--render-stream needs --duration greater than 0",))
    expect(exe, ["--device-info"], 0)


def main_windowed(exe):
    # xvfb-run exits with the status of the command it ran.
    expect(exe, ["--duration", "1", "--width", "320", "--height", "200"], 2, (MESSAGE, MISSING),
           {"EVO_TEST_REQUIRE_DEVICE_EXTENSION": MISSING}, prefix=("xvfb-run", "-a"))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[2] == "--windowed":
        main_windowed(sys.argv[1])
    elif len(sys.argv) == 2:
        main(sys.argv[1])
    else:
        sys.exit("usage: exit_code_test.py <Evolution executable> [--windowed]")
