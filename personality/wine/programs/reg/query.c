/*
 * Copyright 2016-2017, 2021 Hugh McMaster
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

#include <stdio.h>
#include "reg.h"

/* Separator used between REG_MULTI_SZ strings; 0 means the two characters "\0". */
static WCHAR multi_sz_separator;

const WCHAR *reg_type_to_wchar(DWORD type)
{
    int i, array_size = ARRAY_SIZE(type_rels);

    for (i = 0; i < array_size; i++)
    {
        if (type == type_rels[i].type)
            return type_rels[i].name;
    }

    /* Names reg query shows but that REG ADD does not accept. */
    switch (type)
    {
    case REG_LINK: return L"REG_LINK";
    case REG_RESOURCE_LIST: return L"REG_RESOURCE_LIST";
    case REG_FULL_RESOURCE_DESCRIPTOR: return L"REG_FULL_RESOURCE_DESCRIPTOR";
    }
    return NULL;
}

static WCHAR *hex_to_wchar(const BYTE *src, DWORD size_bytes)
{
    WCHAR *buffer = malloc((size_bytes * 2 + 1) * sizeof(WCHAR)), *ptr = buffer;
    DWORD i;

    *buffer = 0;
    for (i = 0; i < size_bytes; i++)
        ptr += swprintf(ptr, 3, L"%02X", src[i]);
    return buffer;
}

WCHAR *reg_data_to_wchar(DWORD type, const BYTE *src, DWORD size_bytes)
{
    WCHAR *buffer = NULL;

    switch (type)
    {
        case REG_SZ:
        case REG_EXPAND_SZ:
            buffer = malloc(size_bytes + sizeof(WCHAR));
            memcpy(buffer, src, size_bytes);
            buffer[size_bytes / sizeof(WCHAR)] = 0;
            break;
        case REG_DWORD:
     /* case REG_DWORD_LITTLE_ENDIAN: */
        case REG_DWORD_BIG_ENDIAN:
        case REG_QWORD:
        {
            /* Short data is zero-extended, long data cut to the type's size;
             * no data at all shows nothing. */
            DWORD max = type == REG_QWORD ? sizeof(ULONGLONG) : sizeof(DWORD);
            ULONGLONG qw = 0;

            buffer = malloc(19 * sizeof(WCHAR));
            *buffer = 0;
            if (!size_bytes) break;
            memcpy(&qw, src, min(size_bytes, max));
            swprintf(buffer, 19, L"0x%I64x", qw);
            break;
        }
        case REG_MULTI_SZ:
        {
            /* The strings are the data's characters split at each NUL; empty
             * strings are skipped and no separator follows the last one. */
            const WCHAR *str = (const WCHAR *)src;
            DWORD len = size_bytes / sizeof(WCHAR), start, j;
            int destindex = 0;

            buffer = malloc((len * 2 + 1) * sizeof(WCHAR));
            for (start = 0; start < len; start = j + 1)
            {
                for (j = start; j < len && str[j]; j++) ;
                if (j == start) continue;
                if (destindex)
                {
                    if (multi_sz_separator) buffer[destindex++] = multi_sz_separator;
                    else
                    {
                        buffer[destindex++] = '\\';
                        buffer[destindex++] = '0';
                    }
                }
                memcpy(buffer + destindex, str + start, (j - start) * sizeof(WCHAR));
                destindex += j - start;
            }
            buffer[destindex] = 0;
            break;
        }
        default:
            /* REG_NONE, REG_BINARY and types reg.exe does not know are shown as hex. */
            return hex_to_wchar(src, size_bytes);
    }
    return buffer;
}

static const WCHAR *newlineW = L"\n";

static REGSAM sam = 0;
static BOOL verbose;

