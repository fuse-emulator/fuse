#!/usr/bin/env bash
# Sergio Baldovi's documented MSYS2 recipe, with an immutable source revision.
set -euo pipefail

test "${MSYSTEM:-}" = MINGW32 && test "$(gcc -dumpmachine)" = i686-w64-mingw32 || {
  echo "Use the MSYS2 MINGW32 shell/compiler for the 32-bit release" >&2
  exit 1
}
script_dir=$(cd "$(dirname "$0")" && pwd)
revision=2f4e4704b731086b11175c433e036bcbe7c9c913
repository=https://github.com/sbaldovi/audiofile.git
prefix=/opt/fuse
work=${1:?Usage: build_windows_audiofile.sh new-build-directory}
mkdir -p "$work"
work=$(cd "$work" && pwd)
test ! -e "$work/audiofile" || {
  echo "Refusing to reuse an existing audiofile build: $work/audiofile" >&2
  exit 1
}
git init "$work/audiofile"
git -C "$work/audiofile" remote add origin "$repository"
git -C "$work/audiofile" fetch --depth 1 origin "$revision"
git -C "$work/audiofile" checkout --detach FETCH_HEAD
test "$(git -C "$work/audiofile" rev-parse HEAD)" = "$revision"
cd "$work/audiofile"
git apply --check "$script_dir/audiofile-mingw-tests.patch"
git apply "$script_dir/audiofile-mingw-tests.patch"
autoreconf --install --verbose
# This legacy C++ library is built as C++11; Fuse's C standard is unaffected.
# Internal UnitTests link with -static and need private C++ symbols which the
# DLL correctly does not export. Build both; downstream uses the shared library.
./configure --prefix="$prefix" --enable-shared --enable-static --disable-flac \
  --disable-docs --disable-examples CXXFLAGS='-O2 -g -std=gnu++11'
make -j2
test -f libaudiofile/.libs/libaudiofile.a
make check
make install-strip
mkdir -p "$prefix/share/fuse/3rd-party"
cp COPYING "$prefix/share/fuse/3rd-party/audiofile-COPYING.txt"
cp COPYING.GPL "$prefix/share/fuse/3rd-party/audiofile-COPYING.GPL.txt"
printf 'audiofile %s %s\n' "$revision" "$repository" \
  > "$prefix/share/fuse/source-versions.txt"
printf 'audiofile-tests-patch %s\n' "$(sha256sum "$script_dir/audiofile-mingw-tests.patch" | cut -d' ' -f1)" \
  >> "$prefix/share/fuse/source-versions.txt"
