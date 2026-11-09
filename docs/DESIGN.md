# Setu visual design

Setu's look is its own: a modern, flat, light desktop that feels familiar to
people coming from current Windows, without reusing any Microsoft artwork,
fonts, icons, sounds or theme files, and without reproducing Windows' exact
layouts. Everything here is drawn from scratch or taken from open-source
projects under licences compatible with the repository (record each in
NOTICE).

This file is the single source of the design tokens. Code that draws Setu UI
(the control theme, window frames, the shell, the file manager, the
installer branding, the wallpaper) uses these values; change them here first.

## Principles

- Calm and flat: no bevels, no gradients on controls, no 3D borders.
- Content first: white surfaces on a light grey window background.
- Rounded: windows, menus and popups have rounded corners; controls a little.
- One accent colour for selection, focus and primary actions.
- Generous spacing; nothing cramped at 100 % scale; crisp at 125 % and 150 %.

## Typography

- UI font: **Inter** (SIL OFL 1.1, Debian `fonts-inter`). Bundled in the
  image and mapped as the substitute for the Windows UI face names
  (`MS Shell Dlg`, `MS Shell Dlg 2`, `Tahoma`, `Segoe UI`, `MS Sans Serif`),
  so unmodified apps get it. This is done with Setu's font override list
  (`HKCU\Software\Wine\Fonts\Overrides`), not FontSubstitutes: Windows
  prefers an installed font over its substitute, and Wine ships Tahoma and
  MS Sans Serif. Overrides apply only when Inter is installed; apps still see
  the face names they asked for.
- Monospace: **JetBrains Mono** or **Noto Sans Mono** (OFL), substituted for
  `Courier New`/`Consolas` only where an app asks for those names.
- Sizes (at 96 DPI): body 9 pt (12 px), caption 9 pt semibold, titles 10.5 pt
  semibold, start menu / large headers 14 pt semibold.

## Colours (light mode, the default)

| Token | Value | Use |
|---|---|---|
| `accent` | `#1F6FB2` | selection, focus ring, default button, links |
| `accent-hover` | `#2A7DC3` | hover on accent elements |
| `accent-pressed` | `#195E97` | pressed accent |
| `accent-text` | `#FFFFFF` | text on accent |
| `window-bg` | `#F3F3F3` | dialog and window background (COLOR_BTNFACE, COLOR_3DFACE) |
| `surface` | `#FFFFFF` | edit fields, list views, menus, cards (COLOR_WINDOW, COLOR_MENU) |
| `surface-hover` | `#EAEAEA` | hover on list items, menu items, flat buttons |
| `border` | `#D1D1D1` | control and window borders |
| `border-subtle` | `#E5E5E5` | separators, card outlines |
| `text` | `#1B1B1B` | primary text |
| `text-secondary` | `#5F5F5F` | secondary text, inactive captions |
| `text-disabled` | `#A0A0A0` | disabled text |
| `titlebar-active` | `#FFFFFF` | active window caption background |
| `titlebar-inactive` | `#F3F3F3` | inactive window caption background |
| `close-hover` | `#C42B1C` | close button hover (text white) |
| `taskbar` | `#EEF1F5` | taskbar background (subtle blue-grey) |
| `desktop` | wallpaper | see below |

Dark mode is planned later; keep colours in one table/struct so it can be
added without touching drawing code.

## Shape and spacing

- Corner radius: windows, menus, popups, start menu 8 px; buttons, edits,
  combo boxes 4 px; checkboxes 3 px; toggle/radio fully round.
- Borders 1 px. Focus ring: 2 px `accent`, inset 1 px, only for keyboard focus.
- Shadows: windows and popups get a soft shadow (8–16 px blur, ~20 % black)
  where the renderer allows; otherwise a 1 px `border`.
- Spacing scale: 4, 8, 12, 16, 24 px. Default control height 28 px (at 96 DPI),
  list rows 32 px, menu items 30 px, taskbar 48 px.

## Window frames

- Caption height 30 px, caption text left-aligned in `text` (semibold),
  window icon 16 px before it. The frame is drawn inside the metrics apps
  can query (SM_CYCAPTION, the frame and padded-border sizes,
  AdjustWindowRectEx), which follow Windows 11: a sizing frame of 8 px
  (BorderWidth 1 + PaddedBorderWidth 4 + the 3 px dialog frame). Windows 11
  leaves most of that band transparent; Setu fills it flat in the caption
  colour inside the 1 px `border`, so it reads as part of the caption at the
  top and as a margin elsewhere, and the whole band is the resize handle.
- Caption buttons 46 × 29 px, flat line glyphs (minimise, maximise/restore,
  close) drawn as vectors in `text`; hover `surface-hover`, close hover
  `close-hover` with white glyph. No 3D button faces.
- 1 px `border` around the window; rounded top corners where possible.

## Controls

- Push button: `surface` fill, 1 px `border`, radius 4; hover `surface-hover`;
  default button `accent` fill with `accent-text`.
- Edit / combo: `surface` fill, 1 px `border`, 2 px `accent` bottom line when
  focused.
- Checkbox / radio: 16 px; unchecked 1 px `text-secondary` outline; checked
  `accent` fill with a white check / dot.
- Scrollbar: thin (6 px) `text-secondary`-at-50 % thumb on transparent track,
  widening to 12 px on hover; no arrow buttons or flat arrows only.
- Progress bar: 4 px track `border-subtle`, `accent` fill, rounded.
- Tabs: text tabs with a 3 px `accent` underline on the selected one.
- Menus: `surface`, radius 8, 4 px padding, items 30 px, hover
  `surface-hover` with radius 4.
- Tooltips: `surface`, 1 px `border`, radius 4.

## Shell

- Taskbar 48 px, `taskbar` colour, icons 24 px in 40 px buttons, running
  apps marked with a 3 px `accent` pill under the icon. App buttons are
  centred by default (option: left), with the Setu button first in the group;
  clock and tray at the right.
- Start menu: floating panel, radius 8, `surface`, shadow, search box at the
  top, pinned apps as an icon grid, "All apps" list behind a button, user and
  power at the bottom.
- File manager: command bar at the top (flat icon buttons), address/breadcrumb
  bar with radius 4, navigation pane on `window-bg`, content on `surface`,
  rows 32 px, accent selection at 20 % with a 1 px accent outline.

## Icons

- Line icons, 1.5 px stroke at 24 px, from **Lucide** (ISC) or **Tabler
  Icons** (MIT), recoloured to `text`. Not Fluent UI System Icons: they are
  MIT-licensed but are Microsoft's artwork. Record the set in NOTICE.

## Wallpaper and branding

- Default wallpaper: an original abstract image (soft gradient waves) in
  blues with a touch of the Setu orange `#F5A623`, generated from code or
  SVG in the repo, so it can be rebuilt at any resolution.
- The Setu bridge logo stays orange `#F5A623`.
