# personality/: NT personality and Win32 layer (Wine fork)

> **License: LGPL-2.1-or-later.** Everything in this directory is, or will be,
> derived from [Wine](https://www.winehq.org/) and stays under Wine's license.
> See `COPYING.LGPL` and the top-level `NOTICE`.

**Status: empty.** Today the oracle harness uses *stock Wine* as the stand-in
for Setu. This directory is where the fork will live.

## Fork plan

1. **Import.** Bring in the latest Wine stable release with full upstream
   history (`git subtree` or a `wine-upstream` branch), so `git blame` and
   rebases keep working.
2. **Build in CI.** Build with the new WoW64 mode (`--enable-archs=x86_64,i386`)
   and install into a private prefix. The harness then uses it as the Setu
   target (`oracle/config.toml` → `[setu] wine = ...`).
3. **Carry changes as a patch series.** Each Setu change is a self-contained
   commit with a `Setu-Oracle:` trailer that lists the oracle tests it fixes,
   and the clean-room sources it relies on.
4. **Rebase on every Wine stable release.** Upstream general fixes to Wine.
   Keep Setu-only behaviour (system session, registry phase B hooks, casefold
   fast path) behind clearly named modules.
5. **Respect the Host Interface seam** (docs/ARCHITECTURE.md §3.6). Linux-specific
   code stays on the unix side of ntdll and in wineserver.

## What will *not* go here

Setu's own MIT/Apache components (registry service, shell, distro tooling).
They live in `services/`, `shell/`, `distro/` and never copy code from here.
