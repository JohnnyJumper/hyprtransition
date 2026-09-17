# AUR packages

- `hyprtransition` — stable, built from the release tag. Bump `pkgver` and `sha256sums` per release.
- `hyprtransition-git` — tracks `main`.

Each directory is what gets pushed to its AUR repository (`PKGBUILD` + `.SRCINFO`).
After editing a PKGBUILD, regenerate `.SRCINFO` from inside its directory:

    makepkg --printsrcinfo > .SRCINFO

Test-build without installing: `makepkg -f`. Lint: `namcap PKGBUILD *.pkg.tar.zst`.
