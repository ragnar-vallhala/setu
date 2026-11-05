/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * odd_data.h: values whose size does not fit their type (the same set as
 * reg/query_odd_data), for the export and round-trip tests.
 */
#ifndef SETU_ODD_DATA_H
#define SETU_ODD_DATA_H

#include "regtool.h"

static void od_set(HKEY k, const wchar_t *name, DWORD type, const void *data, DWORD size)
{
    RegSetValueExW(k, name, 0, type, (const BYTE *)data, size);
}

static void od_set_all(HKEY k)
{
    static const BYTE b8[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    od_set(k, L"sz_no_nul", REG_SZ, L"abc", 6);
    od_set(k, L"sz_embedded_nul", REG_SZ, L"ab\0cd", 12);
    od_set(k, L"sz_odd_size", REG_SZ, L"abc", 5);
    od_set(k, L"sz_zero", REG_SZ, NULL, 0);
    od_set(k, L"expand_no_nul", REG_EXPAND_SZ, L"%x%", 6);
    od_set(k, L"expand_zero", REG_EXPAND_SZ, NULL, 0);
    od_set(k, L"dword_short", REG_DWORD, b8, 2);
    od_set(k, L"dword_long", REG_DWORD, b8, 6);
    od_set(k, L"dword_zero", REG_DWORD, NULL, 0);
    od_set(k, L"dword_be", REG_DWORD_BIG_ENDIAN, b8, 4);
    od_set(k, L"qword_short", REG_QWORD, b8, 4);
    od_set(k, L"qword_long", REG_QWORD, b8, 9);
    od_set(k, L"qword_zero", REG_QWORD, NULL, 0);
    od_set(k, L"multi_no_final", REG_MULTI_SZ, L"a\0b", 8);
    od_set(k, L"multi_empty_inside", REG_MULTI_SZ, L"a\0\0b\0", 12);
    od_set(k, L"multi_one_nul", REG_MULTI_SZ, L"", 2);
    od_set(k, L"multi_zero", REG_MULTI_SZ, NULL, 0);
    od_set(k, L"multi_odd", REG_MULTI_SZ, L"ab\0", 7);
    od_set(k, L"link", REG_LINK, L"\\Registry\\x", 22);
    od_set(k, L"resource_list", REG_RESOURCE_LIST, b8, 3);
    od_set(k, L"full_resource", REG_FULL_RESOURCE_DESCRIPTOR, b8, 3);
    od_set(k, L"requirements", REG_RESOURCE_REQUIREMENTS_LIST, b8, 3);
    od_set(k, L"type_big", 0x7fffffff, b8, 2);
    od_set(k, L"none_zero", REG_NONE, NULL, 0);
    od_set(k, L"binary_zero", REG_BINARY, NULL, 0);
}

#endif /* SETU_ODD_DATA_H */