static void output_value(const WCHAR *value_name, DWORD type, BYTE *data, DWORD data_size)
{
    static const WCHAR *fmt = L"    %1";
    const WCHAR *type_name;
    WCHAR defval[32];
    WCHAR *reg_data;

    if (value_name && value_name[0])
        output_string(fmt, value_name);
    else
    {
        LoadStringW(GetModuleHandleW(NULL), STRING_DEFAULT_VALUE, defval, ARRAY_SIZE(defval));
        output_string(fmt, defval);
    }

    /* Types reg.exe does not know are shown as REG_NONE. */
    if (!(type_name = reg_type_to_wchar(type))) type_name = L"REG_NONE";
    if (verbose)
        output_string(L"    %1 (%2!u!)", type_name, type);
    else
        output_string(fmt, type_name);

    if (data)
    {
        reg_data = reg_data_to_wchar(type, data, data_size);
        output_string(fmt, reg_data);
        free(reg_data);
    }
    else
    {
        LoadStringW(GetModuleHandleW(NULL), STRING_VALUE_NOT_SET, defval, ARRAY_SIZE(defval));
        output_string(fmt, defval);
    }
    output_string(newlineW);
}

static unsigned int num_values_found = 0;

static int query_value(HKEY hkey, WCHAR *value_name, WCHAR *path, BOOL recurse)
{
    LONG rc;
    DWORD max_data_bytes = 2048, data_size;
    DWORD subkey_len;
    DWORD type, path_len, i;
    BYTE *data;
    static const WCHAR *fmt = L"%1\n";
    WCHAR *subkey_name, *subkey_path;
    HKEY subkey;

    data = malloc(max_data_bytes);

    for (;;)
    {
        data_size = max_data_bytes;
        rc = RegQueryValueExW(hkey, value_name, NULL, &type, data, &data_size);
        if (rc == ERROR_MORE_DATA)
        {
            max_data_bytes = data_size;
            data = realloc(data, max_data_bytes);
        }
        else break;
    }

    if (rc == ERROR_SUCCESS)
    {
        output_string(fmt, path);
        output_value(value_name, type, data, data_size);
        output_string(newlineW);
        num_values_found++;
    }

    free(data);

    if (!recurse)
    {
        if (rc == ERROR_FILE_NOT_FOUND)
        {
            if (value_name && *value_name)
            {
                output_string(newlineW);
                output_error(STRING_VALUE_NONEXIST);
                return 1;
            }
            output_string(fmt, path);
            output_value(NULL, REG_SZ, NULL, 0);
            output_string(newlineW);
        }
        return 0;
    }

    subkey_name = malloc(MAX_SUBKEY_LEN * sizeof(WCHAR));

    path_len = lstrlenW(path);

    i = 0;
    for (;;)
    {
        subkey_len = MAX_SUBKEY_LEN;
        rc = RegEnumKeyExW(hkey, i, subkey_name, &subkey_len, NULL, NULL, NULL, NULL);
        if (rc == ERROR_SUCCESS)
        {
            subkey_path = build_subkey_path(path, path_len, subkey_name, subkey_len);
            if (!RegOpenKeyExW(hkey, subkey_name, 0, KEY_READ|sam, &subkey))
            {
                query_value(subkey, value_name, subkey_path, recurse);
                RegCloseKey(subkey);
            }
            free(subkey_path);
            i++;
        }
        else break;
    }

    free(subkey_name);
    return 0;
}

/* Options of a /f or /t search. */
struct search
{
    const WCHAR *pattern;   /* /f, NULL to match everything */
    BOOL keys, names, data; /* /k, /v, /d; all set when none was given */
    BOOL match_case;        /* /c */
    BOOL exact;             /* /e */
    DWORD *types;           /* /t */
    DWORD type_count;
    BOOL recurse;
};

static BOOL search_match(const struct search *s, const WCHAR *str)
{
    if (!s->pattern || !wcscmp(s->pattern, L"*")) return TRUE;
    if (!str) return FALSE;

    /* Case is folded for every letter, not only ASCII. */
    if (s->exact)
        return CompareStringOrdinal(str, -1, s->pattern, -1, !s->match_case) == CSTR_EQUAL;

    if (s->match_case)
        return wcsstr(str, s->pattern) != NULL;
    else
    {
        size_t len = wcslen(s->pattern), i, n = wcslen(str);

        for (i = 0; i + len <= n; i++)
            if (CompareStringOrdinal(str + i, len, s->pattern, len, TRUE) == CSTR_EQUAL) return TRUE;
        return FALSE;
    }
}

