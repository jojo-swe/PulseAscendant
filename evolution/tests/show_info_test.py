"""Evolution --show-info prints the show as JSON without touching SDL video or Vulkan. Its duration and track
starts are the soundtrack's (music/show-audio.json, which tools/prepare_show.py writes), so the check holds for
whatever tracks the show was prepared with; its 31 chapters run in order from 0 to the end.

Usage: show_info_test.py <Evolution> <music/show-audio.json>
"""
import json
import subprocess
import sys


def main():
    exe, receipt = sys.argv[1], sys.argv[2]
    info = json.loads(subprocess.run([exe, "--show-info"], check=True, capture_output=True, text=True).stdout)
    with open(receipt, encoding="utf-8") as f:
        audio = json.load(f)
    failures = []
    duration = audio["frames"] / 48000
    if abs(info["duration"] - duration) > 1e-9:
        failures.append(f"duration {info['duration']} is not the soundtrack's {duration}")
    track_starts = [t["start"] for t in info["tracks"]]
    expected = [frames / 48000 for frames in audio["track_start_frames"]]
    if len(track_starts) != len(expected) or any(abs(a - b) > 1e-9 for a, b in zip(track_starts, expected)):
        failures.append(f"track starts {track_starts} are not the soundtrack's {expected}")
    chapters = info["chapters"]
    starts = [c["start"] for c in chapters]
    if len(chapters) != 31 or starts[0] != 0 or any(b <= a for a, b in zip(starts, starts[1:])):
        failures.append(f"chapter starts {starts} do not run in order from 0 over 31 chapters")
    for c in chapters:
        if not c["start"] < c["accent"] < c["start"] + c["duration"]:
            failures.append(f"{c['title']}: accent {c['accent']} outside the chapter")
    end = chapters[-1]["start"] + chapters[-1]["duration"]
    if abs(end - duration) > 1e-6:
        failures.append(f"the last chapter ends at {end}, not at {duration}")
    for failure in failures:
        print("FAIL:", failure)
    if failures:
        return 1
    print(f"show_info: {len(chapters)} chapters over {info['duration']} s, as music/show-audio.json says")
    return 0


if __name__ == "__main__":
    sys.exit(main())
