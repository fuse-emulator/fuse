Fuse for Windows
================

This is a binary distribution of Fuse (the Free Unix Spectrum Emulator) for
Windows. For an introduction to Fuse, see README.txt. For more detailed
documentation, read fuse.html.

Requirements
------------

A supported 32-bit or 64-bit Windows system for the bundled 32-bit build.

Libraries
---------

These native 32-bit binaries are built with MSYS2's MINGW32 toolchain,
packaged dependencies, and source-built patched audiofile/libspectrum/Fuse.
Neither MSYS2 nor Cygwin needs to be installed to run them. Do not
replace bundled DLLs with those from another toolchain or MSYS2 environment.
Depending on the selected UI and enabled features, the package may include
DLLs from these components:

    audiofile
    bzip2
    libgcrypt
    libgpg-error
    libpng
    libspectrum
    libstdc++
    libxml2
    libiconv
    gettext runtime / libintl
    winpthreads
    xz / liblzma
    zlib
    SDL2

See LICENSES.txt for copyright and license details.

For build and packaging instructions, see INSTALL in the Fuse source tree.
MSYS2 installation and environment documentation: https://www.msys2.org/
The included msys2-packages.txt records the build's installed package versions;
source-versions.txt records the source-built components' revisions.
