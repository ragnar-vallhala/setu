/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * Oracle test: GetBinaryTypeW on the test's own executable and on files with
 * crafted headers (DOS MZ, NE, LE, PE32, PE32+, DLL, other machines),
 * script/text files, by-extension types (.com, .pif) and error paths. The
 * files are only inspected, never run.
 *
 * Docs: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getbinarytypew
 *       https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
 */
#include "oracle.h"

static void write_bytes(const wchar_t *path, const void *data, DWORD n)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w;
    if (n) WriteFile(h, data, n, &w, NULL);
    CloseHandle(h);
}

static void bt_case(const char *name, const wchar_t *path)
{
    DWORD type = 0xAAAAAAAA;
    BOOL r;
    ORACLE_CALL(r, GetBinaryTypeW(path, &type));
    oracle_case(name);
    o_bool("ret", r);
    o_lasterr();
    if (type == 0xAAAAAAAA) o_str("type", "untouched");
    else o_u32("type", type);
    oracle_case_end();
}

/* Builds an image: MZ header, e_lfanew at 0x3c, then a signature at 0x80. */
static DWORD build(unsigned char *buf, const char sig[4], WORD machine, WORD chars, WORD magic, WORD subsystem)
{
    memset(buf, 0, 1024);
    buf[0] = 'M';
    buf[1] = 'Z';
    buf[0x18] = 0x40; /* e_lfarlc >= 0x40: new-format executable */
    *(DWORD *)(buf + 0x3c) = 0x80;
    memcpy(buf + 0x80, sig, 4);
    if (!memcmp(sig, "PE\0\0", 4)) {
        IMAGE_FILE_HEADER *fh = (IMAGE_FILE_HEADER *)(buf + 0x84);
        fh->Machine = machine;
        fh->NumberOfSections = 0;
        fh->SizeOfOptionalHeader = magic == 0x20b ? sizeof(IMAGE_OPTIONAL_HEADER64) : sizeof(IMAGE_OPTIONAL_HEADER32);
        fh->Characteristics = chars;
        unsigned char *oh = buf + 0x84 + sizeof(IMAGE_FILE_HEADER);
        *(WORD *)oh = magic;
        if (magic == 0x20b) {
            IMAGE_OPTIONAL_HEADER64 *o = (IMAGE_OPTIONAL_HEADER64 *)oh;
            o->ImageBase = 0x140000000ULL;
            o->SectionAlignment = 0x1000;
            o->FileAlignment = 0x200;
            o->MajorSubsystemVersion = 6;
            o->SizeOfImage = 0x1000;
            o->SizeOfHeaders = 0x200;
            o->Subsystem = subsystem;
            o->NumberOfRvaAndSizes = 16;
        } else {
            IMAGE_OPTIONAL_HEADER32 *o = (IMAGE_OPTIONAL_HEADER32 *)oh;
            o->ImageBase = 0x400000;
            o->SectionAlignment = 0x1000;
            o->FileAlignment = 0x200;
            o->MajorSubsystemVersion = 6;
            o->SizeOfImage = 0x1000;
            o->SizeOfHeaders = 0x200;
            o->Subsystem = subsystem;
            o->NumberOfRvaAndSizes = 16;
        }
    } else if (!memcmp(sig, "NE", 2)) {
        buf[0x80 + 0x36] = 2; /* target OS: Windows */
    }
    return 1024;
}

