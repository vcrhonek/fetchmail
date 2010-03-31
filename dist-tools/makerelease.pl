#!/usr/bin/perl -w
#
# Make a fetchmail release.
# Dumps a release notice and diffs as a MIME multipart message 
# in RELEASE_NOTES
#

use POSIX qw(strftime);
$tmp = $ENV{TMPDIR} || $ENV{TMP} || $ENV{TEMP} || "/tmp";

$project = "fetchmail";
$website = "http://developer.berlios.de/projects/$project";
$mailfrom = "<$project-devel-owner\@lists.berlios.de> (Fetchmail Development Team)";

die "Need GNU sort!" unless `sort --version | head -n1` =~ /GNU/;

# parse options
$diffs = 0;
$verbose = 0;
$null = ">/dev/null";
$errnull = "2>/dev/null";
while ($i = shift @ARGV)
{
	if ($i =~ /^(--diffs|-d)$/i)
	{
		die "$0 does not yet work with --diffs - needs to be updated for Git first!";
		$diffs = 1;
		next;
	}

	if ($i =~ /^(--verbose|-v)$/i)
	{
		$verbose = 1;
		$null = "";
		next;
	}

	die "Error: Unknown option: $i\n";
}

# extract version from source
$version =`grep 'AC_INIT' configure.ac`;
$version =~ /AC_INIT\([^,]*,\[?([0-9.rc-]+)\]?\,.*\)/;
$version = $1;
die "cannot determine version" unless defined $1;
$tag = "RELEASE_$version";
$tag =~ tr/./-/;

# extract existing tags
open(ID, "git tag | sort -t- -k1,1 -k2,2n -k3,3n |") || die "cannot run git tag: $!\naborting";
while (<ID>) {
	chomp;
	if (m{^(RELEASE_.*)$}) {
		unshift(@versions, $1);
	}
}
close ID || die "git tag   failed, aborting";

if ($versions[0] eq $tag) {
	$tag = $versions[0];
	$oldtag = $versions[1];
} else {
	$tag = '<workfile>';
	$oldtag = $versions[0];
}

$pwd = `pwd`; chomp $pwd;

$ENV{PATH} .= ":$pwd/dist-tools:$pwd/dist-tools/shipper";

print "Building $version release, tag $tag, previous tag $oldtag\n";

if (-d "autom4te.cache") {
	system("rm -rf autom4te.cache")
		and die "Failure in removing autom4te.cache";
}

if (system("autoreconf -ifs") . ($verbose ? 'v' : '')) {
	die("Failure in regenerating autoconf files\n");
}

print "### Test-building the software...\n";
if (system("mkdir -p autobuild && cd autobuild && ../configure -C --silent && make -s clean && make " . ($verbose ? '' : '-s') . " check distcheck")) {
	die("Compilation failure\n");
}

open(REPORT, ">$tmp/$project.PREAMBLE.$$");

print REPORT <<EOF;
From: $mailfrom
Subject: The $version release of $project is available

The $version release of $project is now available at the usual locations,
including <$website>.

The source archive is available at:
<$website/$project-${version}.tar.gz>

Here are the release notes:

EOF

# Extract the current notes
open(NEWS, "NEWS");
while (<NEWS>) {
	if (/^$project/) {
		print REPORT $_;
		last;
	}
}
while (<NEWS>) {
	if (/^$project/) {
		last;
	}
	print REPORT $_;
}

$oldver = $oldtag;
$oldver =~ tr/-/./;
$oldver =~ s/^RELEASE_//;

if ($diffs) {
	print REPORT "Diffs from the previous ($oldver) release follow as a MIME attachment."
} else {
	print REPORT "By popular demand, diffs from the previous release have been omitted."
}

close(NEWS);

close(REPORT);

if ($diffs) {
	if ($tag eq '<workfile>') {
		system("svn diff -r$oldtag        $errnull >$tmp/$project.DIFFS.$$");
	} else {
		system("svn diff -r$oldtag -r$tag $errnull >$tmp/$project.DIFFS.$$");
	}
	print "Diff size:";
	system("wc <$tmp/$project.DIFFS.$$");

	system "metasend -b"
	." -D '$project-$tag announcement' -m 'text/plain' -e 7bit -f $tmp/$project.PREAMBLE.$$"
	." -n -D 'diff between $oldver and $version' -m 'text/plain' -e 7bit -f $tmp/$project.DIFFS.$$"
	." -o ANNOUNCE.EMAIL";
} else {
	system("mv", "$tmp/$project.PREAMBLE.$$", "ANNOUNCE.EMAIL");
}

#unlink("$tmp/$project.PREAMBLE.$$");
unlink("$tmp/$project.DIFFS.$$");

print "Done\n";

# makerelease ends here