/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * setu-theme.h: Setu's theme pack loader (docs/THEMES.md), shared by
 * setu-shell, setu-files and setu-theme.
 *
 * The active pack is HKCU\Software\Setu\Theme, value Current (setu-light
 * when missing). A pack <id> is looked for in %LOCALAPPDATA%\Setu\Themes\<id>
 * first (a user pack wins), then in %WINDIR%\Resources\Themes\<id>. Its
 * theme.ini (or the file the <id>.theme's [Setu] Tokens= names) holds the
 * tokens, icons\index.ini (or [Setu] Icons=) the named icons; Base=<id> in
 * [Theme] inherits every unset key and icon from another pack, and the
 * built-in setu-light table (setu-theme.c) is the last fallback, so a
 * missing or incomplete pack never breaks anything.
 *
 * Every token is listed once, in the X-macro tables below, with its
 * built-in setu-light value (docs/DESIGN.md).
 */
#ifndef SETU_THEME_H
#define SETU_THEME_H

#include <windows.h>

/* [Colors]: id, key, setu-light value (#RRGGBB or #RRGGBBAA) */
#define SETU_COLOR_TOKENS(X) \
    X(ACCENT,                 "accent",                 "#1F6FB2") \
    X(ACCENT_HOVER,           "accent-hover",           "#2A7DC3") \
    X(ACCENT_PRESSED,         "accent-pressed",         "#195E97") \
    X(ACCENT_TEXT,            "accent-text",            "#FFFFFF") \
    X(WINDOW_BG,              "window-bg",              "#F3F3F3") \
    X(SURFACE,                "surface",                "#FFFFFF") \
    X(SURFACE_HOVER,          "surface-hover",          "#EAEAEA") \
    X(BORDER,                 "border",                 "#D1D1D1") \
    X(BORDER_SUBTLE,          "border-subtle",          "#E5E5E5") \
    X(TEXT,                   "text",                   "#1B1B1B") \
    X(TEXT_SECONDARY,         "text-secondary",         "#5F5F5F") \
    X(TEXT_DISABLED,          "text-disabled",          "#A0A0A0") \
    X(TITLEBAR_ACTIVE,        "titlebar-active",        "#FFFFFF") \
    X(TITLEBAR_INACTIVE,      "titlebar-inactive",      "#F3F3F3") \
    X(TITLEBAR_TEXT,          "titlebar-text",          "#1B1B1B") \
    X(TITLEBAR_TEXT_INACTIVE, "titlebar-text-inactive", "#5F5F5F") \
    X(CLOSE_HOVER,            "close-hover",            "#C42B1C") \
    X(TASKBAR,                "taskbar",                "#EEF1F5") \
    X(TASKBAR_TEXT,           "taskbar-text",           "#1B1B1B") \
    X(MENU,                   "menu",                   "#FFFFFF") \
    X(SELECTION,              "selection",              "#1F6FB233") \
    X(SELECTION_BORDER,       "selection-border",       "#1F6FB2") \
    X(SHADOW,                 "shadow",                 "#00000033") \
    X(DESKTOP,                "desktop",                "#1E4F8A") \
    X(LOGO,                   "logo",                   "#F5A623") \
    X(ERROR,                  "error",                  "#C42B1C") \
    X(TILE_1,                 "tile-1",                 "#2F7D6A") \
    X(TILE_2,                 "tile-2",                 "#5B5FC7") \
    X(TILE_3,                 "tile-3",                 "#B55A3C") \
    X(TILE_4,                 "tile-4",                 "#1F6FB2") \
    X(TILE_5,                 "tile-5",                 "#8A6D1F") \
    X(TILE_6,                 "tile-6",                 "#9B4D87")

/* [Metrics]: pixels at 96 DPI */
#define SETU_METRIC_TOKENS(X) \
    X(RADIUS_WINDOW,          "radius-window",          8) \
    X(RADIUS_CONTROL,         "radius-control",         4) \
    X(RADIUS_CHECK,           "radius-check",           3) \
    X(CAPTION_HEIGHT,         "caption-height",         30) \
    X(CAPTION_BUTTON_WIDTH,   "caption-button-width",   46) \
    X(TASKBAR_HEIGHT,         "taskbar-height",         48) \
    X(TASKBAR_BUTTON,         "taskbar-button",         40) \
    X(TASKBAR_ICON,           "taskbar-icon",           24) \
    X(TASKBAR_PILL,           "taskbar-pill",           3) \
    X(ROW_HEIGHT,             "row-height",             32) \
    X(MENU_ITEM_HEIGHT,       "menu-item-height",       30) \
    X(CONTROL_HEIGHT,         "control-height",         28) \
    X(SCROLLBAR_WIDTH,        "scrollbar-width",        6) \
    X(SCROLLBAR_WIDTH_HOVER,  "scrollbar-width-hover",  12) \
    X(FOCUS_WIDTH,            "focus-width",            2) \
    X(SHADOW_BLUR,            "shadow-blur",            16)

/* Named icons (distro/themes/ICONS.md): id, name, built-in Lucide glyph
 * (setu-icons.h; ICON_SETU_MARK is the Setu mark drawn in setu-style.h). */
#define SETU_ICON_NAMES(X) \
    X(NAV_BACK,              "nav-back",              ICON_ARROW_LEFT) \
    X(NAV_FORWARD,           "nav-forward",           ICON_ARROW_RIGHT) \
    X(NAV_UP,                "nav-up",                ICON_ARROW_UP) \
    X(REFRESH,               "refresh",               ICON_ROTATE_CW) \
    X(CMD_NEW_FOLDER,        "cmd-new-folder",        ICON_FOLDER_PLUS) \
    X(CMD_CUT,               "cmd-cut",               ICON_SCISSORS) \
    X(CMD_COPY,              "cmd-copy",              ICON_COPY) \
    X(CMD_PASTE,             "cmd-paste",             ICON_CLIPBOARD_PASTE) \
    X(CMD_RENAME,            "cmd-rename",            ICON_PENCIL) \
    X(CMD_DELETE,            "cmd-delete",            ICON_TRASH_2) \
    X(CMD_PROPERTIES,        "cmd-properties",        ICON_INFO) \
    X(CMD_EMPTY_RECYCLE_BIN, "cmd-empty-recycle-bin", ICON_TRASH_2) \
    X(VIEW_DETAILS,          "view-details",          ICON_LIST) \
    X(VIEW_ICONS,            "view-icons",            ICON_LAYOUT_GRID) \
    X(SEARCH,                "search",                ICON_SEARCH) \
    X(CHEVRON_LEFT,          "chevron-left",          ICON_CHEVRON_LEFT) \
    X(CHEVRON_RIGHT,         "chevron-right",         ICON_CHEVRON_RIGHT) \
    X(CHEVRON_UP,            "chevron-up",            ICON_CHEVRON_UP) \
    X(CHEVRON_DOWN,          "chevron-down",          ICON_CHEVRON_DOWN) \
    X(PIN,                   "pin",                   ICON_PIN) \
    X(POWER,                 "power",                 ICON_POWER) \
    X(USER,                  "user",                  ICON_CIRCLE_USER_ROUND) \
    X(SETU_LOGO,             "setu-logo",             ICON_SETU_MARK) \
    X(SETU_APP,              "setu-app",              ICON_SETU_MARK) \
    X(COMPUTER,              "computer",              ICON_MONITOR) \
    X(FOLDER,                "folder",                ICON_FOLDER) \
    X(FOLDER_OPEN,           "folder-open",           ICON_FOLDER) \
    X(FILE,                  "file",                  ICON_FILE_TEXT) \
    X(FILE_TEXT,             "file-text",             ICON_FILE_TEXT) \
    X(USER_HOME,             "user-home",             ICON_HOUSE) \
    X(USER_DESKTOP,          "user-desktop",          ICON_MONITOR) \
    X(FOLDER_DOCUMENTS,      "folder-documents",      ICON_FILE_TEXT) \
    X(FOLDER_DOWNLOAD,       "folder-download",       ICON_DOWNLOAD) \
    X(DRIVE_FIXED,           "drive-fixed",           ICON_HARD_DRIVE) \
    X(DRIVE_REMOVABLE,       "drive-removable",       ICON_HARD_DRIVE) \
    X(DRIVE_OPTICAL,         "drive-optical",         ICON_HARD_DRIVE) \
    X(RECYCLE_BIN_EMPTY,     "recycle-bin-empty",     ICON_TRASH_2) \
    X(RECYCLE_BIN_FULL,      "recycle-bin-full",      ICON_TRASH_2)

#define SETU_X_ENUM(id, key, def) TC_##id,
enum setu_color_token { SETU_COLOR_TOKENS(SETU_X_ENUM) TC_COUNT };
#undef SETU_X_ENUM
#define SETU_X_ENUM(id, key, def) TM_##id,
enum setu_metric_token { SETU_METRIC_TOKENS(SETU_X_ENUM) TM_COUNT };
#undef SETU_X_ENUM
#define SETU_X_ENUM(id, key, def) TI_##id,
enum setu_icon_name { SETU_ICON_NAMES(SETU_X_ENUM) TI_COUNT };
#undef SETU_X_ENUM

enum setu_theme_where { THEME_BUILTIN, THEME_USER, THEME_SYSTEM };
enum setu_smoothing { SMOOTH_NONE, SMOOTH_GRAYSCALE, SMOOTH_RGB };

struct setu_kv
{
    WCHAR section[40];
    WCHAR key[64];
    WCHAR value[MAX_PATH];
    int level;               /* 0 the pack itself, 1 its Base, ...; -1 built-in */
};

struct setu_theme
{
    WCHAR id[64];
    WCHAR name[128];
    WCHAR dir[MAX_PATH];     /* the pack's folder; empty for the built-in one */
    enum setu_theme_where where;
    BOOL dark;               /* [Theme] Mode=dark */
    int levels;              /* packs in the Base chain that were found */
    WCHAR chain[8][MAX_PATH];/* their folders, the pack first */

    COLORREF color[TC_COUNT];
    BYTE alpha[TC_COUNT];    /* 255 unless #RRGGBBAA */
    int metric[TM_COUNT];    /* pixels at 96 DPI */

    WCHAR font_ui[LF_FACESIZE * 4];   /* face list, first installed wins */
    WCHAR font_mono[LF_FACESIZE * 4];
    int ui_size10, title_size10, header_size10;  /* points x 10 */
    int caption_weight;
    enum setu_smoothing smoothing;

    BOOL taskbar_left;
    int start_columns, start_rows;

    WCHAR icon_file[TI_COUNT][MAX_PATH];   /* "" = built-in glyph */

    struct setu_kv *kv;      /* every key of the chain, the pack's first */
    int nkv;
    WCHAR icon_set[64];      /* [Icons] set= in effect, "" none */
    struct setu_kv *icons;   /* [Icons] of every index.ini in the chain (level: 2 x the
                              * pack's; a set named at level L is 2L - 1) */
    int nicons;
};

/* the active pack, filled by setu_theme_reload() */
extern struct setu_theme setu_theme;
extern const char *const setu_icon_key[TI_COUNT];
extern const char *const setu_color_key[TC_COUNT];
extern const char *const setu_metric_key[TM_COUNT];

/* the active pack's id (HKCU\Software\Setu\Theme\Current) */
void setu_theme_current_id(WCHAR *id, int len);
/* find pack ID: its folder and where; FALSE when not installed */
BOOL setu_theme_find(const WCHAR *id, WCHAR *dir, int len, enum setu_theme_where *where);
/* Load pack ID (NULL: the active one) into T, or the pack in folder DIR
 * when DIR is set. Never fails: what's missing comes from the built-in
 * table. ERR (optional) gets the first problem found. Returns FALSE when
 * the pack itself was not found. */
BOOL setu_theme_load(struct setu_theme *t, const WCHAR *id, const WCHAR *dir, WCHAR *err, int errlen);
void setu_theme_free(struct setu_theme *t);
/* reload the active pack into setu_theme */
void setu_theme_reload(void);

/* any value, resolved through the chain and the built-in table; NULL when unset */
const WCHAR *setu_theme_get(const struct setu_theme *t, const WCHAR *section, const WCHAR *key);
/* the level a key came from: 0 the pack, >0 a Base, -1 built-in, -2 unset */
int setu_theme_level(const struct setu_theme *t, const WCHAR *section, const WCHAR *key);
const WCHAR *setu_theme_icon_lookup(const struct setu_theme *t, const WCHAR *name);

/* Parse an INI file (UTF-8 or UTF-16LE) into LIST: [section] (PREFIX put
 * before its name), key=value, ';'/'#' comment lines and " ;" inline
 * comments. Other lines are counted in *BAD, the first one in *BADLINE. */
BOOL setu_ini_parse(const WCHAR *path, const WCHAR *prefix, struct setu_kv **list, int *n, int level,
                    int *bad, int *badline);
BOOL setu_parse_color(const WCHAR *s, COLORREF *c, BYTE *alpha);
/* METRIC scaled to DPI */
static inline int setu_metric(enum setu_metric_token m, UINT dpi)
{
    return MulDiv(setu_theme.metric[m], (int)dpi, 96);
}

/* Is this message a theme change (WM_SETTINGCHANGE "SetuTheme" or
 * "ImmersiveColorSet", WM_THEMECHANGED, WM_SYSCOLORCHANGE)? */
BOOL setu_theme_changed_msg(UINT msg, WPARAM wp, LPARAM lp);

/* WCAG 2 contrast ratio of two colours, x 100 */
int setu_contrast100(COLORREF a, COLORREF b);

#endif /* SETU_THEME_H */
