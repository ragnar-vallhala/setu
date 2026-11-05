/*
 * REG COMPARE
 *
 * Copyright 2026 Setu contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * reg compare <key1> <key2> [/v name | /ve] [/oa | /od | /os | /on] [/s]
 *
 * Values present in both keys with the same type and data are "equal" (=),
 * other values are printed for each side (< for key1, > for key2). With /s
 * subkeys are compared too: subkeys present on both sides are compared
 * recursively, then subkeys present on only one side are listed. The exit
 * code is 0 when the keys are identical, 2 when they differ and 1 on error.
 */

#include "reg.h"

enum output { OUT_DIFF, OUT_ALL, OUT_SAME, OUT_NONE };

struct cmp_side
{
    HKEY hkey;
    WCHAR *path;     /* full path, for output */
};

static void print_value(WCHAR mark, const WCHAR *path, const WCHAR *name, DWORD type,
                        const BYTE *data, DWORD size)
{
    WCHAR defval[32], *str;
    const WCHAR *type_name = reg_type_to_wchar(type);
    WCHAR type_buf[16];

    if (!name || !*name)
    {
        LoadStringW(GetModuleHandleW(NULL), STRING_DEFAULT_VALUE, defval, ARRAY_SIZE(defval));
        name = defval;
    }
    if (!type_name)
    {
        swprintf(type_buf, ARRAY_SIZE(type_buf), L"0x%x", type);
        type_name = type_buf;
    }
    str = reg_data_to_wchar(type, data, size);
    output_string(L"%1!c! Value: %2  %3 %4 %5\n", mark, path, name, type_name, str ? str : L"");
    free(str);
}

static void print_key(WCHAR mark, const WCHAR *path, const WCHAR *name)
{
    output_string(L"%1!c! Key: %2\\%3\n", mark, path, name);
}

static BOOL get_value(HKEY hkey, const WCHAR *name, DWORD *type, BYTE **data, DWORD *size)
{
    LONG rc;

    *data = NULL;
    *size = 0;
    if ((rc = RegQueryValueExW(hkey, name, NULL, type, NULL, size))) return FALSE;
    if (!(*data = malloc(*size + sizeof(WCHAR)))) return FALSE;
    if ((rc = RegQueryValueExW(hkey, name, NULL, type, *data, size)))
    {
        free(*data);
        *data = NULL;
        return FALSE;
    }
    memset(*data + *size, 0, sizeof(WCHAR));
    return TRUE;
}

/* Compare one value present (or not) on each side; returns TRUE when they differ. */
static BOOL compare_value(struct cmp_side *a, struct cmp_side *b, const WCHAR *name, enum output out)
{
    DWORD type_a, type_b, size_a, size_b;
    BYTE *data_a, *data_b;
    BOOL have_a = get_value(a->hkey, name, &type_a, &data_a, &size_a);
    BOOL have_b = get_value(b->hkey, name, &type_b, &data_b, &size_b);
    BOOL differ;

    differ = have_a != have_b || type_a != type_b || size_a != size_b ||
             (size_a && memcmp(data_a, data_b, size_a));
    if (!have_a && !have_b) differ = FALSE;

    if (differ && (out == OUT_DIFF || out == OUT_ALL))
    {
        if (have_a) print_value('<', a->path, name, type_a, data_a, size_a);
        if (have_b) print_value('>', b->path, name, type_b, data_b, size_b);
    }
    else if (!differ && have_a && (out == OUT_SAME || out == OUT_ALL))
        print_value('=', a->path, name, type_a, data_a, size_a);

    free(data_a);
    free(data_b);
    return differ;
}

static WCHAR *enum_name(HKEY hkey, DWORD index, BOOL values, DWORD max_len)
{
    WCHAR *name = malloc((max_len + 1) * sizeof(WCHAR));
    DWORD len = max_len + 1;
    LONG rc;

    if (!name) return NULL;
    if (values) rc = RegEnumValueW(hkey, index, name, &len, NULL, NULL, NULL, NULL);
    else rc = RegEnumKeyExW(hkey, index, name, &len, NULL, NULL, NULL, NULL);
    if (rc)
    {
        free(name);
        return NULL;
    }
    return name;
}

static BOOL has_subkey(HKEY hkey, const WCHAR *name, REGSAM sam)
{
    HKEY sub;

    if (RegOpenKeyExW(hkey, name, 0, KEY_READ|sam, &sub)) return FALSE;
    RegCloseKey(sub);
    return TRUE;
}

