#!/bin/sh

# script to test-build a PWMD-enabled fetchmail.
# requires libpwmd installed into $libpwmd_dir (default below)
set -ex

: ${libpwmd_dir:=/opt/libpwmd}

cd "$(dirname "$0")"
mkdir -p _build-pwmd
cd _build-pwmd
PKG_CONFIG_PATH="${libpwmd_dir}"/lib/pkgconfig/ ../configure -C --enable-pwmd
make -j20 clean
export LD_LIBRARY_PATH="${libpwmd_dir}"/lib
make -j20 check
d="$(mktemp -d)"
trap "rm -rf $d" 0
FETCHMAILHOME="$d" ./fetchmail -V | head -n1 | grep PWMD