static BOOL search_type(const struct search *s, DWORD type)
{
    DWORD i;

    if (!s->type_count) return TRUE;
    for (i = 0; i < s->type_count; i++)
        if (s->types[i] == type) return TRUE;
    return FALSE;
}

static BOOL search_value(const struct search *s, const WCHAR *name, DWORD type,
                         const BYTE *data, DWORD size)
{
    WCHAR *str;
    BOOL ret;

    if (!search_type(s, type)) return FALSE;
    if (!s->pattern) return TRUE;
    if (!wcscmp(s->pattern, L"*")) return TRUE;
    if (s->names && search_match(s, name)) return TRUE;
    if (!s->data) return FALSE;
    /* Only string data is searched: a DWORD is not matched by its 0x form. */
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ) return FALSE;

    str = reg_data_to_wchar(type, data, size);
    ret = search_match(s, str);
    free(str);
    return ret;
}

static void search_key(const struct search *s, HKEY hkey, WCHAR *path, const WCHAR *name)
{
    DWORD max_value_len = 256, value_len, max_data_bytes = 2048, data_size;
    DWORD subkey_len, i, type, path_len;
    WCHAR *value_name, *subkey_name, *subkey_path;
    BOOL header = FALSE;
    BYTE *data;
    HKEY subkey;
    LONG rc;

    /* The key the search starts from is never matched by name. */
    if (name && s->keys && !s->type_count && search_match(s, name))
    {
        output_string(L"%1\n", path);
        header = TRUE;
        num_values_found++;
    }

    if (s->names || s->data || s->type_count)
    {
        value_name = malloc(max_value_len * sizeof(WCHAR));
        data = malloc(max_data_bytes);

        for (i = 0;;)
        {
            value_len = max_value_len;
            data_size = max_data_bytes;
            rc = RegEnumValueW(hkey, i, value_name, &value_len, NULL, &type, data, &data_size);
            if (rc == ERROR_SUCCESS)
            {
                if (search_value(s, value_name, type, data, data_size))
                {
                    if (!header) output_string(L"%1\n", path);
                    header = TRUE;
                    output_value(value_name, type, data, data_size);
                    num_values_found++;
                }
                i++;
            }
            else if (rc == ERROR_MORE_DATA)
            {
                if (data_size > max_data_bytes)
                {
                    max_data_bytes = data_size;
                    data = realloc(data, max_data_bytes);
                }
                else
                {
                    max_value_len *= 2;
                    value_name = realloc(value_name, max_value_len * sizeof(WCHAR));
                }
            }
            else break;
        }

        free(data);
        free(value_name);

        if (header) output_string(newlineW);
    }

    subkey_name = malloc(MAX_SUBKEY_LEN * sizeof(WCHAR));
    path_len = lstrlenW(path);

    for (i = 0;; i++)
    {
        subkey_len = MAX_SUBKEY_LEN;
        rc = RegEnumKeyExW(hkey, i, subkey_name, &subkey_len, NULL, NULL, NULL, NULL);
        if (rc != ERROR_SUCCESS) break;

        subkey_path = build_subkey_path(path, path_len, subkey_name, subkey_len);
        if (s->recurse)
        {
            if (!RegOpenKeyExW(hkey, subkey_name, 0, KEY_READ|sam, &subkey))
            {
                search_key(s, subkey, subkey_path, subkey_name);
                RegCloseKey(subkey);
            }
        }
        else if (s->keys && !s->type_count && search_match(s, subkey_name))
        {
            output_string(L"%1\n", subkey_path);
            num_values_found++;
        }
        free(subkey_path);
    }

    free(subkey_name);
}

