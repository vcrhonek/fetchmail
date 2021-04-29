#!/bin/sh

# script to test-build a PWMD-enabled fetchmail.
# requires libpwmd installed into $libpwmd_dir (default below)
set -ex

: ${libpwmd_dir:=/opt/libpwmd}

cd "$(dirname "$0")"
mkdir -p _build-pwmd
cd _build-pwmd
PKG_CONFIG_PATH="${libpwmd_dir}"/lib/pkgconfig/ ../configure -C --enable-libpwmd
make -j20 clean
export LD_LIBRARY_PATH="${libpwmd_dir}"/lib
make -j20 check
d="$(mktemp -d)"
trap "rm -rf $d" 0
FETCHMAILHOME="$d" ./fetchmail -V 2>&1 | head -n1 | grep \\+LIBPWMD  || { echo '*** ERROR libpwmd support did not make it into fetchmail ***' ; exit 1 ; }
