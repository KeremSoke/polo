#!/bin/sh
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makes a release from a clean main: checks that meson.build, the Fedora spec
# and CHANGELOG.md agree on VERSION, builds the dist tarball with the patched
# Weston tree, writes its checksum and the release notes, and makes a signed
# tag. It pushes nothing.
#
#   tools/release.sh VERSION [--no-tests]

set -eu

die() { echo "release: $*" >&2; exit 1; }

[ $# -ge 1 ] || die "usage: tools/release.sh VERSION [--no-tests]"
version=$1
tests=
[ "${2:-}" = --no-tests ] && tests=--no-tests
tag=v$version
cd "$(git rev-parse --show-toplevel)"

echo "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || die "$version is not MAJOR.MINOR.PATCH"
[ "$(git branch --show-current)" = main ] || die "not on main"
[ -z "$(git status --porcelain)" ] || die "the tree is not clean"
git rev-parse -q --verify "refs/tags/$tag" >/dev/null && die "$tag exists"
git config user.signingkey >/dev/null || die "no git user.signingkey to sign the tag with"

grep -q "^  version: '$version'," meson.build || die "meson.build is not at $version"
grep -q "^Version: *$version\$" packaging/fedora/polo.spec || die "polo.spec is not at $version"
grep -q "^## $version " CHANGELOG.md || die "CHANGELOG.md has no $version section"

out=build-release-$version
rm -rf "$out"
meson setup "$out" --buildtype=release -Dtests=false >/dev/null
meson dist -C "$out" --include-subprojects --formats xztar $tests

tarball=$out/meson-dist/polo-$version.tar.xz
[ -f "$tarball" ] || die "no $tarball"
tar -tJf "$tarball" | grep -q "^polo-$version/subprojects/weston/libweston/" ||
	die "the tarball has no Weston tree"

notes=$out/meson-dist/release-notes-$version.md
awk -v v="## $version " 'index($0, v) == 1 { on = 1; next } /^## / { on = 0 } on' CHANGELOG.md >"$notes"
[ -s "$notes" ] || die "the $version section of CHANGELOG.md is empty"

git tag -s "$tag" -F "$notes"
git tag -v "$tag" >/dev/null 2>&1 || die "$tag does not verify"

cat <<EOF
Made $tag, $tarball and $notes.
To publish:
  git push origin $tag
  gh release create $tag $tarball $tarball.sha256sum --notes-file $notes --verify-tag
EOF
