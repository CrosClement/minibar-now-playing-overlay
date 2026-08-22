# Security policy

## What MiniBar does and does not do

MiniBar is a single, unprivileged executable. It never injects code into other processes,
never installs hooks, never runs a service, never opens a network connection and never
writes outside its own folder (`minibar.ini` next to the executable). It reads other
applications only through public Windows APIs — the media session manager, Core Audio, and
window enumeration — the same ones the Windows volume flyout uses.

That makes the attack surface small, but not zero: it parses cover art delivered by other
applications (through WIC) and reads an INI file from its own folder.

## Supported versions

Only the latest release gets security fixes. There is no long-term support branch.

## Reporting a vulnerability

Please do **not** open a public issue for a security problem.

Use GitHub's private vulnerability reporting on this repository (*Security* tab →
*Report a vulnerability*). If that is not available to you, contact the maintainer through
their GitHub profile ([@AceisX](https://github.com/AceisX)) and ask for a private channel
before sending details.

Include what you can: the release version, Windows build, the player involved, and steps to
reproduce. You will get an acknowledgement within a few days; fixes ship as a new release
with the advisory published alongside it, and you will be credited unless you prefer not to
be.
