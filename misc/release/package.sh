#!/bin/bash
#
# A release's Linux and Windows downloads from CI's build artifacts (out-linux-amd64, out-windows-x64), the version
# cmake/identity.cmake's:
# - client, mohreloaded-<version>-<platform>: the build's files without the dedicated server, plus COPYING.txt,
#   this folder's INSTALL-<platform>.txt as INSTALL.txt and this folder's licenses/ (with the Omaha sound's,
#   OMAHA-SOUND-LICENSES.txt, moved into licenses/omaha-sound/);
# - server, mohreloaded-server-<version>-<platform>: the dedicated server and the game module only, plus COPYING.txt
#   and this folder's INSTALL-server.txt as INSTALL.txt. The libraries built into those two (zlib, Recast & Detour)
#   ask for no notice in a download.
# INSTALL.txt gets the version and the commit. A download must hold exactly the files in files-<platform>.txt
# (files-<platform>-server.txt), so a build that adds or drops a file stops here instead of shipping. Linux's programs
# go out without their debugging data (strip --strip-debug), as Windows' do (no .pdb): the CI artifact keeps it.
#
#   misc/release/package.sh <linux|windows> <client|server> <artifact dir> <output dir> <commit>
#
# Prints the archive's path. .github/workflows/branches-build.yml runs it for each push to vulkan that changes code,
# tags-publish-release.yml for a version tag.
#
set -euo pipefail

die() { echo "package.sh: $*" >&2; exit 1; }

[ $# -eq 5 ] || die "usage: package.sh <linux|windows> <client|server> <artifact dir> <output dir> <commit>"
platform=$1
kind=$2
artifact=$3
out=$4
commit=$5

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
version=$(sed -n 's/^set(PROJECT_VERSION \([0-9][0-9.]*\))$/\1/p' "$root/cmake/identity.cmake")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "no version in cmake/identity.cmake"
case $platform in
linux) exe=; lib=.so;;
windows) exe=.exe; lib=.dll;;
*) die "unknown platform: $platform";;
esac
case $kind in
client) name="mohreloaded-$version-$platform"; list="$here/files-$platform.txt"; install="$here/INSTALL-$platform.txt";;
server) name="mohreloaded-server-$version-$platform"; list="$here/files-$platform-server.txt"; install="$here/INSTALL-server.txt";;
*) die "unknown kind: $kind (client or server)";;
esac
stage="$out/$name"

[[ $commit =~ ^[0-9a-f]{40}$ ]] || die "not a full commit ID: $commit"
[ -e "$artifact/mohreloaded$exe" ] || die "$artifact doesn't hold the game"
[ -e "$artifact/mohreloaded-server$exe" ] || die "$artifact doesn't hold the dedicated server"

rm -rf "$stage"
mkdir -p "$stage"
if [ "$kind" = client ]; then
	cp -R "$artifact/." "$stage/"
	rm -f "$stage/mohreloaded-server$exe"
	cp -R "$here/licenses" "$stage/"
	mkdir -p "$stage/licenses/omaha-sound"
	mv "$stage/OMAHA-SOUND-LICENSES.txt" "$stage/licenses/omaha-sound/"
else
	cp "$artifact/mohreloaded-server$exe" "$artifact/game$lib" "$stage/"
fi
cp "$root/COPYING.txt" "$stage/"
if [ "$platform" = linux ]; then
	sed "s/@COMMIT@/$commit/; s/@VERSION@/$version/g" "$install" > "$stage/INSTALL.txt"
else
	# Notepad's line endings
	sed "s/@COMMIT@/$commit/; s/@VERSION@/$version/g; s/\$/\r/" "$install" > "$stage/INSTALL.txt"
fi

diff <(cd "$stage" && find . -mindepth 1 | sed 's#^\./##' | LC_ALL=C sort) <(LC_ALL=C sort "$list") \
	|| die "$name doesn't hold the files in $(basename "$list") (< the build's, > the list's)"

# The artifact's zip lost the permissions: folders and files readable, the programs executable
find "$stage" -type d -exec chmod 755 {} +
find "$stage" -type f -exec chmod 644 {} +
if [ "$platform" = linux ]; then
	for f in "$stage"/mohreloaded "$stage"/mohreloaded-server "$stage"/mohlauncher "$stage"/*.so "$stage"/*.so.*; do
		# Linux programs only (ELF), not a stand-in a test leaves there
		if [ -f "$f" ] && [ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = 177ELF ]; then ${STRIP:-strip} --strip-debug "$f"; fi
	done
	for f in "$stage"/mohreloaded "$stage"/mohreloaded-server "$stage"/mohlauncher; do
		if [ -f "$f" ]; then chmod 755 "$f"; fi
	done
	archive="$out/$name.tar.gz"
	rm -f "$archive"
	tar --sort=name --owner=0 --group=0 --numeric-owner -czf "$archive" -C "$out" "$name"
else
	archive="$out/$name.zip"
	rm -f "$archive"
	(cd "$out" && zip -q -X -9 -r "$name.zip" "$name")
fi

echo "$archive"