static BOOL compare_keys(struct cmp_side *a, struct cmp_side *b, enum output out, BOOL recurse, REGSAM sam)
{
    DWORD max_a = 0, max_b = 0, maxk_a = 0, maxk_b = 0, i;
    BOOL differ = FALSE;
    WCHAR *name;

    RegQueryInfoKeyW(a->hkey, NULL, NULL, NULL, NULL, &maxk_a, NULL, NULL, &max_a, NULL, NULL, NULL);
    RegQueryInfoKeyW(b->hkey, NULL, NULL, NULL, NULL, &maxk_b, NULL, NULL, &max_b, NULL, NULL, NULL);
    max_a = max(max_a, max_b) + 1;
    maxk_a = max(max(maxk_a, maxk_b), MAX_SUBKEY_LEN) + 1;

    /* Values of key1, in its order, then values only key2 has. */
    for (i = 0; (name = enum_name(a->hkey, i, TRUE, max_a)); i++)
    {
        if (compare_value(a, b, name, out)) differ = TRUE;
        free(name);
    }
    for (i = 0; (name = enum_name(b->hkey, i, TRUE, max_a)); i++)
    {
        if (RegQueryValueExW(a->hkey, name, NULL, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND)
        {
            if (compare_value(a, b, name, out)) differ = TRUE;
        }
        free(name);
    }

    if (!recurse) return differ;

    /* Subkeys on both sides first, then those on one side only. */
    for (i = 0; (name = enum_name(a->hkey, i, FALSE, maxk_a)); i++)
    {
        struct cmp_side sa, sb;

        if (!RegOpenKeyExW(a->hkey, name, 0, KEY_READ|sam, &sa.hkey))
        {
            if (!RegOpenKeyExW(b->hkey, name, 0, KEY_READ|sam, &sb.hkey))
            {
                sa.path = build_subkey_path(a->path, lstrlenW(a->path), name, lstrlenW(name));
                sb.path = build_subkey_path(b->path, lstrlenW(b->path), name, lstrlenW(name));
                if (compare_keys(&sa, &sb, out, TRUE, sam)) differ = TRUE;
                free(sa.path);
                free(sb.path);
                RegCloseKey(sb.hkey);
            }
            RegCloseKey(sa.hkey);
        }
        free(name);
    }
    for (i = 0; (name = enum_name(a->hkey, i, FALSE, maxk_a)); i++)
    {
        if (!has_subkey(b->hkey, name, sam))
        {
            differ = TRUE;
            if (out == OUT_DIFF || out == OUT_ALL) print_key('<', a->path, name);
        }
        free(name);
    }
    for (i = 0; (name = enum_name(b->hkey, i, FALSE, maxk_a)); i++)
    {
        if (!has_subkey(a->hkey, name, sam))
        {
            differ = TRUE;
            if (out == OUT_DIFF || out == OUT_ALL) print_key('>', b->path, name);
        }
        free(name);
    }
    return differ;
}

int reg_compare(int argc, WCHAR *argvW[])
{
    HKEY root_a, root_b;
    WCHAR *path_a, *path_b, *value_name = NULL;
    BOOL value_empty = FALSE, recurse = FALSE, have_out = FALSE, differ;
    enum output out = OUT_DIFF;
    struct cmp_side a, b;
    REGSAM sam = 0;
    LONG rc;
    int i;

    if (argc < 4)
        return syntax_error();

    for (i = 4; i < argc; i++)
    {
        WCHAR *str;

        if (argvW[i][0] != '/' && argvW[i][0] != '-')
            return syntax_error();

        str = &argvW[i][1];

        if (!lstrcmpiW(str, L"ve"))
        {
            if (value_empty || value_name) return syntax_error();
            value_empty = TRUE;
        }
        else if (!lstrcmpiW(str, L"v"))
        {
            if (value_empty || value_name || !(value_name = argvW[++i])) return syntax_error();
        }
        else if (!lstrcmpiW(str, L"s"))
        {
            if (recurse) return syntax_error();
            recurse = TRUE;
        }
        else if (!lstrcmpiW(str, L"oa") || !lstrcmpiW(str, L"od") ||
                 !lstrcmpiW(str, L"os") || !lstrcmpiW(str, L"on"))
        {
            if (have_out) return syntax_error();
            have_out = TRUE;
            switch (towlower(str[1]))
            {
            case 'a': out = OUT_ALL; break;
            case 'd': out = OUT_DIFF; break;
            case 's': out = OUT_SAME; break;
            default:  out = OUT_NONE; break;
            }
        }
        else if (!lstrcmpiW(str, L"reg:32"))
        {
            if (sam & KEY_WOW64_32KEY) return syntax_error();
            sam |= KEY_WOW64_32KEY;
        }
        else if (!lstrcmpiW(str, L"reg:64"))
        {
            if (sam & KEY_WOW64_64KEY) return syntax_error();
            sam |= KEY_WOW64_64KEY;
        }
        else
            return syntax_error();
    }

    if (sam == (KEY_WOW64_32KEY|KEY_WOW64_64KEY))
        return syntax_error();

    if (!parse_registry_key(argvW[2], &root_a, &path_a) ||
        !parse_registry_key(argvW[3], &root_b, &path_b))
        return 1;

    if (root_a == root_b && !lstrcmpiW(path_a ? path_a : L"", path_b ? path_b : L""))
    {
        output_error(STRING_COMPARE_SAME);
        return 1;
    }

    if ((rc = RegOpenKeyExW(root_a, path_a, 0, KEY_READ|sam, &a.hkey)))
    {
        output_reg_error(rc);
        return 1;
    }
    if ((rc = RegOpenKeyExW(root_b, path_b, 0, KEY_READ|sam, &b.hkey)))
    {
        RegCloseKey(a.hkey);
        output_reg_error(rc);
        return 1;
    }
    a.path = get_long_key(root_a, path_a);
    b.path = get_long_key(root_b, path_b);

    if (value_name || value_empty)
    {
        const WCHAR *name = value_empty ? NULL : value_name;

        if (RegQueryValueExW(a.hkey, name, NULL, NULL, NULL, NULL) ||
            RegQueryValueExW(b.hkey, name, NULL, NULL, NULL, NULL))
        {
            output_error(STRING_KEY_NONEXIST);
            rc = 1;
            goto done;
        }
        differ = compare_value(&a, &b, name, out);
    }
    else
        differ = compare_keys(&a, &b, out, recurse, sam);

    output_message(differ ? STRING_COMPARE_DIFFERENT : STRING_COMPARE_IDENTICAL);
    output_message(STRING_SUCCESS);
    rc = differ ? 2 : 0;

done:
    free(a.path);
    free(b.path);
    RegCloseKey(a.hkey);
    RegCloseKey(b.hkey);
    return rc;
}