static int query_all(HKEY hkey, WCHAR *path, BOOL recurse, BOOL recursing)
{
    LONG rc;
    DWORD num_subkeys, num_values;
    DWORD max_value_len = 256, value_len;
    DWORD max_data_bytes = 2048, data_size;
    DWORD subkey_len;
    DWORD i, type, path_len;
    WCHAR *value_name, *subkey_name, *subkey_path;
    BYTE *data;
    HKEY subkey;

    rc = RegQueryInfoKeyW(hkey, NULL, NULL, NULL, &num_subkeys, NULL,
                          NULL, &num_values, NULL, NULL, NULL, NULL);
    if (rc) return 1;

    if (num_values || recursing)
        output_string(L"%1\n", path);

    value_name = malloc(max_value_len * sizeof(WCHAR));
    data = malloc(max_data_bytes);

    i = 0;
    for (;;)
    {
        value_len = max_value_len;
        data_size = max_data_bytes;
        rc = RegEnumValueW(hkey, i, value_name, &value_len, NULL, &type, data, &data_size);
        if (rc == ERROR_SUCCESS)
        {
            output_value(value_name, type, data, data_size);
            i++;
        }
        else if (rc == ERROR_MORE_DATA)
        {
            if (data_size > max_data_bytes)
            {
                max_data_bytes = data_size;
                data = realloc(data, max_data_bytes);
            }
            else
            {
                max_value_len *= 2;
                value_name = realloc(value_name, max_value_len * sizeof(WCHAR));
            }
        }
        else break;
    }

    free(data);
    free(value_name);

    if (i || recursing)
        output_string(newlineW);

    if (!num_subkeys)
        return 0;

    subkey_name = malloc(MAX_SUBKEY_LEN * sizeof(WCHAR));

    path_len = lstrlenW(path);

    i = 0;
    for (;;)
    {
        subkey_len = MAX_SUBKEY_LEN;
        rc = RegEnumKeyExW(hkey, i, subkey_name, &subkey_len, NULL, NULL, NULL, NULL);
        if (rc == ERROR_SUCCESS)
        {
            if (recurse)
            {
                subkey_path = build_subkey_path(path, path_len, subkey_name, subkey_len);
                if (!RegOpenKeyExW(hkey, subkey_name, 0, KEY_READ|sam, &subkey))
                {
                    query_all(subkey, subkey_path, recurse, TRUE);
                    RegCloseKey(subkey);
                }
                free(subkey_path);
            }
            else output_string(L"%1\\%2\n", path, subkey_name);
            i++;
        }
        else break;
    }

    free(subkey_name);
    return 0;
}

static int run_query(HKEY root, WCHAR *path, WCHAR *key_name, WCHAR *value_name,
                     BOOL value_empty, BOOL recurse, struct search *search)
{
    HKEY hkey;
    int ret = 0;

    if (RegOpenKeyExW(root, path, 0, KEY_READ|sam, &hkey))
    {
        output_error(STRING_KEY_NONEXIST);
        return 1;
    }

    output_string(newlineW);

    if (search)
    {
        search_key(search, hkey, key_name, NULL);
        output_message(STRING_MATCHES_FOUND, num_values_found);
        if (!num_values_found) ret = 1;
    }
    else if (value_name || value_empty)
    {
        ret = query_value(hkey, value_name, key_name, recurse);
        if (recurse)
            output_message(STRING_MATCHES_FOUND, num_values_found);
    }
    else
        ret = query_all(hkey, key_name, recurse, FALSE);

    RegCloseKey(hkey);

    return ret;
}

static BOOL parse_types(WCHAR *str, struct search *s)
{
    WCHAR *p = str, *next;
    DWORD i;

    s->types = malloc((wcslen(str) / 2 + 1) * sizeof(DWORD));
    s->type_count = 0;

    for (;;)
    {
        if ((next = wcschr(p, ','))) *next = 0;

        for (i = 0; i < ARRAY_SIZE(type_rels); i++)
            if (!lstrcmpiW(type_rels[i].name, p)) break;
        if (i == ARRAY_SIZE(type_rels)) return FALSE;
        s->types[s->type_count++] = type_rels[i].type;

        if (!next) break;
        p = next + 1;
    }
    return TRUE;
}

