# Setu theme packs

Everything that decides how Setu looks is data, not code: colours, fonts,
sizes and corner radii, icons, the control theme, the wallpaper, the cursor,
the boot splash and the installer branding. A **theme pack** holds one
complete set; switching packs changes the whole look. Code keeps only a
built-in copy of the default pack's values as a fallback, so nothing breaks
when a pack is missing or incomplete.

docs/DESIGN.md describes the default pack (`setu-light`). This file is the
format every pack follows and the contract every component reads.

## Where packs live

| Path | Who |
|---|---|
| `distro/themes/<id>/` | source of the packs shipped with Setu (SVGs, scripts, theme.ini) |
| `C:\Windows\Resources\Themes\<id>\` | installed, built packs (Windows' own theme folder, so the `.theme` file is where Windows programs expect it) |
| `%LOCALAPPDATA%\Setu\Themes\<id>\` | packs a user adds |

`<id>` is lowercase ASCII, digits and `-` (e.g. `setu-light`, `setu-dark`).
A user pack with the same id as a system pack wins.

The active pack: `HKCU\Software\Setu\Theme`, value `Current` (REG_SZ, the
id). Missing means `setu-light`.

Content that packs share is installed once, next to the packs:

| Path | What | Named by |
|---|---|---|
| `C:\Windows\Resources\Setu\Icons\<set>\` | icon sets (`index.ini` + ICOs) | `[Icons] set=` |
| `C:\Windows\Web\Wallpaper\Setu\<style>-<palette>.jpg` | the generated wallpaper collection (4K) | `[Wallpaper] collection=` |
| `C:\Windows\Cursors\Setu\<set>\` | cursor sets (`.cur`, `.ani`) | `[Cursor] theme=` |
| `C:\Windows\Media\Setu\<scheme>\` | sound schemes (`.wav`) | `[Sounds] scheme=` |

On Setu's images the system packs and this content are links into
`/usr/share/setu/` that `setu-session` makes at every login
(distro/README.md, "Theme packs"), so they are read-only from Windows.
A user's own icon sets go in `%LOCALAPPDATA%\Setu\Icons\<set>\` and win
over the system's.

## Layout of a built pack

```
<id>/
  <id>.theme          Windows theme file (INI) + a [Setu] section
  theme.ini           Setu tokens (colours, fonts, metrics), see below
  icons/
    index.ini         icon names -> files, see "Icons"
    <name>.ico        multi-size icons (16, 20, 24, 32, 48, 64, 256 px)
    <name>.svg        source, kept for rebuilding at other sizes (optional)
  visual/<id>.msstyles   control theme generated from theme.ini (optional:
                         a pack may name another pack's msstyles instead)
  wallpaper/          wallpaper.png (4K) and wallpaper.bmp, or a set by size
  cursors/            optional; or a cursor theme name in theme.ini
  boot/               optional Plymouth assets (logo.png, background colour
                      comes from theme.ini)
  installer/          optional Calamares branding images
  preview.png         640x400 preview for a picker
  LICENSE / NOTICE    licences of everything in the pack
```

Source packs in `distro/themes/<id>/` hold `theme.ini`, `icons/*.svg`,
artwork scripts and the `.theme` template; the image build renders the ICOs,
the wallpaper and the msstyles into the built layout above.

## `<id>.theme`

Standard Windows theme file format
(https://learn.microsoft.com/en-us/windows/win32/controls/themesfileformat-overview),
so Windows programs and tools that read or apply `.theme` files see a normal
theme:

```ini
[Theme]
DisplayName=Setu Light

[Control Panel\Desktop]
Wallpaper=%WinDir%\Resources\Themes\setu-light\wallpaper\wallpaper.bmp
WallpaperStyle=10
TileWallpaper=0

[VisualStyles]
Path=%WinDir%\Resources\Themes\setu-light\visual\setu-light.msstyles
ColorStyle=NormalColor
Size=NormalSize

[Control Panel\Colors]
; COLOR_* values as "R G B", generated from theme.ini

[Control Panel\Cursors]
; optional: the cursor set's files, e.g.
DefaultValue=setu-light
Arrow=%WinDir%\Cursors\Setu\setu-light\arrow.cur
Wait=%WinDir%\Cursors\Setu\setu-light\wait.ani
; ...

[AppEvents\Schemes\Apps\.Default\SystemAsterisk\.Current]
; optional, one section per sound event of the scheme
DefaultValue=%WinDir%\Media\Setu\setu-chimes\systemasterisk.wav

[MasterThemeSelector]
MTSM=RJSPBS

[Setu]
Tokens=theme.ini
Icons=icons\index.ini
```

## `theme.ini` (Setu tokens)

Every key is optional; a missing key falls back to the default pack's
value. Colours are `#RRGGBB` or `#RRGGBBAA`; sizes are pixels at 96 DPI
(components scale them by the DPI).

```ini
[Theme]
Id=setu-light
Name=Setu Light
Version=1
Mode=light                ; light | dark (lets apps and the shell pick)
Base=                     ; optional: id of a pack to inherit unset keys from

[Colors]
accent=#1F6FB2
accent-hover=#2A7DC3
accent-pressed=#195E97
accent-text=#FFFFFF
window-bg=#F3F3F3
surface=#FFFFFF
surface-hover=#EAEAEA
border=#D1D1D1
border-subtle=#E5E5E5
text=#1B1B1B
text-secondary=#5F5F5F
text-disabled=#A0A0A0
titlebar-active=#FFFFFF
titlebar-inactive=#F3F3F3
titlebar-text=#1B1B1B
titlebar-text-inactive=#5F5F5F
close-hover=#C42B1C
taskbar=#EEF1F5
taskbar-text=#1B1B1B
menu=#FFFFFF
selection=#1F6FB233       ; list selection fill
selection-border=#1F6FB2
shadow=#00000033
desktop=#1E4F8A           ; behind the wallpaper / no wallpaper

[Fonts]
ui=Inter                  ; face list, first installed wins: "Inter, Noto Sans"
ui-size=9                 ; points
caption-weight=600
title-size=10.5
header-size=14
mono=JetBrains Mono, Noto Sans Mono
smoothing=grayscale       ; none | grayscale | rgb

[Metrics]
radius-window=8
radius-control=4
radius-check=3
caption-height=30
caption-button-width=46
taskbar-height=48
taskbar-button=40
taskbar-icon=24
row-height=32
menu-item-height=30
control-height=28
scrollbar-width=6
scrollbar-width-hover=12
focus-width=2
shadow-blur=12

[Shell]
taskbar-alignment=center  ; center | left
start-columns=6
start-rows=4

[Cursor]
theme=setu-light          ; a cursor set: the X cursor theme of that name
                          ; (labwc/Xwayland) and, if C:\Windows\Cursors\Setu\
                          ; has a set of that name, its .cur/.ani files
size=24

[Sounds]
scheme=setu-chimes        ; C:\Windows\Media\Setu\<scheme>\ (sounds.ini maps
                          ; events to files)

[Wallpaper]
style=10                  ; WallpaperStyle (10 = fill)
; the picture, from the first pack in the Base= chain that names one:
collection=ridges-setu-dark   ; an item of the collection: <style>-<palette>
seed=1                        ; optional (default 1)
; or, in a source pack, script=artwork/wallpaper.py (drawn at build time;
; the built pack then has wallpaper\wallpaper.bmp). The .theme names a
; BMP or the collection JPEG; setu-theme converts a non-BMP wallpaper to
; %LOCALAPPDATA%\Setu\Wallpaper\<name>.bmp, since the desktop loads BMPs.

[Icons]
set=setu-colour           ; an icon set, see "Icons"

[Boot]
background=#F3F3F3
logo=boot/logo.png
```

New keys may be added; components ignore keys they don't know. Changing a
key's meaning needs a new key name.

## Icons

A pack names an icon set with `[Icons] set=<name>` (setu-line, setu-bold,
setu-duotone, setu-solid, setu-colour from art/icons; or a user's set). A
set named at some level of the `Base=` chain beats the `icons\index.ini` of
that level and its bases, but a child pack's own `icons\index.ini` still
wins, so a pack can replace single icons of its set. Set lookup:
`%LOCALAPPDATA%\Setu\Icons\<set>\index.ini`, then
`%WINDIR%\Resources\Setu\Icons\<set>\index.ini`. Built packs also carry
the icons of their set in their own `icons\`, so paths into a pack stay
valid. `setu-app` is a full-colour tile (the start button and installer
logo) and is never recoloured.

`icons/index.ini` maps names to files:

```ini
[Icons]
folder=folder.ico
folder-open=folder-open.ico
file=file.ico
file-text=file-text.ico
drive-fixed=drive-fixed.ico
drive-removable=drive-removable.ico
recycle-bin-empty=trash.ico
recycle-bin-full=trash-full.ico
...
```

Names follow the freedesktop Icon Naming Specification where one fits
(https://specifications.freedesktop.org/icon-naming-spec/latest/), plus
Setu names for its UI glyphs (`cmd-new-folder`, `cmd-cut`, `cmd-copy`,
`cmd-paste`, `cmd-rename`, `cmd-delete`, `nav-back`, `nav-forward`, `nav-up`,
`refresh`, `view-details`, `view-icons`, `search`, `power`, `user`,
`chevron-right`, ...). The full list of names Setu uses is
`distro/themes/ICONS.md`, kept up to date by whoever adds a name.

Who uses them:
- setu-shell and setu-files look up every icon and glyph by name in the
  active pack, falling back to their built-in Lucide copies.
- Windows programs get the pack's folder, drive and file-type icons through
  the documented `Shell Icons` registry key
  (`HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Icons`,
  index -> "path,index"), which the apply step writes; shell32 already reads
  it. App icons stay the apps' own.

Icons that are glyphs (drawn in a text colour) are monochrome; the shell
recolours them to the matching token. Full-colour icons are used as they are.

## Applying a pack

`setu-theme.exe` (in shell/) is the one tool that switches packs:

```
setu-theme list                 ids, names, mode, where installed
setu-theme current
setu-theme apply <id>           switch now
setu-theme apply --login <id>   what setu-session runs at login (no broadcasts)
setu-theme check <dir>          validate a pack
```

`apply` does, through documented APIs only:
1. writes `HKCU\Software\Setu\Theme\Current`;
2. sets the system colours (`SetSysColors`) and the `Control Panel\Colors`
   values from the pack;
3. activates the pack's msstyles (uxtheme) so themed controls repaint
   (`WM_THEMECHANGED`);
4. sets the wallpaper (`SystemParametersInfo(SPI_SETDESKWALLPAPER, ...)`
   with `WallpaperStyle`);
5. writes Setu's font overrides and `FontSmoothing*` from `[Fonts]`;
6. writes the `Shell Icons` entries and refreshes the icon cache
   (`SHChangeNotify(SHCNE_ASSOCCHANGED)`);
7. broadcasts `WM_SETTINGCHANGE` with `"ImmersiveColorSet"` and
   `"SetuTheme"` so setu-shell, setu-files and other windows reload.

Linux-side parts that can't change live (cursor theme, labwc theme, Plymouth,
Calamares) are regenerated from the active pack by `setu-session` at the next
login or by the image build; `apply` says so when they differ.

## Rules for packs

- Everything in a pack must be original or under a licence compatible with
  the repo (MIT/BSD/ISC/OFL/Apache, or unmodified Debian packages referred to
  by name). No Microsoft artwork, fonts, icons, cursors or msstyles.
- A pack must pass `setu-theme check`: valid INI, colours parse, every
  referenced file exists, contrast of `text` on `surface` and `accent-text`
  on `accent` at least 4.5:1.
