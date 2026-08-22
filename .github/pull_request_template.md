<!-- One change per PR. For anything bigger than a typo or an obvious fix, link the issue where it was discussed. -->

## What and why

<!-- What changes, and the reason. Link the issue. -->

## Checklist

- [ ] `.\build.ps1` is warning-free at `/W4`
- [ ] No hook, no injection, nothing that touches another process from the inside
- [ ] Nothing runs when nothing is happening (no new timer that stays on at rest)
- [ ] If it can affect the budget: private working set and CPU at rest measured before and after, numbers below
- [ ] If it changes something visible: screenshot attached
- [ ] If it adds a setting: documented in `README.md` and `README.it.md`, default written in `Config::WriteDefaultsIfMissing`

## Measurements (if relevant)

<!-- Mode/layout, 60 s at rest with music playing. Before → after. -->
