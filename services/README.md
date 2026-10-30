# services/: Windows-compatible system services

**Status: stub.** License: MIT OR Apache-2.0.

Planned components (see docs/ARCHITECTURE.md §3.3):

| Component | Purpose |
|---|---|
| `registryd/` | Registry phase B: transactional backing store behind the wineserver registry protocol |
| `scm/` | Service Control Manager extensions and the `setu-system.service` systemd unit |
| `fs/` | Filesystem semantics: ext4-casefold volume setup, drive-letter / mount service, DOS attribute and NT ACL xattr mapping |
| `identity/` | SID ↔ Linux uid/gid mapping |

Every behaviour here is specified by oracle tests in `tests/win32/` (registry:
`tests/win32/advapi32/`).