static BOOL is_switch_arg(const WCHAR *str)
{
    return str && (str[0] == '/' || str[0] == '-');
}

int reg_query(int argc, WCHAR *argvW[])
{
    HKEY root;
    WCHAR *path, *key_name, *value_name = NULL, *types = NULL;
    BOOL value_empty = FALSE, recurse = FALSE, v_switch = FALSE;
    BOOL keys = FALSE, data = FALSE, match_case = FALSE, exact = FALSE, separator = FALSE;
    struct search search = { 0 };
    WCHAR *pattern = NULL;
    BOOL find = FALSE;
    int i;

    if (!parse_registry_key(argvW[2], &root, &path))
        return 1;

    for (i = 3; i < argc; i++)
    {
        WCHAR *str;

        if (!is_switch_arg(argvW[i]))
            goto invalid;

        str = &argvW[i][1];

        if (!lstrcmpiW(str, L"ve"))
        {
            if (value_empty) goto invalid;
            value_empty = TRUE;
            continue;
        }
        else if (!lstrcmpiW(str, L"se"))
        {
            if (separator || !argvW[i + 1] || wcslen(argvW[i + 1]) != 1) goto invalid;
            separator = TRUE;
            multi_sz_separator = argvW[++i][0];
            continue;
        }
        else if (!lstrcmpiW(str, L"reg:32"))
        {
            if (sam & KEY_WOW64_32KEY) goto invalid;
            sam |= KEY_WOW64_32KEY;
            continue;
        }
        else if (!lstrcmpiW(str, L"reg:64"))
        {
            if (sam & KEY_WOW64_64KEY) goto invalid;
            sam |= KEY_WOW64_64KEY;
            continue;
        }
        else if (!str[0] || str[1])
            goto invalid;

        switch (towlower(*str))
        {
        case 'v':
            if (value_name || v_switch) goto invalid;
            /* With /f, a bare /v restricts the search to value names. */
            if (!argvW[i + 1] || is_switch_arg(argvW[i + 1]))
                v_switch = TRUE;
            else
                value_name = argvW[++i];
            break;
        case 's':
            if (recurse) goto invalid;
            recurse = TRUE;
            break;
        case 'f':
            if (find || !argvW[i + 1]) goto invalid;
            find = TRUE;
            pattern = argvW[++i];
            break;
        case 't':
            if (types || !argvW[i + 1]) goto invalid;
            types = argvW[++i];
            break;
        case 'k':
            if (keys) goto invalid;
            keys = TRUE;
            break;
        case 'd':
            if (data) goto invalid;
            data = TRUE;
            break;
        case 'c':
            if (match_case) goto invalid;
            match_case = TRUE;
            break;
        case 'e':
            if (exact) goto invalid;
            exact = TRUE;
            break;
        case 'z':
            if (verbose) goto invalid;
            verbose = TRUE;
            break;
        default:
            goto invalid;
        }
    }

    if (value_name && value_empty)
        goto invalid;

    if (sam == (KEY_WOW64_32KEY|KEY_WOW64_64KEY))
        goto invalid;

    /* /k, /d, /c, /e and a bare /v only make sense with /f. */
    if (!find && (keys || data || match_case || exact || v_switch))
        goto invalid;

    if (find || types)
    {
        if (value_name || value_empty) goto invalid;
        if (types && !parse_types(types, &search)) goto invalid;
        search.pattern = find ? pattern : NULL;
        search.keys = keys;
        search.names = v_switch;
        search.data = data;
        if (find && !keys && !v_switch && !data)
            search.keys = search.names = search.data = TRUE;
        search.match_case = match_case;
        search.exact = exact;
        search.recurse = recurse;
    }

    key_name = get_long_key(root, path);

    return run_query(root, path, key_name, value_name, value_empty, recurse,
                     (find || types) ? &search : NULL);

invalid:
    return syntax_error();
}
