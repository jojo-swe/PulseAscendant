"""Check Evolution's process exit codes: 2 with a readable message when the GPU lacks a requirement,
1 for bad usage, 0 for a run that works. EVO_TEST_REQUIRE_DEVICE_EXTENSION makes the context require an
extension no driver has, so any machine looks unsupported."""

import os
import subprocess
import sys

MISSING = "VK_EVO_nope"
MESSAGE = "EVOLUTION needs a Vulkan 1.3 GPU with ray tracing"


def run(exe, args, extra_env=None):
    env = dict(os.environ)
    env.pop("EVO_TEST_REQUIRE_DEVICE_EXTENSION", None)
    env.update(extra_env or {})
    result = subprocess.run([exe, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
                            timeout=120)
    return result.returncode, result.stderr.decode(errors="replace")


def expect(exe, args, code, stderr_parts=(), extra_env=None):
    actual, stderr = run(exe, args, extra_env)
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


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: exit_code_test.py <Evolution executable>")
    main(sys.argv[1])
