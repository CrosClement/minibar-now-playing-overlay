# Packaging

Manifests for the package managers. Nothing here is submitted automatically: these are the
files to hand to winget and Scoop when a release is ready.

Both manifests pin the SHA256 of the executable **attached to the release**, which is not
the same file you get from a local build — MSVC does not produce bit-identical binaries
across builds, so two compilations of the same source have the same size and different
hashes. Always hash the released asset:

```powershell
gh release download v1.0.0 --repo AceisX/minibar-now-playing-overlay --pattern minibar.exe --dir .
Get-FileHash minibar.exe -Algorithm SHA256
```

## winget — `winget/`

Three files, as winget requires: version, installer, and default locale. The package is
declared `portable`, which is what MiniBar is: winget puts the executable under its own
Links directory and creates a `minibar` command, with nothing written to the registry.

Before submitting, validate them with the winget client itself:

```powershell
winget validate --manifest packaging\winget
```

To submit, open a pull request against [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs)
with the three files under `manifests/a/AceisX/MiniBar/1.0.0/`. The
[wingetcreate](https://github.com/microsoft/winget-create) tool automates that:

```powershell
wingetcreate update AceisX.MiniBar --version 1.1.0 --urls <url-of-the-new-exe> --submit
```

The repository must be public and the release reachable without authentication, otherwise
the automated validation in winget-pkgs cannot download the installer.

## Scoop — `scoop/minibar.json`

Ready for a personal bucket, or for a pull request to
[ScoopInstaller/Extras](https://github.com/ScoopInstaller/Extras).

`checkver` and `autoupdate` are wired to the GitHub releases, so `scoop update` and the
bucket's own automation pick up a new version by themselves — as long as the asset keeps
the name `minibar.exe` and the tag keeps the form `v<version>`.

`persist` is set on `minibar.ini`: Scoop installs each version in its own directory, so
without it the configuration — mode, position, size, chosen player — would be lost at every
update.

## When a new version goes out

1. Publish the release with `minibar.exe` attached (the CI does this on a `v*` tag).
2. Hash the released asset, as above.
3. In `winget/`, bump `PackageVersion` in all three files, and update `InstallerUrl`,
   `InstallerSha256`, `ReleaseDate` and `ReleaseNotesUrl` in the installer and locale
   manifests. Re-run `winget validate`.
4. In `scoop/minibar.json`, bump `version`, `url` and `hash`.
