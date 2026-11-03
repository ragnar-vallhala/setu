/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: local and global atom tables: AddAtomW / FindAtomW /
 * GetAtomNameW / DeleteAtom and the Global* versions, InitAtomTable, the
 * integer-atom range (#1234 strings, MAKEINTATOM up to 0xBFFF), case
 * insensitivity, name length limits (255), reference counting, and
 * GetAtomName buffer rules.
 *
 * Atom values above 0xC000 are allocated by the system, so only their range
 * and relationships are printed. Global atom names are unique to this test
 * and deleted again.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-addatomw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-findatomw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getatomnamew
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-deleteatom
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-globaladdatomw
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-initatomtable
 *       https://learn.microsoft.com/en-us/windows/win32/dataxchg/about-atom-tables
 */
#include "oracle.h"

static const char *atom_class(ATOM a)
{
    if (!a) return "zero";
    if (a < MAXINTATOM) return "integer";
    return "string";
}

static void name_case(const char *name, int global, ATOM a, int size)
{
    wchar_t buf[300];
    UINT r;
    wmemset(buf, L'#', 300);
    if (global) ORACLE_CALL(r, GlobalGetAtomNameW(a, buf, size));
    else ORACLE_CALL(r, GetAtomNameW(a, buf, size));
    oracle_case(name);
    o_u32("ret", r);
    o_lasterr();
    if (size > 0) {
        buf[size < 300 ? size : 299] = 0;
        o_wstr("buf", buf);
    }
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/atoms");

    ATOM a, b, c;
    BOOL ok;
    wchar_t n255[300], n256[300];
    for (int i = 0; i < 255; i++) n255[i] = L'a' + i % 26;
    n255[255] = 0;
    wcscpy(n256, n255);
    n256[255] = L'z';
    n256[256] = 0;

    ORACLE_CALL(ok, InitAtomTable(37));
    oracle_case("init_table");
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();

    /* Local string atoms. */
    ORACLE_CALL(a, AddAtomW(L"SetuOracleAtom"));
    oracle_case("add_string");
    o_str("class", atom_class(a));
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(b, AddAtomW(L"setuoracleATOM"));
    oracle_case("add_same_other_case");
    o_bool("same", a == b);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(c, FindAtomW(L"SETUORACLEATOM"));
    oracle_case("find_other_case");
    o_bool("same", a == c);
    o_lasterr();
    oracle_case_end();
    name_case("name_keeps_first_case", 0, a, 64);
    name_case("name_size_5", 0, a, 5);
    name_case("name_size_1", 0, a, 1);
    name_case("name_size_0", 0, a, 0);
    ORACLE_CALL(c, DeleteAtom(a));
    oracle_case("delete_once_still_referenced");
    o_u32("ret", c);
    o_lasterr();
    o_bool("still_found", FindAtomW(L"SetuOracleAtom") == a);
    oracle_case_end();
    ORACLE_CALL(c, DeleteAtom(a));
    oracle_case("delete_last_reference");
    o_u32("ret", c);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(c, FindAtomW(L"SetuOracleAtom"));
    oracle_case("find_after_delete");
    o_u32("ret", c);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(c, DeleteAtom(a));
    oracle_case("delete_again");
    o_bool("returned_atom", c == a);
    o_u32("ret_zero", c == 0);
    o_lasterr();
    oracle_case_end();
    name_case("name_of_deleted", 0, a, 64);

    /* Integer atoms. */
    ORACLE_CALL(a, AddAtomW(L"#1234"));
    oracle_case("add_integer_string");
    o_u32("ret", a);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW((LPCWSTR)MAKEINTATOM(1234)));
    oracle_case("add_makeintatom");
    o_u32("ret", a);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW(L"#0"));
    oracle_case("add_integer_zero");
    o_u32("ret", a);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW((LPCWSTR)MAKEINTATOM(0)));
    oracle_case("add_makeintatom_zero");
    o_u32("ret", a);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW(L"#49151"));
    oracle_case("add_integer_bfff");
    o_u32("ret", a);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW(L"#49152"));
    oracle_case("add_integer_c000");
    o_str("class", atom_class(a));
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW((LPCWSTR)MAKEINTATOM(0xc000)));
    oracle_case("add_makeintatom_c000");
    o_u32("ret", a);
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW(L"#12a"));
    oracle_case("add_integer_with_letters");
    o_str("class", atom_class(a));
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW(L"#0x10"));
    oracle_case("add_integer_hex_text");
    o_str("class", atom_class(a));
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW(L"#99999"));
    oracle_case("add_integer_too_big");
    o_str("class", atom_class(a));
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, AddAtomW(L"#"));
    oracle_case("add_hash_only");
    o_str("class", atom_class(a));
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(a, FindAtomW(L"#4321"));
    oracle_case("find_integer_never_added");
    o_u32("ret", a);
    o_lasterr();
    oracle_case_end();
    name_case("name_integer", 0, 1234, 64);
    name_case("name_integer_small_buffer", 0, 1234, 3);
    name_case("name_integer_zero", 0, 0, 64);
    ORACLE_CALL(c, DeleteAtom(1234));
    oracle_case("delete_integer");
    o_u32("ret", c);
    o_lasterr();
    oracle_case_end();

    /* Name limits. */
    ORACLE_CALL(a, AddAtomW(n255));
    oracle_case("add_255_chars");
    o_str("class", atom_class(a));
    o_lasterr();
    oracle_case_end();
    name_case("name_255_chars_full", 0, a, 300);
    ORACLE_CALL(b, AddAtomW(n256));
    oracle_case("add_256_chars");
    o_str("class", atom_class(b));
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(b, AddAtomW(L""));
    oracle_case("add_empty");
    o_str("class", atom_class(b));
    o_lasterr();
    oracle_case_end();
    ORACLE_CALL(b, AddAtomA("SetuOracleAnsi"));
    ORACLE_CALL(c, FindAtomW(L"SetuOracleAnsi"));
    oracle_case("ansi_and_wide_share_table");
    o_bool("same", b == c && b != 0);
    o_lasterr();
    oracle_case_end();
    {
        char abuf[8];
        UINT r;
        memset(abuf, '#', sizeof abuf);
        ORACLE_CALL(r, GetAtomNameA(b, abuf, 5));
        oracle_case("name_ansi_size_5");
        o_u32("ret", r);
        o_lasterr();
        o_bytes("buf", abuf, 6);
        oracle_case_end();
    }

    /* Global atoms are not tested here: whether GlobalAddAtom works depends on
     * the window station the process runs in (it fails with access denied in a
     * non-interactive session), which is a property of the test machine. */

    return oracle_end();
}