int main(void)
{
    oracle_begin("kernel32/GetBinaryTypeW");
    unsigned char img[1024];
    DWORD n;
    const WORD EXE = IMAGE_FILE_EXECUTABLE_IMAGE;

    {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(NULL, self, MAX_PATH);
        bt_case("self_exe", self);
    }

    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_I386, EXE | IMAGE_FILE_32BIT_MACHINE, 0x10b, 3);
    write_bytes(L"pe32.exe", img, n);
    bt_case("pe32_i386_console", L"pe32.exe");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_I386, EXE | IMAGE_FILE_32BIT_MACHINE, 0x10b, 2);
    write_bytes(L"pe32gui.exe", img, n);
    bt_case("pe32_i386_gui", L"pe32gui.exe");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_AMD64, EXE | IMAGE_FILE_LARGE_ADDRESS_AWARE, 0x20b, 3);
    write_bytes(L"pe64.exe", img, n);
    bt_case("pe32plus_amd64", L"pe64.exe");
    write_bytes(L"pe64.bin", img, n);
    bt_case("pe32plus_amd64_other_extension", L"pe64.bin");
    write_bytes(L"pe64noext", img, n);
    bt_case("pe32plus_amd64_no_extension", L"pe64noext");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_AMD64, EXE | IMAGE_FILE_DLL, 0x20b, 3);
    write_bytes(L"lib64.dll", img, n);
    bt_case("pe32plus_dll", L"lib64.dll");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_I386, EXE | IMAGE_FILE_DLL | IMAGE_FILE_32BIT_MACHINE, 0x10b, 3);
    write_bytes(L"lib32.dll", img, n);
    bt_case("pe32_dll", L"lib32.dll");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_ARM64, EXE, 0x20b, 3);
    write_bytes(L"arm64.exe", img, n);
    bt_case("pe32plus_arm64", L"arm64.exe");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_ARMNT, EXE | IMAGE_FILE_32BIT_MACHINE, 0x10b, 3);
    write_bytes(L"armnt.exe", img, n);
    bt_case("pe32_armnt", L"armnt.exe");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_IA64, EXE, 0x20b, 3);
    write_bytes(L"ia64.exe", img, n);
    bt_case("pe32plus_ia64", L"ia64.exe");
    n = build(img, "PE\0\0", 0x1234, EXE, 0x10b, 3);
    write_bytes(L"unknown_machine.exe", img, n);
    bt_case("pe_unknown_machine", L"unknown_machine.exe");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_I386, 0, 0x10b, 3);
    write_bytes(L"notexec.exe", img, n);
    bt_case("pe32_not_executable_flag", L"notexec.exe");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_AMD64, EXE, 0x10b, 3);
    write_bytes(L"mismatch.exe", img, n);
    bt_case("amd64_with_pe32_magic", L"mismatch.exe");
    n = build(img, "PE\0\0", IMAGE_FILE_MACHINE_I386, EXE, 0x10b, 3);
    write_bytes(L"truncated.exe", img, 0x90);
    bt_case("pe_truncated_after_signature", L"truncated.exe");

    n = build(img, "NE\0\0", 0, 0, 0, 0);
    write_bytes(L"ne.exe", img, n);
    bt_case("ne_windows", L"ne.exe");
    img[0x80 + 0x36] = 1; /* OS/2 */
    write_bytes(L"ne_os2.exe", img, n);
    bt_case("ne_os2", L"ne_os2.exe");
    n = build(img, "LE\0\0", 0, 0, 0, 0);
    write_bytes(L"le.exe", img, n);
    bt_case("le", L"le.exe");
    n = build(img, "LX\0\0", 0, 0, 0, 0);
    write_bytes(L"lx.exe", img, n);
    bt_case("lx", L"lx.exe");
    n = build(img, "XX\0\0", 0, 0, 0, 0);
    write_bytes(L"unknown_sig.exe", img, n);
    bt_case("unknown_new_signature", L"unknown_sig.exe");

    /* Plain DOS: MZ with e_lfarlc < 0x40. */
    memset(img, 0, sizeof img);
    img[0] = 'M';
    img[1] = 'Z';
    img[0x18] = 0x1c;
    write_bytes(L"dos.exe", img, 512);
    bt_case("dos_mz", L"dos.exe");
    write_bytes(L"dos_tiny.exe", "MZ", 2);
    bt_case("mz_two_bytes", L"dos_tiny.exe");
    write_bytes(L"zm.exe", "ZM\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32);
    bt_case("zm_signature", L"zm.exe");

    /* By extension or content. */
    write_bytes(L"prog.com", "\xb4\x4c\xcd\x21", 4);
    bt_case("com_extension", L"prog.com");
    write_bytes(L"prog.COM", "\xb4\x4c\xcd\x21", 4); /* same file, other case */
    bt_case("com_extension_upper", L"prog.COM");
    write_bytes(L"prog_com.bin", "\xb4\x4c\xcd\x21", 4);
    bt_case("com_bytes_other_extension", L"prog_com.bin");
    {
        unsigned char pif[545];
        memset(pif, 0, sizeof pif);
        write_bytes(L"prog.pif", pif, sizeof pif);
        bt_case("pif_extension", L"prog.pif");
    }
    write_bytes(L"script.bat", "@echo off\r\n", 11);
    bt_case("bat_file", L"script.bat");
    write_bytes(L"script.cmd", "@echo off\r\n", 11);
    bt_case("cmd_file", L"script.cmd");
    write_bytes(L"text.txt", "hello world\r\n", 13);
    bt_case("text_file", L"text.txt");
    write_bytes(L"text.exe", "hello world\r\n", 13);
    bt_case("text_with_exe_extension", L"text.exe");
    write_bytes(L"empty.exe", "", 0);
    bt_case("empty_exe", L"empty.exe");
    write_bytes(L"elf.exe", "\x7f" "ELF\x02\x01\x01\0\0\0\0\0\0\0\0\0", 16);
    bt_case("elf_header", L"elf.exe");

    bt_case("missing", L"missing.exe");
    bt_case("missing_dir", L"nodir\\x.exe");
    CreateDirectoryW(L"d.exe", NULL);
    bt_case("directory", L"d.exe");
    bt_case("empty_path", L"");
    {
        HANDLE h = CreateFileW(L"pe64.exe", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        bt_case("locked_exclusive", L"pe64.exe");
        CloseHandle(h);
    }
    {
        DWORD type = 0xAAAAAAAA;
        BOOL r;
        ORACLE_CALL(r, GetBinaryTypeA("pe32.exe", &type));
        oracle_case("ansi_pe32");
        o_bool("ret", r);
        o_lasterr();
        o_u32("type", type);
        oracle_case_end();
    }

    return oracle_end();
}
