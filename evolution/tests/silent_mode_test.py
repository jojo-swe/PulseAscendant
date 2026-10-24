"""Check that EVOLUTION runs without an audio device (Review Focus 3). A windowed run under xvfb-run plays a
silent WAV of exactly the show's length, so the music itself is acceptable and only the audio driver
decides:
- SDL_AUDIODRIVER=nonexistent: exit 0 and "SILENT MODE: cannot start SDL audio" (the monotonic clock);
- SDL_AUDIODRIVER=dummy: exit 0 and "AUDIO: <the WAV>" (the music clock, played into nothing).

xvfb-run sends the command's stderr to stdout, so both streams are read as one.

Usage: silent_mode_test.py <Evolution executable> <output directory>"""

import json
import os
import struct
import subprocess
import sys

SAMPLE_RATE = 48000


def write_silent_wav(path, frames):
    """Stereo 48 kHz PCM16 silence. The samples are left as a hole in the file, so it costs no disk space."""
    data_bytes = frames * 4
    header = (b"RIFF" + struct.pack("<I", 36 + data_bytes) + b"WAVE" +
              b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, SAMPLE_RATE, SAMPLE_RATE * 4, 4, 16) +
              b"data" + struct.pack("<I", data_bytes))
    with open(path, "wb") as out:
        out.write(header)
        out.truncate(len(header) + data_bytes)


def run_windowed(exe, wav, driver):
    env = dict(os.environ, SDL_AUDIODRIVER=driver)
    command = ["xvfb-run", "-a", exe, "--validation", "--duration", "0.5", "--width", "320", "--height", "200",
               "--music", wav]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, timeout=120)
    output = result.stdout.decode(errors="replace")
    sys.stderr.write(output)
    return result.returncode, output


def expect(exe, wav, driver, status):
    code, output = run_windowed(exe, wav, driver)
    if code != 0:
        sys.exit(f"SDL_AUDIODRIVER={driver}: expected exit code 0, got {code}")
    if status not in output:
        sys.exit(f"SDL_AUDIODRIVER={driver}: the output lacks '{status}'")
    print(f"SDL_AUDIODRIVER={driver}: exits 0 with '{status}'")


def main(exe, out_dir):
    info = json.loads(subprocess.run([exe, "--show-info"], stdout=subprocess.PIPE, check=True, timeout=60).stdout)
    os.makedirs(out_dir, exist_ok=True)
    wav = os.path.join(out_dir, "silent_show.wav")
    write_silent_wav(wav, round(info["duration"] * SAMPLE_RATE))

    expect(exe, wav, "nonexistent", "SILENT MODE: cannot start SDL audio")
    expect(exe, wav, "dummy", "AUDIO: " + wav)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: silent_mode_test.py <Evolution executable> <output directory>")
    main(sys.argv[1], sys.argv[2])
