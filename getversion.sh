#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec grep ^AC_INIT configure.ac | cut -f3 -d'[' | cut -f1 -d']'
