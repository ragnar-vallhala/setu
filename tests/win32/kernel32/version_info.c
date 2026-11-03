/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: Windows version APIs for an executable without a
 * compatibility manifest: GetVersion, GetVersionExW/A (structure size
 * checks, the reported version), RtlGetVersion (the real version),
 * VerifyVersionInfoW with every comparison style, VerSetConditionMask, and
 * the version numbers in KUSER_SHARED_DATA.
 *
 * The goldens come from Windows 10 22H2 (10.0.19045); suite masks and
 * product types that depend on the edition are not printed.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversion
 *       https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversionexw
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/targeting-your-application-at-windows-8-1
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlgetversion
 *       https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-verifyversioninfow
 *       https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-versetconditionmask
 *       https://learn.microsoft.com/en-us/windows/win32/sysinfo/verifying-the-system-version
 *       https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-kuser_shared_data
 */
#include "oracle.h"

typedef LONG(WINAPI *rtl_get_version_fn)(OSVERSIONINFOEXW *);

static void verify_case(const char *name, DWORD major, DWORD minor, DWORD build, DWORD mask, BYTE op)
{
    OSVERSIONINFOEXW v;
    memset(&v, 0, sizeof v);
    v.dwOSVersionInfoSize = sizeof v;
    v.dwMajorVersion = major;
    v.dwMinorVersion = minor;
    v.dwBuildNumber = build;
    ULONGLONG cond = 0;
    if (mask & VER_MAJORVERSION) cond = VerSetConditionMask(cond, VER_MAJORVERSION, op);
    if (mask & VER_MINORVERSION) cond = VerSetConditionMask(cond, VER_MINORVERSION, op);
    if (mask & VER_BUILDNUMBER) cond = VerSetConditionMask(cond, VER_BUILDNUMBER, op);
    BOOL ok;
    ORACLE_CALL(ok, VerifyVersionInfoW(&v, mask, cond));
    oracle_case(name);
    o_bool("ret", ok);
    o_lasterr();
    oracle_case_end();
}

