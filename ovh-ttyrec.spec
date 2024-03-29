Summary: Extended (but compatible) fork of ttyrec
Name: ovh-ttyrec
Version: 1.1.7.1
Release: 1
License: BSD
Group: Applications/System
Source: https://github.com/ovh/ovh-ttyrec/archive/master.zip

%description
Extended (but compatible) fork of ttyrec.
ttyrec is a terminal (tty) recorder,
it comes with ttyplay, which is a tty player.

Some features of ovh-ttyrec follow:
- Drop-in replacement of the classic ttyrec,
  additional features don't break compatibility
- The code is portable and OS features that
  can be used are detected at compile time
- Supports ttyrec output file rotation
  without interrupting the session
- Supports locking the session after a keyboard input
  timeout, optionally displaying a custom message
- Supports terminating the session after a keyboard input timeout
- Supports manually locking or terminating the
  session via "cheatcodes" (specific keystrokes)
- Supports a no-tty mode, relying on pipes instead
  of pseudottys, while still recording stdout/stderr
- Automatically detects whether to use pseudottys
  or pipes, also overridable from command-line
- Supports reporting the number of bytes that
  were output to the terminal on session exit

%prep

%setup -q -n ovh-ttyrec

%build
%configure
%make_build

%install
%make_install
find "$RPM_BUILD_ROOT"/usr/bin -type f -exec strip '{}' \;
find "$RPM_BUILD_ROOT"

%clean
rm -rf -- "$RPM_BUILD_ROOT"

%files
%{_mandir}/man1/ttyplay.*
%{_mandir}/man1/ttytime.*
%{_mandir}/man1/ttyrec.*
%{_bindir}/ttyplay
%{_bindir}/ttytime
%{_bindir}/ttyrec

%changelog
* Mon Sep 18 2023 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.7.1
- fix: ttyplay: playing zstd-compressed files created in append mode halted after first stream

* Fri Sep 15 2023 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.7.0
- feat: add --stealth-stdout and --stealth-stderr

* Mon Mar 29 2021 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.6.7
- fix: rare interlocking on exit
- enh: default install prefix is now /usr/local
- fix: FreeBSD compilation
- chore: autobuild for FreeBSD

* Mon Nov 09 2020 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.6.6
- chore: display machine triplet in -V

* Tue Sep 15 2020 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.6.5
- fix: race condition when running w/o pty

* Thu Mar 05 2020 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.6.4
- fix: -k was not working correctly when used without -t

* Thu Oct 10 2019 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.6.3
- fix: race condition on exit when a sighandler gets called while we're in libc's exit(), fixes #7

* Fri Aug 30 2019 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.6.2
- fix: race condition on exit where ttyrec could get stuck

* Fri Jun 14 2019 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.6.1
- enh: with -f, auto-append .zst if -Z or --zstd was specified
- fix: allow usage of -f even if -F if specified

* Tue Jun 04 2019 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.6.0
- feat: added generic fread/fwrite/fclose wrappers as a framework to support several (de)compression algorithms
- feat: add zstd support to ttyrec and ttyplay
      ttyrec: add -Z option to enable on-the-fly compression if available
      ttyrec: add --zstd to force on-the-fly zstd compression
      ttyrec: add -l option to fine-tune the zstd compression ratio (between 1 and 19, default 3)
      ttyrec: add --max-flush-time to specify a number of seconds after which we force zstd to flush
        its output buffers to ensure somewhat idle sessions still get flushed to disk regularly
      ttyplay: zstd decompression is automatically enabled if the file suffix is ".zst"
      ttyplay: add -Z option to force on-the-fly zstd decompression regardless of the file suffix
      ttytime: add a warning if timing a .zst file is attempted (not supported)
- feat: implement long-options parsing for ttyrec
- feat: add --name-format (-F) to specify a custom file name compatible with strftime()
- feat: add --warn-before-lock and --warn-before-kill options
- fix: abort if doinput() can't write to master
- chore: nicify termios debug output
- chore: get rid of help2man requirement
- chore: portability fixes, tested under Linux, FreeBSD, NetBSD, OpenBSD, DragonFlyBSD, Darwin, Haiku, OmniOS

* Thu May 09 2019 Stéphane Lesimple (deb packages) <stephane.lesimple@corp.ovh.com>   1.1.5.0
- First public release
- Add -c option to enable cheatcodes, as they're now disabled by default
