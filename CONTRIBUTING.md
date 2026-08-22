# Contributing to MiniBar

Thanks for taking the time. MiniBar is small on purpose, and keeping it small is the main
thing a contribution has to respect. This page says what that means in practice.

## Before you start

Open an issue first for anything bigger than a typo or an obvious bug fix. The quickest way
to get a change merged is to agree on it before writing it — especially because two kinds
of change are refused on principle:

- **Anything that touches other processes from the inside**: DLL injection, DirectX/Vulkan
  hooks, global `SetWindowsHookEx`. MiniBar only uses public, out-of-process APIs, so it
  stays safe next to anti-cheat software. A feature that needs a hook does not get in, even
  if the hook is small.
- **Anything that breaks the performance budget**: under 15 MB of RAM, ~0 % CPU at rest, no
  GPU work between redraws, an executable under 2 MB (the CI fails the build otherwise),
  no render loop. If a feature needs a timer that runs when nothing is happening, it needs
  a different design.

The reasoning behind these rules is in [docs/design.md](docs/design.md) (Italian).

## Building

You need nothing installed: the toolchain is portable and lives in `tools\`.

```powershell
.\build.ps1                 # Release into dist\minibar.exe
.\build.ps1 -Config debug
.\build.ps1 -Clean
```

If `tools\` is empty (fresh clone), rebuild it as described in
[README.it.md](README.it.md#ricostruire-la-toolchain): `tools\get-msvc.ps1` downloads MSVC
and the Windows SDK from Microsoft's own packages and extracts them without installing
anything. The CI uses a separate CMake preset (`ci`) with the Visual Studio that GitHub's
runners provide.

The build must be warning-free at `/W4`; new warnings are treated as errors in review.

## Making a change

- One change per pull request. Small PRs get reviewed quickly; large ones wait.
- Match the surrounding code: C++20, Win32 and C++/WinRT, no third-party libraries, no
  exceptions across module boundaries, comments only where the code cannot say it.
- Everything that talks to another process goes through `PlayerManager` / `PlayerVolume` /
  `SmtcService`; the rest of the app must not know which player is running.
- Strings shown to the user are in English. Configuration keys go in `minibar.ini` and are
  documented in both READMEs.
- If you change how something looks, attach a screenshot to the PR. The screenshots and the
  GIF in `docs/media/` are generated, not taken from a desktop — ask in the issue if you need
  to regenerate them.
- Measure before and after. For anything that could affect the budget, put the numbers in
  the PR description: private working set and CPU at rest over 60 s with music playing, in
  the same mode and layout before and after.

## Commit messages

Short imperative subject, body explaining why when it is not obvious. No AI co-author
trailers or generated-by footers.

## Reporting bugs

Use the bug report template: it asks for the Windows build, the player, the mode and layout
you were in, and the contents of `minibar.ini`. Most bugs in an overlay are
environment-specific, and those four things are usually the whole diagnosis.
