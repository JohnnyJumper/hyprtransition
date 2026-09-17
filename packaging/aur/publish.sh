#!/bin/sh
# Pushes one package directory to the AUR:  publish.sh hyprtransition|hyprtransition-git ["commit message"]
# Needs an AUR account with your SSH key registered (https://aur.archlinux.org/account/).
set -e
cd "$(dirname "$0")/$1"
makepkg --printsrcinfo > .SRCINFO
version=$(sed -n 's/^\tpkgver = //p' .SRCINFO)
message=${2:-"Update to $version"}

clone=$(mktemp -d)
git clone -q "ssh://aur@aur.archlinux.org/$1.git" "$clone"
cp PKGBUILD .SRCINFO "$clone/"
cd "$clone"
git add PKGBUILD .SRCINFO
git commit -q -m "$message"
git push -q origin master
rm -rf "$clone"
echo "published $1 $version -> https://aur.archlinux.org/packages/$1"
