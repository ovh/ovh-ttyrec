#! /usr/bin/perl
use strict;
use warnings;
use File::Copy;

my $ver;
my @changes;
my $changelog_fd;
my $oldspec_fd;
my $newspec_fd;
my $first = 1;
open($changelog_fd, '<', 'debian/changelog') or die $!;
open($oldspec_fd, '<', 'ovh-ttyrec.spec') or die $!;
open($newspec_fd, '>', 'ovh-ttyrec.spec.tmp') or die $!;

# first, copy all the old spec contents up to %changelog
while (<$oldspec_fd>) {
	print $newspec_fd $_;
	if (/^%changelog/) { last; }
}
close($oldspec_fd);

while (<$changelog_fd>) {
	if (m{^ovh-ttyrec \(([^)]+)\)}) {
		$ver = $1;
	}
	elsif (m{^ -- (.+)\s+(...), (..) (...) (....)}) {
		my ($author,$wday,$day,$month,$year) = ($1,$2,$3,$4,$5);
		# from: Thu, 15 Sep 2020 10:59:22 +0200
		# to: Wed Nov 04 2020
		my $date = "$wday $month $day $year";
		if (@changes) { s/^\*/-/ for @changes; }
		print $newspec_fd "\n" if $first == 0;
		$first = 0;
		print $newspec_fd "* $date $author  $ver\n";
		print $newspec_fd join("\n", @changes);
		print $newspec_fd "\n";
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
close($newspec_fd);
move("ovh-ttyrec.spec.tmp", "ovh-ttyrec.spec");
