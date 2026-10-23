"""Synthesise the deterministic placeholder song: 134 BPM, 4/4, exactly 270 s of stereo 48 kHz PCM16.

It stands in for the real soundtrack so the whole audio pipeline and the timeline can be tested. The
sections are laid out so the authored chapter targets fall in musically distinct places (bars of
60/134*4 = 1.79104 s):

    bars   0-7    chip intro: square-wave lead, noise hats, no kick
    bars   8-31   groove: kick on every beat, snare on 2 and 4, saw bass on the chord roots
    bars  32-55   + arpeggio and pad chords changing every 4 bars
    bars  56-79   + brighter lead
    bars  80-87   breakdown: no kick, pad and a rising noise sweep
    bars  88-119  drop: kick, sub bass, wide saw chords; the loudest section
    bars 120-135  outro: the intro lead again, over a kick
    bars 136-end  pad only, fading out to silence at 270 s

Every drum is a synthesised transient. Tones are additive (band-limited). Fixed seed, so two runs write
identical files. Usage: python3 tools/make_placeholder.py <out.wav>
"""
import hashlib
import sys
import wave
from pathlib import Path

import numpy as np

SAMPLE_RATE = 48000
FRAMES = 12_960_000  # exactly 270.0 s
BPM = 134
BEAT = 60 / BPM
BAR = 4 * BEAT
SEED = 1993
PEAK_DBFS = -1.0

# A minor: Am, F, C, G, one chord per 4 bars. Pad voicings and bass roots as MIDI notes.
CHORDS = ((57, 60, 64), (53, 57, 60), (55, 60, 64), (55, 59, 62))
ROOTS = (33, 29, 36, 31)
# The intro lead: a two-bar motif of eighth notes (None = rest).
MOTIF = ((69, 72, 76, 72, 74, 72, 69, None), (67, 69, 72, 74, 76, 74, 72, 69))


