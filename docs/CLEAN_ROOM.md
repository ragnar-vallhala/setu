# Clean-Room Rules

Setu reimplements Windows behaviour. To do that legally and ethically, we
work only from **public information and black-box observation**. These rules
bind every contributor: human, AI agent, or automated tool. Breaking them can
taint code and force its removal, so we take them seriously.

## 1. Never

* **Never disassemble, decompile, debug-step into, or otherwise inspect the
  contents of Windows binaries**: DLLs, EXEs, drivers, the kernel, firmware.
  That includes using `objdump`, IDA, Ghidra, radare2, a debugger, or "just
  looking at the import table" on Microsoft-shipped files.
* **Never use Microsoft debug symbols** (PDBs, the public symbol server) or
  anything derived from them (e.g. struct layouts dumped from symbols).
* **Never read leaked or "source-available under NDA" Microsoft source**
  (leaked Windows/NT source trees, the Windows Research Kernel, shared-source
  programmes, etc.). If you've read such material, tell the maintainers before
  you contribute to the affected areas.
* **Never paste the contents of any of the above** into code, comments, issues,
  commit messages, or prompts to AI tools.
* Don't use AI tools to "recall" Microsoft internals. Treat any non-public
  detail an AI produces as untrusted. Verify it against an allowed source, or
  observe it with the oracle.

## 2. Allowed sources

| Source | Examples |
|---|---|
| Public Microsoft documentation | learn.microsoft.com API reference, Open Specifications ([MS-*] protocol docs), published blog posts |
| Public headers and specs | MinGW-w64 headers, Wine headers, the PE/COFF spec, ECMA-335, UEFI/ACPI specs |
| Wine source | winehq.org git, including its conformance tests (LGPL: keep derived code in `personality/`) |
| Other clean-room projects | ReactOS documentation and *tests* (behavioural facts only; ReactOS code is GPL, so don't copy it into MIT/Apache areas) |
| **Black-box oracle** | Running our own test programs on a real Windows VM and recording **inputs and outputs only**: return values, error codes, resulting file and registry state, visible UI |

Black-box means: we control the inputs, we observe the outputs through public
APIs, and we never look at *how* Windows computes them.

## 3. The oracle stays black-box

* Test programs in `tests/win32/` call public APIs and print their results.
  They don't read Windows system files, dump memory, or inspect DLL code.
* Golden files (`tests/golden/`) contain only those printed results.
* The Windows VM is a measurement device. Don't install reverse-engineering
  tools on it, and don't copy files from `C:\Windows` out of it.

## 4. Every implementation PR records its sources

Each PR that changes behaviour in `personality/`, `services/`, `shell/`, or
`distro/` must include a **Clean-room sources** section (the PR template has
one):

```
## Clean-room sources
- Docs: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
- Oracle tests: tests/win32/kernel32/CreateFileW.c (cases: create_new_exists, open_dir_no_backup)
- Other: Wine dlls/kernelbase/file.c (upstream behaviour, LGPL)
- I confirm I did not inspect Windows binaries, symbols, or leaked source.
```

CI checks that this section exists on PRs touching those directories.

## 5. When in doubt, stop and ask

If a task seems to need looking inside Windows (for example, "what does this
undocumented struct look like?"), **stop and open an issue labelled
`clean-room-question`**. The usual answer is to design an oracle test that
observes the behaviour from outside. If that's impossible, the maintainers
decide, and the answer may be "we don't implement this".

AI agents working on Setu must follow the same rule. Stop and ask the human
maintainer instead of proceeding.

## 6. Trademarks

Don't use "Windows" or other Microsoft marks in Setu's name, logos, or
component names. Describe compatibility factually ("runs Windows
applications"). Setu isn't affiliated with Microsoft.
