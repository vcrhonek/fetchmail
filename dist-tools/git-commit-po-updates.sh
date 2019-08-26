#!/bin/sh
set -eu

# see if Perl has Carp::Always available:
perl -MCarp::Always -e ''

git diff -G ^.Project-Id-Version --name-only po/*.po \
| while read pofile ; do 
	if ! cmd="$(perl -WT - "$pofile" <<'_EOF'
use strict;
use Carp::Always ();
use warnings FATAL => 'uninitialized';
my ($ver, $dat, $trl, $lang, $lcod, $found);

while(<>)
{
	if (/^"Project-Id-Version: (.+)\\n"/)	{ $ver=$1 };
	if (/^"PO-Revision-Date: (.+)\\n"/)	{ $dat=$1 };
	if (/^"Last-Translator: (.+)\\n"/)	{ $trl=$1 };
	if (/^"Language-Team: ([^<]+?)\s+<.*>\\n"/)
						{ $lang=$1 };
	if (/^"Language: (.+)\\n"/)		{ $lcod=$1 };
	if ($ver and $dat and $trl and $lang and $lcod) {
		$found = 1;
		last; 
	}
}

if ($found) {
	print "git commit --author '$trl' --date '$dat' -m 'Update <$lcod> $lang translation to $ver'"
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
	eval "$cmd"
done