int main(void)
{
    oracle_begin("kernel32/version_info");
    BOOL ok;

    {
        DWORD v;
        ORACLE_CALL(v, GetVersion());
        oracle_case("get_version");
        o_u32("major", LOBYTE(LOWORD(v)));
        o_u32("minor", HIBYTE(LOWORD(v)));
        o_u32("build", HIWORD(v) & 0x7fff);
        o_bool("high_bit", (v & 0x80000000) != 0);
        o_lasterr();
        oracle_case_end();
    }

    {
        OSVERSIONINFOW v;
        memset(&v, 0xcc, sizeof v);
        v.dwOSVersionInfoSize = sizeof v;
        ORACLE_CALL(ok, GetVersionExW(&v));
        oracle_case("get_version_ex_w");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("major", v.dwMajorVersion);
        o_u32("minor", v.dwMinorVersion);
        o_u32("build", v.dwBuildNumber);
        o_u32("platform", v.dwPlatformId);
        o_wstr("csd", v.szCSDVersion);
        o_u32("size_after", v.dwOSVersionInfoSize);
        oracle_case_end();
    }
    {
        OSVERSIONINFOEXW v;
        memset(&v, 0xcc, sizeof v);
        v.dwOSVersionInfoSize = sizeof v;
        ORACLE_CALL(ok, GetVersionExW((OSVERSIONINFOW *)&v));
        oracle_case("get_version_ex_w_ex_struct");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("major", v.dwMajorVersion);
        o_u32("minor", v.dwMinorVersion);
        o_u32("build", v.dwBuildNumber);
        o_u32("sp_major", v.wServicePackMajor);
        o_u32("sp_minor", v.wServicePackMinor);
        o_u32("reserved", v.wReserved);
        o_bool("product_type_workstation", v.wProductType == VER_NT_WORKSTATION);
        oracle_case_end();
    }
    {
        OSVERSIONINFOW v;
        memset(&v, 0, sizeof v);
        v.dwOSVersionInfoSize = sizeof v - 1;
        ORACLE_CALL(ok, GetVersionExW(&v));
        oracle_case("get_version_ex_w_bad_size");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        v.dwOSVersionInfoSize = sizeof v + 4;
        ORACLE_CALL(ok, GetVersionExW(&v));
        oracle_case("get_version_ex_w_size_plus_4");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        v.dwOSVersionInfoSize = 0;
        ORACLE_CALL(ok, GetVersionExW(&v));
        oracle_case("get_version_ex_w_size_0");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }
    {
        OSVERSIONINFOA v;
        memset(&v, 0xcc, sizeof v);
        v.dwOSVersionInfoSize = sizeof v;
        ORACLE_CALL(ok, GetVersionExA(&v));
        oracle_case("get_version_ex_a");
        o_bool("ret", ok);
        o_lasterr();
        o_u32("major", v.dwMajorVersion);
        o_u32("minor", v.dwMinorVersion);
        o_u32("build", v.dwBuildNumber);
        o_str("csd", v.szCSDVersion);
        oracle_case_end();
        v.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
        ORACLE_CALL(ok, GetVersionExA(&v));
        oracle_case("get_version_ex_a_w_size");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    /* ---- RtlGetVersion: not subject to the manifest ---- */
    {
        rtl_get_version_fn rgv = (rtl_get_version_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
        OSVERSIONINFOEXW v;
        memset(&v, 0xcc, sizeof v);
        v.dwOSVersionInfoSize = sizeof v;
        LONG st = rgv(&v);
        oracle_case("rtl_get_version");
        o_hex32("status", st);
        o_u32("major", v.dwMajorVersion);
        o_u32("minor", v.dwMinorVersion);
        o_u32("build", v.dwBuildNumber);
        o_u32("platform", v.dwPlatformId);
        o_wstr("csd", v.szCSDVersion);
        o_u32("sp_major", v.wServicePackMajor);
        o_bool("product_type_workstation", v.wProductType == VER_NT_WORKSTATION);
        oracle_case_end();
        memset(&v, 0, sizeof v);
        v.dwOSVersionInfoSize = 12;
        st = rgv(&v);
        oracle_case("rtl_get_version_bad_size");
        o_hex32("status", st);
        oracle_case_end();
    }

    /* ---- KUSER_SHARED_DATA ---- */
    {
        const volatile BYTE *ksd = (const BYTE *)0x7ffe0000;
        oracle_case("kuser_shared_data");
        o_u32("nt_major", *(const volatile ULONG *)(ksd + 0x26c));
        o_u32("nt_minor", *(const volatile ULONG *)(ksd + 0x270));
        o_u32("nt_build", *(const volatile ULONG *)(ksd + 0x260));
        o_u32("product_type_workstation", *(const volatile ULONG *)(ksd + 0x264) == VER_NT_WORKSTATION);
        o_bool("product_type_valid", *(const volatile BYTE *)(ksd + 0x268) != 0);
        o_u32("image_number_low", *(const volatile USHORT *)(ksd + 0x2c));
        o_u32("image_number_high", *(const volatile USHORT *)(ksd + 0x2e));
        oracle_case_end();
    }

    /* ---- VerSetConditionMask ---- */
    {
        oracle_case("ver_set_condition_mask");
        o_u64("major_ge", VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL));
        o_u64("minor_eq", VerSetConditionMask(0, VER_MINORVERSION, VER_EQUAL));
        o_u64("build_lt", VerSetConditionMask(0, VER_BUILDNUMBER, VER_LESS));
        o_u64("platform_eq", VerSetConditionMask(0, VER_PLATFORMID, VER_EQUAL));
        o_u64("sp_major_ge", VerSetConditionMask(0, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL));
        o_u64("suite_and", VerSetConditionMask(0, VER_SUITENAME, VER_AND));
        o_u64("product_eq", VerSetConditionMask(0, VER_PRODUCT_TYPE, VER_EQUAL));
        o_u64("two_bits_last_wins", VerSetConditionMask(0, VER_MAJORVERSION | VER_MINORVERSION, VER_LESS));
        o_u64("op_zero", VerSetConditionMask(0, VER_MAJORVERSION, 0));
        o_u64("mask_zero", VerSetConditionMask(0x1234, 0, VER_EQUAL));
        o_u64("combine", VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER), VER_MINORVERSION, VER_LESS_EQUAL));
        o_u64("op_8", VerSetConditionMask(0, VER_MAJORVERSION, 8));
        oracle_case_end();
        oracle_case("probe_ver_set_condition_mask");
        o_u64("input_kept_op_zero", VerSetConditionMask(0x1234, VER_MAJORVERSION, 0));
        o_u64("input_kept_op_8", VerSetConditionMask(0x1234, VER_MAJORVERSION, 8));
        o_u64("input_kept_op_9", VerSetConditionMask(0x1234, VER_MAJORVERSION, 9));
        o_u64("input_kept_op_eq", VerSetConditionMask(0x1234, VER_MAJORVERSION, VER_EQUAL));
        o_u64("mask_zero_op_zero", VerSetConditionMask(0x1234, 0, 0));
        o_u64("unknown_type_bit", VerSetConditionMask(0x1234, 0x100, VER_EQUAL));
        o_u64("high_input_mask_zero", VerSetConditionMask(0x8000000000001234ull, 0, VER_EQUAL));
        o_u64("sp_minor_eq", VerSetConditionMask(0, VER_SERVICEPACKMINOR, VER_EQUAL));
        o_u64("replace_existing_slot", VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION, VER_LESS), VER_MAJORVERSION, VER_EQUAL));
        oracle_case_end();
    }

    /* ---- VerifyVersionInfoW ---- */
    verify_case("verify_major_ge_6", 6, 0, 0, VER_MAJORVERSION, VER_GREATER_EQUAL);
    verify_case("verify_major_ge_10", 10, 0, 0, VER_MAJORVERSION, VER_GREATER_EQUAL);
    verify_case("verify_major_eq_6", 6, 0, 0, VER_MAJORVERSION, VER_EQUAL);
    verify_case("verify_major_minor_ge_6_2", 6, 2, 0, VER_MAJORVERSION | VER_MINORVERSION, VER_GREATER_EQUAL);
    verify_case("verify_major_minor_ge_6_3", 6, 3, 0, VER_MAJORVERSION | VER_MINORVERSION, VER_GREATER_EQUAL);
    verify_case("verify_major_minor_eq_6_2", 6, 2, 0, VER_MAJORVERSION | VER_MINORVERSION, VER_EQUAL);
    verify_case("verify_major_lt_7", 7, 0, 0, VER_MAJORVERSION, VER_LESS);
    verify_case("verify_major_gt_5", 5, 0, 0, VER_MAJORVERSION, VER_GREATER);
    verify_case("verify_build_ge_9200", 0, 0, 9200, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    verify_case("verify_build_ge_19045", 0, 0, 19045, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    verify_case("verify_build_le_19045", 0, 0, 19045, VER_BUILDNUMBER, VER_LESS_EQUAL);
    verify_case("verify_minor_only_eq_0", 0, 0, 0, VER_MINORVERSION, VER_EQUAL);
    {
        OSVERSIONINFOEXW v;
        memset(&v, 0, sizeof v);
        v.dwOSVersionInfoSize = sizeof v;
        v.dwMajorVersion = 6;
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, 0, 0));
        oracle_case("verify_mask_zero");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_MAJORVERSION, 0));
        oracle_case("verify_condition_zero");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        v.dwMajorVersion = 7;
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_MAJORVERSION, 0));
        oracle_case("probe_verify_condition_zero_major_7");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_MAJORVERSION, VER_GREATER_EQUAL << 3));
        oracle_case("probe_verify_raw_ge_no_high_bit_major_7");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_MAJORVERSION, VER_LESS << 3));
        oracle_case("probe_verify_raw_less_no_high_bit_major_7");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_MAJORVERSION, 0x8000000000000000ull));
        oracle_case("probe_verify_only_high_bit_major_7");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_MAJORVERSION, VER_EQUAL << 0));
        oracle_case("probe_verify_condition_in_minor_slot_major_7");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        v.dwMajorVersion = 6;
        v.dwMinorVersion = 9;
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_MAJORVERSION | VER_MINORVERSION, VerSetConditionMask(0, VER_MAJORVERSION, VER_EQUAL)));
        oracle_case("probe_verify_minor_without_condition");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        v.dwMinorVersion = 0;
        v.dwPlatformId = VER_PLATFORM_WIN32_NT;
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_PLATFORMID, VerSetConditionMask(0, VER_PLATFORMID, VER_EQUAL)));
        oracle_case("verify_platform_nt");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        v.wServicePackMajor = 0;
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_SERVICEPACKMAJOR, VerSetConditionMask(0, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL)));
        oracle_case("verify_sp_ge_0");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        v.wServicePackMajor = 1;
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_SERVICEPACKMAJOR, VerSetConditionMask(0, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL)));
        oracle_case("verify_sp_ge_1");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        v.wProductType = VER_NT_WORKSTATION;
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_PRODUCT_TYPE, VerSetConditionMask(0, VER_PRODUCT_TYPE, VER_EQUAL)));
        oracle_case("verify_product_workstation");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
        v.dwMajorVersion = 10;
        v.dwMinorVersion = 0;
        ORACLE_CALL(ok, VerifyVersionInfoW(&v, VER_MAJORVERSION | VER_MINORVERSION,
                                           VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION, VER_LESS),
                                                               VER_MINORVERSION, VER_EQUAL)));
        oracle_case("verify_major_less_minor_equal");
        o_bool("ret", ok);
        o_lasterr();
        oracle_case_end();
    }

    return oracle_end();
}
