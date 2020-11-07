ovh-ttyrec
==========

`ttyrec` is a terminal (tty) recorder, it comes with `ttyplay`, which is a tty player.

The original ttyrec is Copyright (c) 2000 Satoru Takabayashi.

The original ttyrec is based on the `script` program, Copyright (c) 1980 Regents of the University of California.

ovh-ttyrec is based (and compatible with) the original ttyrec, and can be used as a drop-in replacement. It is licensed under the 3-clause BSD license (see LICENSE file).

Efforts have been made to ensure the code is portable. It is known to work under at least:

 - Linux (all versions and distros)
 - BSD (known to work under at least FreeBSD, NetBSD, OpenBSD, DragonFlyBSD)
 - Darwin (macOS aka OS X aka Mac OS X)
 - Haiku (community OS compatible with BeOS)
 - OpenSolaris (known to work under at least OmniOS CE)

It should work under any POSIX OS that support either `openpty()` or the `grantpt()`/`unlockpt() `mechanisms.

## features

- Drop-in replacement of the classic ttyrec, additional features don't break compatibility
- The code is portable and OS features that can be used are detected at compile time
- Supports on-the-fly (de)compression using the zstd algorithm
- Supports ttyrec output file rotation without interrupting the session
- Supports locking the session after a keyboard input timeout, optionally displaying a custom message
- Supports terminating the session after a keyboard input timeout
- Supports manually locking or terminating the session via "cheatcodes" (specific keystrokes)
- Supports a no-tty mode, relying on pipes instead of pseudottys, while still recording stdout/stderr
- Automatically detects whether to use pseudottys or pipes, also overridable from command-line
- Supports reporting the number of bytes that were output to the terminal on session exit

## compilation

To compile the binaries and build the man pages, just run:

        $ ./configure && make

You'll need `libzstd` on the build machine if you want ttyrec to be compiled with zstd support. The library will be statically linked when possible.

If you explicitly don't want libzstd, define `NO_ZSTD=1` before running configure. If you want it but dynamically linked, define `NO_STATIC_ZSTD=1`.

Installation:

        $ make install

Note that installation is not needed to test the binaries: you can just call `./ttyrec` from the build folder.

## build a .deb package

If you want to build a .deb (Debian/Ubuntu) package, just run:

        $ ./configure && make deb

## build a .rpm package

If you want to build a .rpm (RHEL/CentOS) package, just run:

        $ ./configure && make rpm

## usage

The simplest usage is just calling the binary, it'll execute the users' shell and record the session until exit:

        $ ttyrec

To replay this session:

        $ ttyplay ./ttyrecord

Run some shell commands:

        $ ttyrec -f cmds.ttyrec -- sh -c 'for i in a b c; do echo $i; done'

Connect to a remote machine interactively, lock the session after 1 minute of inactivity, and kill it after 5 minutes of inactivity:

        $ ttyrec -t 60 -k 300 -- ssh remoteserver

Execute a local script remotely with the default remote shell:

        $ cat script.sh | ttyrec -- ssh remoteserver

Record a screen session, with on-the-fly compression:

        $ ttyrec -Z screen

Usage information:

        $ ttyrec -h

## version scheme

We follow the version format `A.B.C.D`. The following rules apply:

  - A is incremented when the file format of ttyrec changes, as long as A=1, the format is compatible with the original ttyrec (and original ttyplay)
  - B is incremented for a breaking change in the way ttyrec can be called (a command-line option was removed for example), which means in that case, other programs or scripts using ttyrec should be checked for compatibility
  - C is incremented for any non-hotfix change that stays backwards compatible (a new feature that can be enabled with a new command-line option for example)
  - D is incremented for a quickfix/hotfix, or a change in the build system, docs, etc.

When a digit is incremented, all the "lower" ones go back to zero, i.e. if we are at version 4.7.1.5, and we implement a breaking change, the version number becomes 4.8.0.0.
