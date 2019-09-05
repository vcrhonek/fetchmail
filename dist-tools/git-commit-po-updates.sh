#!/bin/sh
# dist-tools/git-commit-po-updates.sh
# A helper script to commit translation updates into Git.
# Assumes translation updates are visible to git,
# and one directory above.
#
# © Copyright 2019 by Matthias Andree.
# Licensed under the GNU General Public License V2 or,
# at your choice, any later version.
#
# Supported options:
# -n:   dry-run, only print commands, but do not run them.
set -eu


cd "$(realpath $(dirname $0))/.."

# see if Perl has Carp::Always available,
# and implicitly fail (set -e) if it hasn't.
perl -MCarp::Always -e ''

dryrun_pfx=
while getopts 'n' opt ; do
  case $opt in
  n) dryrun_pfx=: ;;
  ?) printf 'Usage: %s [-n]\n' "$0" ;
     exit 2 ;
  esac
done

git diff -G ^.Project-Id-Version --name-only po/*.po \
| while read pofile ; do 
	if ! cmd="$(perl -WT - "$pofile" <<'_EOF'
use Encode::Locale;
use Encode;
use strict;
use Carp::Always ();
use warnings FATAL => 'uninitialized';
my ($ver, $dat, $trl, $lang, $lcod, $cset, $found);

while(<>)
{
	if (/^"Project-Id-Version: (.+)\\n"/)	{ $ver=$1; };
	if (/^"PO-Revision-Date: (.+)\\n"/)	{ $dat=$1; };
	if (/^"Last-Translator: (.+)\\n"/)	{ $trl=$1; };
	if (/^"Language-Team: ([^<]+?)\s+<.*>\\n"/)
						{ $lang=$1; };
	if (/^"Language: (.+)\\n"/)		{ $lcod=$1; };
	if (/^"Content-Type: text\/plain; charset=(.+)\\n"/)
						{ $cset = $1; };
	if ($ver and $dat and $trl and $lang and $lcod and $cset) {
		$found = 1;
		last; 
	}
}

$trl = Encode::decode($cset, $trl);

if ($found) {
	print Encode::encode(locale => "git commit --author '$trl' --date '$dat' -m 'Update <$lcod> $lang translation to $ver'", Encode::FB_CROAK);
} else {
	exit(1);
}
_EOF
)"
	then
		echo >&2 "Parsing $pofile failed, skipping."
		continue
	fi
	cmd="$cmd '$pofile'"
	printf '+ %s\n' "$cmd"
	$dryrun_pfx eval "$cmd"
done
