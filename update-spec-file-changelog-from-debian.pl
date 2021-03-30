#! /usr/bin/perl
use strict;
use warnings;
use File::Copy;

my $ver;
my $latestver;
my @changes;
my $changelog_fd;
my $oldspec_fd;
my $newspec_fd;
my $first = 1;

open($oldspec_fd, '<', 'ovh-ttyrec.spec') or die $!;
# first, copy all the old spec contents up to %changelog
my @contents;
while (<$oldspec_fd>) {
	push @contents, $_;
	if (/^%changelog/) { last; }
}
close($oldspec_fd);

open($changelog_fd, '<', 'debian/changelog') or die $!;
while (<$changelog_fd>) {
	if (m{^ovh-ttyrec \(([^)]+)\)}) {
		$ver = $1;
		$latestver = $ver if not defined $latestver;
	}
	elsif (m{^ -- (.+)\s+(...), (..) (...) (....)}) {
		my ($author,$wday,$day,$month,$year) = ($1,$2,$3,$4,$5);
		# from: Thu, 15 Sep 2020 10:59:22 +0200
		# to: Wed Nov 04 2020
		my $date = "$wday $month $day $year";
		if (@changes) { s/^\*/-/ for @changes; }
		push @contents, "\n" if $first == 0;
		$first = 0;
		push @contents, "* $date $author  $ver\n";
		push @contents, join("\n", @changes);
		push @contents, "\n";
		undef $ver;
		@changes = ();
	}
	elsif (m{^  (\* .+)}) {
		push @changes, $1;
	}
	elsif (m{^  (.+)}) {
		push @changes, $1;
	}
}
close($changelog_fd);

s/^Version: .*/Version: $latestver/ for @contents;
open($newspec_fd, '>', 'ovh-ttyrec.spec.tmp') or die $!;
print $newspec_fd join("", @contents);
close($newspec_fd);

move("ovh-ttyrec.spec.tmp", "ovh-ttyrec.spec");