def frequency(midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


def chord_index(bar):
    return (bar // 4) % len(CHORDS)


def seconds(bar, beat=0.0):
    return (bar * 4 + beat) * BEAT


class Mix:
    def __init__(self):
        self.left = np.zeros(FRAMES)
        self.right = np.zeros(FRAMES)

    def add(self, time, signal, gain, pan=0.0):
        """Add a mono signal at time (s); pan -1 = left, +1 = right (equal power)."""
        start = int(round(time * SAMPLE_RATE))
        if start >= FRAMES:
            return
        signal = signal[:FRAMES - start] * gain
        angle = (pan + 1) * np.pi / 4
        self.left[start:start + len(signal)] += signal * np.cos(angle) * np.sqrt(2)
        self.right[start:start + len(signal)] += signal * np.sin(angle) * np.sqrt(2)


def timeline(duration):
    return np.arange(int(round(duration * SAMPLE_RATE))) / SAMPLE_RATE


def envelope(t, attack=.005, decay=None, release=.01):
    """Linear attack, optional exponential decay, short linear release so notes never click."""
    shape = np.minimum(t / attack, 1)
    if decay is not None:
        shape = shape * np.exp(-t / decay)
    tail = np.clip((t[-1] - t) / release, 0, 1) if len(t) else t
    return shape * tail


def additive(freq, t, harmonics, odd_only=False, detune_cents=0.0):
    """Band-limited saw (all harmonics) or square (odd harmonics) at freq, 1/k amplitudes."""
    freq = freq * 2 ** (detune_cents / 1200)
    out = np.zeros_like(t)
    for k in range(1, harmonics + 1):
        if odd_only and k % 2 == 0:
            continue
        if k * freq >= 16000:
            break
        out += np.sin(2 * np.pi * k * freq * t) / k
    return out


def band_noise(rng, duration, low, high):
    """White noise restricted to [low, high) Hz by an FFT mask."""
    noise = rng.standard_normal(int(round(duration * SAMPLE_RATE)))
    spectrum = np.fft.rfft(noise)
    freqs = np.fft.rfftfreq(len(noise), 1 / SAMPLE_RATE)
    spectrum[(freqs < low) | (freqs >= high)] = 0
    filtered = np.fft.irfft(spectrum, len(noise))
    return filtered / np.abs(filtered).max()


def make_drums(rng):
    t = timeline(.4)
    # Kick: 55 Hz sine with a fast pitch drop from 165 Hz and an 80 ms decay.
    pitch = 55 + 110 * np.exp(-t / .03)
    kick = np.sin(2 * np.pi * np.cumsum(pitch) / SAMPLE_RATE) * envelope(t, .001, .08)
    # Snare: 120 ms of band-passed noise.
    snare_t = timeline(.12)
    snare = band_noise(rng, .12, 180, 6000) * envelope(snare_t, .001, .04)
    # Hat: 30 ms of high noise.
    hat_t = timeline(.03)
    hat = band_noise(rng, .03, 7000, 20000) * envelope(hat_t, .0005, .008, .003)
    return kick, snare, hat


def note(freq, duration, harmonics, odd_only=False, decay=None, detune_cents=0.0, attack=.005):
    t = timeline(duration)
    return additive(freq, t, harmonics, odd_only, detune_cents) * envelope(t, attack, decay)


def chip_lead(mix, first_bar, last_bar, gain):
    for bar in range(first_bar, last_bar + 1):
        for step, midi in enumerate(MOTIF[bar % 2]):
            if midi is not None:
                mix.add(seconds(bar, step / 2), note(frequency(midi), BEAT / 2 * .9, 9, True, .25), gain)


def bright_lead(mix, first_bar, last_bar, gain):
    pattern = (0, 2, 1, 2, 0, 1, 2, 1)
    for bar in range(first_bar, last_bar + 1):
        chord = CHORDS[chord_index(bar)]
        for step, index in enumerate(pattern):
            midi = chord[index] + 24
            mix.add(seconds(bar, step / 2), note(frequency(midi), BEAT / 2 * .9, 6, decay=.18), gain,
                    pan=.3 if step % 2 else -.3)


def arpeggio(mix, first_bar, last_bar, gain):
    pattern = (0, 1, 2, 1)
    for bar in range(first_bar, last_bar + 1):
        chord = CHORDS[chord_index(bar)]
        for step in range(16):
            midi = chord[pattern[step % 4]] + 12
            mix.add(seconds(bar, step / 4), note(frequency(midi), BEAT / 4 * .8, 7, True, .07), gain,
                    pan=-.5 + step % 4 / 3)


def pad(mix, first_bar, last_bar, gain, detune=4.0, fade=None):
    """Sustained saw chords, one per 4 bars (aligned to the progression), detuned left/right."""
    bar = first_bar
    while bar <= last_bar:
        length = min(4 - bar % 4, last_bar + 1 - bar)
        duration = length * BAR
        for midi in CHORDS[chord_index(bar)]:
            for pan, cents in ((-.8, -detune), (.8, detune)):
                tone = note(frequency(midi), duration, 6, detune_cents=cents, attack=.08)
                if fade is not None:
                    tone = tone * fade(seconds(bar) + timeline(duration))
                mix.add(seconds(bar), tone, gain, pan)
        bar += length


def bass(mix, first_bar, last_bar, gain):
    for bar in range(first_bar, last_bar + 1):
        root = frequency(ROOTS[chord_index(bar)])
        for beat in range(4):
            mix.add(seconds(bar, beat), note(root, BEAT * .85, 10, decay=.15), gain)


def sub_bass(mix, first_bar, last_bar, gain):
    for bar in range(first_bar, last_bar + 1):
        root = frequency(ROOTS[chord_index(bar)])
        for beat in range(4):
            mix.add(seconds(bar, beat), note(root, BEAT * .95, 1, attack=.01), gain)


def drums(mix, kit, first_bar, last_bar, kick=0.0, snare=0.0, hat=0.0):
    kick_sound, snare_sound, hat_sound = kit
    for bar in range(first_bar, last_bar + 1):
        for beat in range(4):
            if kick:
                mix.add(seconds(bar, beat), kick_sound, kick)
            if snare and beat in (1, 3):
                mix.add(seconds(bar, beat), snare_sound, snare)
            # Hats on the beat only. With equal eighth-note hats a 1.5-beat period fits the perfectly sharp
            # synthetic onsets as well as the beat (44.8 frames at 100 Hz is not a whole lag), and the
            # tracker settles on 89 BPM. Real recordings are not this regular.
            if hat:
                mix.add(seconds(bar, beat), hat_sound, hat, pan=.25)


def sweep(mix, rng, first_bar, last_bar, gain):
    """Rising noise: overlapping Hann frames whose low-pass cutoff and level climb to the drop."""
    duration = (last_bar + 1 - first_bar) * BAR
    size, hop = 4096, 2048
    count = int(duration * SAMPLE_RATE) // hop
    window = np.hanning(size)
    freqs = np.fft.rfftfreq(size, 1 / SAMPLE_RATE)
    out = np.zeros(count * hop + size)
    for i in range(count):
        progress = i / max(1, count - 1)
        cutoff = 300 * (12000 / 300) ** progress
        spectrum = np.fft.rfft(rng.standard_normal(size))
        spectrum[freqs >= cutoff] = 0
        out[i * hop:i * hop + size] += np.fft.irfft(spectrum, size) * window * progress ** 2
    out /= np.abs(out).max()
    mix.add(seconds(first_bar), out, gain)


def synthesise():
    rng = np.random.default_rng(SEED)
    kit = make_drums(rng)
    mix = Mix()
    # Intro: chip lead and hats only.
    chip_lead(mix, 0, 7, .22)
    drums(mix, kit, 0, 7, hat=.10)
    # Groove onwards: kick, snare, hats, saw bass.
    drums(mix, kit, 8, 79, kick=.75, snare=.30, hat=.10)
    bass(mix, 8, 79, .16)
    # Arpeggio and pad chords.
    arpeggio(mix, 32, 79, .07)
    pad(mix, 32, 79, .035)
    # Brighter lead.
    bright_lead(mix, 56, 79, .06)
    # Breakdown: pad and a rising sweep, no kick.
    pad(mix, 80, 87, .045)
    sweep(mix, rng, 80, 87, .30)
    # Drop: the loudest section.
    drums(mix, kit, 88, 119, kick=1.0, snare=.40, hat=.14)
    sub_bass(mix, 88, 119, .45)
    pad(mix, 88, 119, .07, detune=12.0)
    arpeggio(mix, 88, 119, .08)
    # Outro: the intro lead over a kick.
    chip_lead(mix, 120, 135, .22)
    drums(mix, kit, 120, 135, kick=.75, hat=.08)
    # Pad-only fade to silence at the end of the file.
    fade_start, fade_end = seconds(136), FRAMES / SAMPLE_RATE
    pad(mix, 136, int(np.ceil(fade_end / BAR)), .045,
        fade=lambda t: np.clip((fade_end - t) / (fade_end - fade_start), 0, 1))
    stereo = np.stack((mix.left, mix.right), axis=1)
    stereo *= 10 ** (PEAK_DBFS / 20) / np.abs(stereo).max()
    return np.round(stereo * 32767).astype('<i2')


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: make_placeholder.py <out.wav>')
    out = Path(sys.argv[1])
    pcm = synthesise()
    assert len(pcm) == FRAMES
    out.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(out), 'wb') as audio:
        audio.setparams((2, 2, SAMPLE_RATE, FRAMES, 'NONE', 'not compressed'))
        audio.writeframes(pcm.tobytes())
    digest = hashlib.sha256(out.read_bytes()).hexdigest()
    print(f'{out.name}: {FRAMES} frames, {FRAMES / SAMPLE_RATE:.1f} s, sha256 {digest}')


if __name__ == '__main__':
    main()
