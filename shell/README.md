# shell/: Setu desktop shell

**Status: stub.** License: MIT OR Apache-2.0.

An Explorer-style desktop: taskbar, start menu, file manager, system tray,
and launching `.exe` / `.msi` directly.

Plan (docs/ARCHITECTURE.md §3.4): the shell is a **Win32 application** built
with mingw-w64 and running on the Setu personality, on top of a minimal
Wayland compositor. It uses only public, documented APIs (`ShellExecuteExW`,
`IShellLink`, …), so it's also a large-scale test of the personality.

First target (Milestone 1): a taskbar with a launcher that can start
`notepad.exe` and run an `.msi` through `msiexec`.
