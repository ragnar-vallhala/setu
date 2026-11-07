# Generated system sounds

`soundgen.py` synthesizes every Setu system sound from a few notes and an
instrument model (bell, marimba, soft pluck), with no samples. All schemes
play in D major pentatonic, so any two sounds fit together. The start-up and
log-on sound is the "bridge" motif: a rising arch of notes that lands on a
chord over a warm pad.

| Scheme | Sound |
|---|---|
| setu-chimes | glassy bells with a little room (the default) |
| setu-wood | soft marimba, warm and short |
| setu-minimal | quiet ticks, 6 dB softer than the others |

Each scheme covers the standard Windows sound events (`.Default`,
`SystemAsterisk`, `SystemExclamation`, `SystemHand`, `SystemQuestion`,
`SystemNotification`, `MailBeep`, `DeviceConnect`, `DeviceDisconnect`,
`DeviceFail`, `EmptyRecycleBin`, `WindowsLogon`, `WindowsLogoff`,
`WindowsUAC`, `SystemStart`, `SystemExit`), so a pack's `.theme` can list
them under `[AppEvents\Schemes\...]` and Windows programs that play system
sounds get them.

    python3 soundgen.py list
    python3 soundgen.py build -o out     # out/<scheme>/*.wav, sounds.ini, out/<scheme>-demo.wav

The demo file plays the start-up sound, then every event in turn.

Original work, MIT OR Apache-2.0. Needs python3-numpy.
