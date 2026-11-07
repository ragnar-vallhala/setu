#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Setu's generated system sounds.

Every sound is synthesized here from a few notes and an instrument (no
samples), so the whole set can be rebuilt, retuned or lengthened. Each
scheme covers the standard Windows sound events, so a pack's .theme can list
them in [AppEvents\\Schemes\\Apps\\.Default\\<event>\\.Current]; the event
names are the documented ones (learn.microsoft.com, "Theme file format").

    soundgen.py build -o OUTDIR      OUTDIR/<scheme>/<event>.wav + sounds.ini,
                                     OUTDIR/<scheme>-demo.wav (every sound in turn)
    soundgen.py list

Output: 44.1 kHz, 16-bit stereo WAV, peaks at -3 dBFS (minimal: -9). Needs python3-numpy.
"""
import argparse
import os
import sys
import wave

import numpy as np

RATE = 44100
# All schemes play in D major pentatonic, so sounds from one scheme fit
# together. Notes are semitones above D4.
D4 = 293.66


def hz(semi):
    return D4 * 2 ** (semi / 12)


# ---------------------------------------------------------- instruments

def env(n, attack=0.004, decay=0.35):
    t = np.arange(n) / RATE
    a = np.clip(t / attack, 0, 1)
    return a * np.exp(-t / decay)


def bell(f, dur, decay=0.6, bright=1.0):
    """Inharmonic bell: partials at bell-like ratios, upper ones die faster."""
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    out = np.zeros(n)
    for ratio, amp, dk in ((1, 1, 1), (2.0, 0.45, 0.6), (2.76, 0.3 * bright, 0.4),
                           (5.4, 0.12 * bright, 0.25), (8.93, 0.05 * bright, 0.15)):
        out += amp * np.sin(2 * np.pi * f * ratio * t) * env(n, 0.002, decay * dk)
    return out


def marimba(f, dur, decay=0.28):
    """Wood bar: fundamental plus a 4x partial that fades fast, soft mallet."""
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    return (np.sin(2 * np.pi * f * t) * env(n, 0.003, decay)
            + 0.35 * np.sin(2 * np.pi * f * 3.99 * t) * env(n, 0.001, decay * 0.18)
            + 0.08 * np.sin(2 * np.pi * f * 9.2 * t) * env(n, 0.0005, 0.02))


def pluck(f, dur, decay=0.12):
    """Short soft tick for the minimal scheme."""
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    return np.sin(2 * np.pi * f * t + 0.6 * np.sin(2 * np.pi * f * 2 * t) * env(n, 0.001, 0.03)) * env(n, 0.002, decay)


def pad(f, dur):
    """Slow warm chord tone for start-up and log-on."""
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    a = np.clip(t / 0.35, 0, 1) * np.clip((dur - t) / 0.9, 0, 1)
    out = sum(np.sin(2 * np.pi * f * d * t) for d in (1.0, 1.003, 0.997)) / 3
    out += 0.3 * sum(np.sin(2 * np.pi * f * 2 * d * t) for d in (1.0, 1.004)) / 2
    return out * a


INSTR = {'bell': bell, 'marimba': marimba, 'pluck': pluck}


def seq(notes, instr, gap, dur=1.2, **kw):
    """notes: list of semitone values or tuples (chord); gap seconds between."""
    total = gap * (len(notes) - 1) + dur
    out = np.zeros(int(total * RATE) + 1)
    for i, chord in enumerate(notes):
        chord = chord if isinstance(chord, tuple) else (chord,)
        for s in chord:
            x = INSTR[instr](hz(s), dur, **kw) / len(chord) ** 0.5
            o = int(i * gap * RATE)
            out[o:o + len(x)] += x
    return out


def reverb(x, amount=0.25, length=1.2, seed=1):
    """Short convolution reverb from decaying noise (two channels, decorrelated)."""
    rng = np.random.default_rng(seed)
    n = int(length * RATE)
    t = np.arange(n) / RATE
    outs = []
    for ch in range(2):
        ir = rng.standard_normal(n) * np.exp(-t / (length / 5))
        ir[: int(0.012 * RATE)] = 0
        ir /= np.sqrt((ir ** 2).sum())
        m = len(x) + n - 1
        size = 1 << (m - 1).bit_length()
        wet = np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(ir, size), size)[:m]
        dry = np.concatenate([x, np.zeros(n - 1)])
        outs.append(dry + amount * wet)
    return np.stack(outs, 1)


# --------------------------------------------------------------- schemes
# event -> (notes, gap, dur, extra) per scheme. Pentatonic degrees in
# semitones: 0 D, 2 E, 4 F#, 7 A, 9 B, 12 D5 ...

def design(scheme):
    ins = {'setu-chimes': 'bell', 'setu-wood': 'marimba', 'setu-minimal': 'pluck'}[scheme]
    k = {'setu-chimes': 12, 'setu-wood': 0, 'setu-minimal': 12}[scheme]  # octave
    rv = {'setu-chimes': 0.35, 'setu-wood': 0.2, 'setu-minimal': 0.1}[scheme]
    d = 1.4 if ins == 'bell' else 0.8
    ev = {
        '.Default': ([k + 7], 0.1, d),
        'SystemAsterisk': ([k + 7, k + 12], 0.09, d),
        'SystemNotification': ([k + 9, k + 14], 0.11, d),
        'SystemQuestion': ([k + 4, k + 9], 0.12, d),
        'SystemExclamation': ([k + 9, k + 4], 0.11, d),
        'SystemHand': ([k + 2, k - 3, (k - 10, k - 3)], 0.12, d),       # critical stop: falling
        'MailBeep': ([k + 12, k + 16, k + 19], 0.07, d),
        'DeviceConnect': ([k + 7, k + 12], 0.08, d * 0.8),
        'DeviceDisconnect': ([k + 12, k + 7], 0.08, d * 0.8),
        'DeviceFail': ([k + 4, k + 3], 0.14, d * 0.8),
        'EmptyRecycleBin': ([k + 16, k + 12, k + 9, k + 7], 0.045, d * 0.6),
        'WindowsLogoff': ([k + 12, k + 9, k + 7, (k, k + 7)], 0.14, d * 1.3),
        'WindowsUAC': ([k + 4, k + 7], 0.1, d),
        'SystemExit': ([k + 12, k + 7, (k, k + 4)], 0.16, d * 1.4),
    }
    return ins, rv, ev


def startup(scheme):
    """The start-up / log-on sound: the 'bridge' motif — a rising arch and a
    landing chord — over a warm pad."""
    ins, rv, _ = design(scheme)
    k = 12 if ins != 'marimba' else 0
    line = seq([k, k + 4, k + 7, k + 12, k + 9, (k + 4, k + 7, k + 14)], ins,
               0.16, 2.2 if ins == 'bell' else 1.2)
    p = np.zeros(int(3.6 * RATE))
    for s in (-12, -5, 2, 7):
        x = pad(hz(s), 3.6)
        p[:len(x)] += 0.18 * x
    out = np.zeros(max(len(line), len(p)))
    out[:len(line)] += line
    out[:len(p)] += p if ins != 'pluck' else 0.5 * p
    return reverb(out, rv + 0.15, 2.0)


def finish(x, peak_db=-3):
    if x.ndim == 1:
        x = np.stack([x, x], 1)
    # trim trailing silence, fade the tail
    level = np.abs(x).max(1)
    last = np.nonzero(level > level.max() * 10 ** (-60 / 20))[0][-1] + 1
    x = x[:last]
    fade = min(len(x), int(0.05 * RATE))
    x[-fade:] *= np.linspace(1, 0, fade)[:, None]
    x *= 10 ** (peak_db / 20) / np.abs(x).max()
    return x


def write_wav(path, x):
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


SCHEMES = {
    'setu-chimes': 'glassy bells with a little room (the default)',
    'setu-wood': 'soft marimba, warm and short',
    'setu-minimal': 'quiet ticks, almost nothing',
}


def build(outdir):
    for scheme in SCHEMES:
        d = os.path.join(outdir, scheme)
        os.makedirs(d, exist_ok=True)
        ins, rv, ev = design(scheme)
        files = {}
        peak = -9 if scheme == 'setu-minimal' else -3
        demo = []
        for name, (notes, gap, dur) in ev.items():
            x = finish(reverb(seq(notes, ins, gap, dur), rv), peak)
            demo += [x, np.zeros((int(0.5 * RATE), 2))]
            fn = name.lstrip('.').lower() + '.wav'
            write_wav(os.path.join(d, fn), x)
            files[name] = fn
        s = finish(startup(scheme), peak)
        write_wav(os.path.join(outdir, scheme + '-demo.wav'), np.concatenate([s, np.zeros((RATE, 2))] + demo))
        write_wav(os.path.join(d, 'startup.wav'), s)
        files['WindowsLogon'] = 'startup.wav'
        files['SystemStart'] = 'startup.wav'
        with open(os.path.join(d, 'sounds.ini'), 'w') as f:
            f.write(f'; {scheme}: {SCHEMES[scheme]}. Generated by art/sounds/soundgen.py.\n'
                    '; event=file; a .theme lists each as\n'
                    '; [AppEvents\\Schemes\\Apps\\.Default\\<event>\\.Current] DefaultValue=DIR\\<file>\n'
                    '[Sounds]\n')
            for k, v in files.items():
                f.write(f'{k}={v}\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build'); b.add_argument('-o', '--out', required=True)
    sub.add_parser('list')
    a = ap.parse_args()
    if a.cmd == 'build':
        build(a.out)
    else:
        for k, v in SCHEMES.items():
            print(f'{k:13} {v}')
        print('events:', ' '.join(list(design('setu-chimes')[2]) + ['WindowsLogon', 'SystemStart']))


if __name__ == '__main__':
    sys.exit(main())
