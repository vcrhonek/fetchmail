#! /bin/sh

# Script to upload fetchmail website from Git repository
# (C) 2008 - 2014 by Matthias Andree. GNU GPL v3.

: ${SOURCEFORGE_LOGIN=m-a}
: ${ROOTSRV:=$(cat ~/.rootsrv 2>/dev/null)}

# abort on error
set -eu

# cd to parent of script
cd "$(dirname "$0")"
cd ..

echo "==>  Running sanity checks"
# make sure we have no dangling symlinks
if LC_ALL=C file * | egrep broken\|dangling ; then
    echo "broken symlinks -> abort" >&2
    exit 1
fi

pids=

echo "==>  Uploading website (rsync) to SourceForge"
# upload
rsync \
    --chmod=ug=rwX,o=rX,Dg=s --perms \
    --copy-links --times --checksum --verbose \
    --exclude host-scripts \
    --exclude .git --exclude '*~' --exclude '#*#' \
    * .htaccess \
    "${SOURCEFORGE_LOGIN},fetchmail@web.sourceforge.net:htdocs/" &
pids="$pids $!"

echo "==>  Uploading website (rsync) to own root server"
rsync \
    --chmod=ug=rwX,o=rX,Dg=s --perms \
    --copy-links --times --checksum --verbose \
    --exclude host-scripts \
    --exclude .git --exclude '*~' --exclude '#*#' \
    * \
    "${ROOTSRV}"://usr/local/www/fetchmail.info/ &
pids="$pids $!"

echo "==>  Uploading website (rsync) to local"
rsync \
    --chmod=ug=rwX,o=rX,Dg=s --perms \
    --copy-links --times --checksum --verbose \
    --exclude host-scripts \
    --exclude .git --exclude '*~' --exclude '#*#' \
    * \
    $HOME/public_html/fetchmail/info/ &
pids="$pids $!"

wait $pids

echo "==>  Synchronizing web dir."
synchome.sh

echo "==>  Done; check rsync output above for success."
