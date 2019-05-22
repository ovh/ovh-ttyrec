// vim: noai:ts=4:sw=4:expandtab:

/* Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 * Copyright 2001-2019 The ovh-ttyrec Authors. All rights reserved.
 *
 * This work is based on the original ttyrec, whose license text
 * can be found below unmodified.
 *
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* 1999-02-22 Arkadiusz Miśkiewicz <misiek@misiek.eu.org>
 * - added Native Language Support
 */

/* 2000-12-27 Satoru Takabayashi <satoru@namazu.org>
 * - modify `script' to create `ttyrec'.
 */

/* 2001-2010 Several OVH contributors who will recognize themselves
 * - lots of forgotten things
 */

/* 2010-2019 Stéphane Lesimple <stephane.lesimple@corp.ovh.com>
 * - bugfixes (SIGWINCH handling and others)
 * - BSD/MacOS compatibility
 * - SIGUSR1 handling for ttyrec log rotation
 * - input timeout locking mechanism
 * - more things (see features in the README.md file)
 */

/*
 * script
 */
#include <sys/types.h>   // open
#include <sys/stat.h>    // open
#include <fcntl.h>       // open, access
#include <termios.h>     // tcsetattr
#include <sys/ioctl.h>   // ioctl
#include <sys/time.h>    // gettimeofday
#include <sys/wait.h>    // wait3
#include <libgen.h>      // dirname
#include <stdio.h>       // printf, ...
#include <unistd.h>      // read, write, usleep, ...
#include <string.h>      // strlen, memset, ...
#include <stdlib.h>      // exit, free, getpt, ...
#include <errno.h>       // errno
#include <pthread.h>     // pthread_create
#include <signal.h>      // sigaction
#include <sys/utsname.h> // uname
#include <time.h>        // localtime

#include "configure.h"
#include "ttyrec.h"
#include "io.h"

#ifdef HAVE_openpty
# if defined(HAVE_openpty_pty_h)
#  include <pty.h>
# elif defined(HAVE_openpty_util_h)
#  include <util.h>
# elif defined(HAVE_openpty_libutil_h)
#  include <libutil.h>
# endif
#endif

#if defined(__linux__)
# define LINUX_OS
# define OS_STR    "Linux"
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__bsdi__)
# define BSD_OS
# define OS_STR    "BSD"
#elif defined(__SVR4) || defined(__svr4__) || defined(sun) || defined(__sun)
# define SUN_OS
# define OS_STR    "SUN"
#elif defined(macintosh) || defined(Macintosh) || (defined(__APPLE__) && defined(__MACH__))
# define MAC_OS
# define OS_STR    "MacOS"
#else
# define OS_STR    "UnknownOS"
#endif

#ifdef HAVE_isastream
# include <stropts.h>
#endif

#ifdef SUN_OS
# define PID_T_FORMAT    "%ld"
#else
# define PID_T_FORMAT    "%d"
#endif

#define HAVE_inet_aton
#define HAVE_scsi_h
#define HAVE_kd_h

#if !defined(CDEL)
# if defined(_POSIX_VDISABLE)
#  define CDEL    _POSIX_VDISABLE
# elif defined(CDISABLE)
#  define CDEL    CDISABLE
# else /* not _POSIX_VISIBLE && not CDISABLE */
#  define CDEL    255
# endif /* not _POSIX_VISIBLE && not CDISABLE */
#endif /* !CDEL */

#define printdbg(...)     if (opt_debug > 0) { fprintf(stderr, __VA_ARGS__); }
#define printdbg2(...)    if (opt_debug > 1) { fprintf(stderr, __VA_ARGS__); }

// functions used in the main() before the forks
void fixtty(void);
void help(void);
void set_ttyrec_file_name(char **nameptr);
void getmaster(void);

// functions used by the parent
void doinput(void);
void sigwinch_handler_parent(int signal);
void *timeout_watcher(void *arg);
void do_lock(void);
void handle_cheatcodes(char c);

// functions used by the child
void dooutput(void);
void sigwinch_handler_child(int signal);

// functions used by the subchild
void doshell(const char *, char **);
void getslave(void);

// sighandlers (parent and child)
void swing_output_file(int signal);
void unlock_session(int signal);
void lock_session(int signal);
void finish(int signal);
void sigterm_handler(int signal);

// other functions used by parent and child
void done(int status);
void fail(void);
void print_termios_info(int fd, const char *prefix);

// ansi control codes
static const char *ansi_clear         = "\033[2J";
static const char *ansi_home          = "\033[H";
static const char *ansi_hidecursor    = "\033[?25l";
static const char *ansi_showcursor    = "\033[?25h";
static const char *ansi_save          = "\033[?47h";
static const char *ansi_restore       = "\033[?47l";
static const char *ansi_savecursor    = "\0337";
static const char *ansi_restorecursor = "\0338";

static time_t last_activity = 0;
static time_t locked_since  = 0;

static const char version[] = "1.1.5.0";

static FILE *fscript;
static int  child;
static int  subchild;
static char *me = NULL;
#ifdef HAVE_openpty
static int openpty_used    = 0;
static int openpty_disable = 0;
#endif
// below: only used in notty mode
int stdout_pipe[2];         // subchild will write to it, child will read from it
int stderr_pipe[2];         // subchild will write to it, child will read from it
// below: only used in tty mode
static int master;
static int slave;

static char *fname       = NULL;
static char *dname       = NULL;
static char *uuid        = NULL;
static long timeout_lock = 0;
static long timeout_kill = 0;

static struct termios parent_stdin_termios;
static struct winsize parent_stdin_winsize;
static int            parent_stdin_isatty = 0;
#if !defined(HAVE_openpty)
static char line[] = "/dev/ptyXX";
#endif

static int  opt_want_tty        = 1; // never=0, auto=1, force=2
static int  opt_append          = 0;
static int  opt_debug           = 0;
static int  opt_count_bytes     = 0;
static int  opt_cheatcodes      = 0;
static char *opt_custom_message = NULL;

static int use_tty   = 1; // no=0, yes=1
static int can_exit  = 0;
static int childexit = 254;

int main(int argc, char **argv)
{
    char *command = NULL;
    char **params = NULL;
    char *shell   = NULL;
    int  legacy   = 0;
    int  ch;

    shell = getenv("SHELL");
    if (shell == NULL)
    {
        shell = "/bin/sh";
    }

    while ((ch = getopt(argc, argv, "cCupVhvanf:z:d:t:T:k:s:e:")) != EOF)
    {
        switch ((char)ch)
        {
        // debug ttyrec
        case 'v':
            opt_debug++;
            break;

        // open ttyrec file in append mode instead of write mode
        case 'a':
            opt_append++;
            break;

        // inhibit cheatcodes (force lock, force kill)
        case 'C':
            opt_cheatcodes = 0;
            break;

        // enable cheatcodes (force lock, force kill)
        case 'c':
            opt_cheatcodes = 1;
            break;

        // ignored (for compatibility with ttyrec classic)
        case 'u':
            break;

        // ttyrec classic way of specifying the command to launch, it uses sh -c
        case 'e':
            legacy    = 1;
            params    = malloc(sizeof(char *) * 4);
            command   = shell;
            params[0] = strrchr(shell, '/') + 1;
            params[1] = "-c";
            params[2] = strdup(optarg);
            params[3] = NULL;
            break;

        // directory to write ttyrec files to (autogenerated)
        case 'd':
            dname = strdup(optarg);
            break;

        // fullpath of ttyrec file to write to (optional, autogenerated if missing)
        case 'f':
            fname = strdup(optarg);
            break;

        // uuid, will appear in my ttyrec output file names, to keep track even after rotation (if omitted, will default to my pid)
        case 'z':
            uuid = strdup(optarg);
            break;

        // openpty_disable, don't prefer openpty() on systems that support it
        case 'p':
#ifdef HAVE_openpty
            openpty_disable++;
#else
            fprintf(stderr, "Ignored option 'p': openpty() not supported on this system.\r\n");
#endif
            break;

        // timeout before lock (t) or kill (k)
        case 't':
        case 'k':
            errno = 0;
            long timeout = strtol(optarg, NULL, 10);
            if ((errno != 0) || (timeout <= 0))
            {
                help();
                fprintf(stderr, "Invalid value passed to -%c (%s), expected a strictly positive integer\r\n", (char)ch, optarg);
                exit(EXIT_FAILURE);
            }
            printdbg("timeout %c=%ld\r\n", ch, timeout_lock);
            if ((char)ch == 't')
            {
                timeout_lock = timeout;
            }
            else if ((char)ch == 'k')
            {
                timeout_kill = timeout;
            }
            break;

        case 's':
            opt_custom_message = strdup(optarg);
            break;

        // if specified, will count number of bytes out and print it on termination (experimental)
        case 'n':
            opt_count_bytes++;
            break;

        case 'T':
            if (strncmp(optarg, "never", strlen("never")) == 0)
            {
                opt_want_tty = 0;
            }
            else if (strncmp(optarg, "auto", strlen("auto")) == 0)
            {
                opt_want_tty = 1;
            }
            else if (strncmp(optarg, "always", strlen("always")) == 0)
            {
                opt_want_tty = 2;
            }
            else
            {
                help();
                fprintf(stderr, "Invalid value passed to -T (%s), expected either 'never', 'auto' or 'always'\r\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;

        // version
        case 'V':
            printf("ttyrec v%s\r\n", version);
#ifdef DEFINES_STR
            printf("%s (%s)\r\n", DEFINES_STR, OS_STR);
#endif
#ifdef __VERSION__
            printf("compiler version %s\r\n", __VERSION__);
#endif
            exit(0);

        // 'h', and any other unknown option
        case 'h':
        default:
            help();
            exit(EXIT_FAILURE);
        }
    }
    argc -= optind;
    argv += optind;

    printdbg("remaining non-parsed options argc=%d\r\n", argc);
    for (int i = 0; i < argc; i++)
    {
        printdbg("option %d: <%s>\r\n", i, argv[i]);
    }

    if (uuid == NULL)
    {
        uuid = malloc(sizeof(char) * BUFSIZ);
        snprintf(uuid, BUFSIZ, PID_T_FORMAT, getpid());
    }

    if ((timeout_lock > 0) && (timeout_kill > 0) && (timeout_kill < timeout_lock))
    {
        help();
        fprintf(stderr, "specified timeout_lock (%ld) is higher than timeout_kill (%ld), this doesn't make sense\r\n", timeout_lock, timeout_kill);
        exit(EXIT_FAILURE);
    }

    if (legacy)
    {
        // strdup: make it free()able
        fname = (argv[0] == NULL ? strdup("ttyrecord") : strdup(argv[0]));
    }
    else
    {
        if (argv[0] == NULL)
        {
            command   = shell;
            params    = malloc(sizeof(char *) * 3);
            params[0] = strrchr(shell, '/') + 1;
            params[1] = "-i";
            params[2] = NULL;
        }
        else
        {
            command = argv[0];
            params  = argv;
        }
    }

    printdbg("will execvp %s with the following params:\r\n", command);
    if (params == NULL)
    {
        printdbg("(none)\r\n");
    }
    else
    {
        for (int index = 0; params[index] != NULL; index++)
        {
            printdbg("- '%s'\r\n", params[index]);
        }
    }

    // if neither dname nor fname are given, set dname to current cir
    if ((dname == NULL) && (fname == NULL))
    {
        dname = strdup(".");
    }

    // if no file name given, generate it (dname is used as directory)
    if (fname == NULL)
    {
        set_ttyrec_file_name(&fname);
    }

    // if dname == ".", it might be because we've set it
    if (dname == NULL)
    {
        // strdup(dirname) because in done() we free() dname
        dname = strdup(dirname(strdup(fname)));
    }

    if (dname == NULL)
    {
        fprintf(stderr, "failed to find a proper dname from fname=<%s>\r\n", fname);
        exit(EXIT_FAILURE);
    }
    printdbg("will use %s as dname\r\n", dname);

    if ((fscript = fopen(fname, opt_append ? "a" : "w")) == NULL)
    {
        perror(fname);
        exit(EXIT_FAILURE);
    }
    free(fname);
    setbuf(fscript, NULL);

    {
        struct sigaction act;
        memset(&act, '\0', sizeof(act));
        act.sa_handler = &finish;
        act.sa_flags   = SA_NOCLDSTOP | SA_RESTART;
        if (sigaction(SIGCHLD, &act, NULL))
        {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }

        memset(&act, '\0', sizeof(act));
        act.sa_handler = &swing_output_file;
        act.sa_flags   = SA_RESTART;
        if (sigaction(SIGUSR1, &act, NULL))
        {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }

        memset(&act, '\0', sizeof(act));
        act.sa_handler = &unlock_session;
        act.sa_flags   = SA_RESTART;
        if (sigaction(SIGUSR2, &act, NULL))
        {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }

        memset(&act, '\0', sizeof(act));
        act.sa_handler = &lock_session;
        act.sa_flags   = SA_RESTART;
        if (sigaction(SIGURG, &act, NULL))
        {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }

        memset(&act, '\0', sizeof(act));
        act.sa_handler = &sigterm_handler;
        act.sa_flags   = SA_RESTART;
        if (sigaction(SIGTERM, &act, NULL))
        {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
    }

    parent_stdin_isatty = isatty(0);
    switch (opt_want_tty)
    {
    case 2:
        use_tty = 1;
        break;

    case 0:
        use_tty = 0;
        break;

    default:
        use_tty = parent_stdin_isatty;
    }
    printdbg("parent: isatty(stdin) == %d, opt_want_tty == %d, use_tty == %d\r\n", parent_stdin_isatty, opt_want_tty, use_tty);

    if (use_tty)
    {
        getmaster();
        print_termios_info(master, "parent master termios");
        print_termios_info(0, "parent stdin before fixtty termios");
        if (parent_stdin_isatty)
        {
            fixtty();
        }
        print_termios_info(0, "parent stdin after fixtty termios");
    }
    else
    {
        // pipe[0] is read, pipe[1] is write
        if (pipe(stdout_pipe))
        {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
        if (pipe(stderr_pipe))
        {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    child = fork();
    if (child < 0)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    else if (child == 0)
    {
        // we are the child
        printdbg("child pid is %ld\r\n", (long int)getpid());
        print_termios_info(0, "child stdin termios");
        subchild = child = fork();
        if (child < 0)
        {
            perror("fork");
            fail();
        }
        else if (child)
        {
            // we are still the child (parent of subchild)
            me = "child";
            dooutput();
        }
        else
        {
            // we are the subchild
            printdbg("subchild pid is %ld\r\n", (long int)getpid());
            print_termios_info(0, "subchild stdin termios");
            me = "subchild";
            doshell(command, params);
        }
    }
    else
    {
        // we are the parent
        me = "parent";
        printdbg("parent pid is %ld\r\n", (long int)getpid());
        sigwinch_handler_parent(SIGWINCH);
        if (timeout_lock || timeout_kill)
        {
            pthread_t watcher_thread;
            if (pthread_create(&watcher_thread, NULL, timeout_watcher, NULL) == -1)
            {
                perror("pthread");
                fail();
            }
        }
        doinput();
    }

    return 0;
}


void handle_cheatcodes(char c)
{
    static int lockseq = 0;
    static int killseq = 0;

    if (opt_cheatcodes != 1)
    {
        return;
    }

    // LOCK
    if (c == '\x0c') // ^L
    {
        if (++lockseq >= 8)
        {
            lockseq = 0;
            do_lock();
        }
    }
    else
    {
        lockseq = 0;
    }

    // KILL
    if (((c == '\x0b') && ((killseq == 0) || (killseq == 4))) ||                                   // ^K
        ((c == '\x09') && ((killseq == 1) || (killseq == 5))) ||                                   // ^I
        ((c == '\x0c') && ((killseq == 2) || (killseq == 3) || (killseq == 6) || (killseq == 7)))) // ^L
    {
        killseq++;
    }
    else
    {
        killseq = 0;
    }
    if (killseq >= 8)
    {
        kill(child, SIGTERM);
    }
}


// called by parent
void doinput(void)
{
    int  cc;
    char ibuf[BUFSIZ];

    (void)fclose(fscript);
#ifdef HAVE_openpty
    if (openpty_used)
    {
        // openpty opens the master and the slave in a single call to getmaster(),
        // but in that case we don't want the slave (we won't call getslave())
        (void)close(slave);
    }
#endif

    struct sigaction act;
    memset(&act, '\0', sizeof(act));
    act.sa_handler = &sigwinch_handler_parent;
    act.sa_flags   = SA_RESTART;
    if (sigaction(SIGWINCH, &act, NULL))
    {
        perror("sigaction");
        fail();
    }
    last_activity = time(NULL);

    if (use_tty)
    {
        print_termios_info(master, "parent master termios in doinput");

        while ((cc = read(0, ibuf, BUFSIZ)) > 0)
        {
            printdbg2("[in:%d]", cc);
            if (!locked_since)
            {
                if (write(master, ibuf, cc) == -1)
                {
                    perror("write[parent-master]");
                }
                last_activity = time(NULL);
                if (cc == 1)
                {
                    handle_cheatcodes(ibuf[0]);
                }
            }
        }

        if (opt_debug && (cc == -1))
        {
            perror("read");
        }
    }
    else
    {
        // we won't use a pseudotty, just pipes, but as we're the parent so we don't need those
        // also our STDIN is passed thru the subchild, so we don't need to handle it ourselves, we'll just wait for our child exit
        close(0);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
    }

    printdbg("%s("PID_T_FORMAT "): end doinput, waiting for child\r\n", me, getpid());
    wait3(&childexit, 0, 0);

    printdbg("%s("PID_T_FORMAT "): end doinput, child exited with status=%d, exiting too\r\n", me, getpid(), childexit);
    done(childexit);
}


// handler of SIGCHLD
void finish(int signal)
{
    int waitpid;
    int die = 0;

    (void)signal;
    printdbg("%s("PID_T_FORMAT "): got SIGCHLD, calling wait3\r\n", me, getpid());

    while ((waitpid = wait3(&childexit, WNOHANG, 0)) > 0)
    {
        if (waitpid == subchild)
        {
            printdbg("%s("PID_T_FORMAT "): subchild exited with %d, setting can_exit to 1\r\n", me, getpid(), childexit);
            can_exit = 1;
        }
        else if (waitpid == child)
        {
            printdbg("%s("PID_T_FORMAT "): child exited with %d, exiting too\r\n", me, getpid(), childexit);
            die = 1;
        }
    }

    if (die)
    {
        done(childexit);
    }
}


void set_ttyrec_file_name(char **nameptr)
{
    struct timeval tv;
    struct tm      *t = NULL;

    if (gettimeofday(&tv, NULL))
    {
        perror("gettimeofday()");
        fail();
    }

    t = localtime((const time_t *)&tv.tv_sec);
    if (t == NULL)
    {
        perror("localtime()");
        fail();
    }

    *nameptr = malloc(sizeof(char) * BUFSIZ);
    if (*nameptr == NULL)
    {
        perror("malloc()");
        fail();
    }
    if (snprintf(*nameptr, BUFSIZ, "%s/%04u-%02u-%02u.%02u-%02u-%02u.%06lu.%s.ttyrec", dname, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, (long unsigned int)tv.tv_usec, uuid) == -1)
    {
        perror("snprintf()");
        free(*nameptr);
        fail();
    }
}


void swing_output_file(int signal)
{
    char *newname = NULL;

    (void)signal;

    if (subchild != 0)
    {
        set_ttyrec_file_name(&newname);

        fclose(fscript);

        if ((fscript = fopen(newname, "w")) == NULL)
        {
            perror("fopen()");
            free(newname);
            fail();
        }
        free(newname);
        setbuf(fscript, NULL);
    }
}


// SIGUSR2
void unlock_session(int signal)
{
    last_activity = time(NULL);
    // to avoid signal storm, abort if not locked
    if (!locked_since)
    {
        return;
    }

    printdbg("%s("PID_T_FORMAT "): unlock_session()\r\n", me, getpid());
    locked_since = 0;

    // in case only the parent or the child got the SIG,
    // ensure the other also gets it
    kill(subchild > 0 ? getppid() : child, signal);

    if (subchild == 0)
    {
        // if we're the parent, force our children to redraw after unlock
        usleep(1000 * 300);

        struct winsize tmpwin;
        (void)ioctl(master, TIOCGWINSZ, (char *)&tmpwin);

        int pixels_per_row = tmpwin.ws_ypixel / tmpwin.ws_row;
        tmpwin.ws_row++;
        tmpwin.ws_ypixel += pixels_per_row;
        (void)ioctl(master, TIOCSWINSZ, (char *)&tmpwin);
        kill(child, SIGWINCH);

        usleep(1000 * 300);

        tmpwin.ws_row--;
        tmpwin.ws_ypixel -= pixels_per_row;
        (void)ioctl(master, TIOCSWINSZ, (char *)&tmpwin);
        kill(child, SIGWINCH);
    }
    else
    {
        // child: restore console, make cursor visible again, restore its position
        (void)fputs(ansi_restore, stdout);
        (void)fputs(ansi_restorecursor, stdout);
        (void)fputs(ansi_showcursor, stdout);
    }
}


// SIGURG
void lock_session(int signal)
{
    (void)signal;
    // to avoid signal storm, abort if locked
    if (locked_since)
    {
        return;
    }

    printdbg("%s("PID_T_FORMAT "): lock_session()\r\n", me, getpid());
    locked_since = time(NULL);

    // in case only the parent or the child got the SIG,
    // ensure the other also gets it
    kill(subchild > 0 ? getppid() : child, signal);

    // if we're the parent, nothing more to do
    if (subchild == 0)
    {
        return;
    }

    const char *lock = "\033[31m" \
                       "██╗      ██████╗  ██████╗██╗  ██╗███████╗██████╗ \r\n"
                       "██║     ██╔═══██╗██╔════╝██║ ██╔╝██╔════╝██╔══██╗\r\n"
                       "██║     ██║   ██║██║     █████╔╝ █████╗  ██║  ██║\r\n"
                       "██║     ██║   ██║██║     ██╔═██╗ ██╔══╝  ██║  ██║\r\n"
                       "███████╗╚██████╔╝╚██████╗██║  ██╗███████╗██████╔╝\r\n"
                       "╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝╚══════╝╚═════╝ \033[0m\r\n"                    \
                       "     .--------.      \r\n"                                                       \
                       "    / .------. \\     \r\n"                                                      \
                       "   / /        \\ \\    \033[33mThe current session is \033[31mLOCKED\033[0m\r\n" \
                       "   | |        | |    \r\n"                                                       \
                       "  _| |________| |_   \033[33mSince %s\033[0m\r\n"                                \
                       ".' |_|        |_| '. \r\n"                                                       \
                       "'._____ ____ _____.' \033[33mDue to input inactivity timeout\033[0m\r\n"         \
                       "|     .'____'.     | \r\n"                                                       \
                       "'.__.'.'    '.'.__.' \r\n"                                                       \
                       "'.__  |      |  __.' \r\n"                                                       \
                       "|   '.'.____.'.'   | \033[33m%s,\033[0m\r\n"                                     \
                       "'.____'.____.'____.' \r\n"                                                       \
                       "'.________________.' \033[33m%s.\033[0m\r\n";

    char           hostname[BUFSIZ];
    struct utsname name;
    if (uname(&name))
    {
        hostname[0] = '\0';
    }
    else
    {
        strncpy(hostname, name.nodename, BUFSIZ);
        hostname[BUFSIZ - 1] = '\0';
    }

    const char *salute[] =
    {
        "Kind regards",       "Your dearest friend",
        "Love",               "xoxoxo",               "XoXoXo",
        "Respectfully yours", "Best",
        "Sincerely yours",    "Take care",
        "Cheers",             "With deepest sympathy",
    };
#define salute_len    (sizeof(salute) / sizeof(const char *))

    // save cursor pos, save buffer, clear screen, put cursor at home position, hide cursor
    (void)fputs(ansi_savecursor, stdout);
    (void)fputs(ansi_save, stdout);
    (void)fputs(ansi_clear, stdout);
    (void)fputs(ansi_home, stdout);
    (void)fputs(ansi_hidecursor, stdout);

    time_t    now     = time(NULL);
    struct tm *tm_now = localtime(&now);
    char      ctime_now[BUFSIZ];
    strftime(ctime_now, BUFSIZ, "%A %Y-%m-%d %H:%M:%S", tm_now);

    srand(now);
    (void)printf(lock, ctime_now, salute[rand() % salute_len], hostname);

    if (opt_custom_message != NULL)
    {
        (void)fputs("\r\n", stdout);
        (void)puts(opt_custom_message);
    }
}


void do_lock(void)
{
    locked_since = time(NULL);
    kill(child, SIGURG);
}


void *timeout_watcher(void *arg)
{
    (void)arg;
    for ( ; ; )
    {
        sleep(1);
        // handle lock: if input is idle, and we have a pseudotty, lock
        if ((timeout_lock > 0) && use_tty)
        {
            if (!locked_since && (time(NULL) - last_activity > timeout_lock))
            {
                printdbg("parent: timeout_watcher: do_lock()\r\n");
                do_lock();
            }
        }
        // handle kill
        if (timeout_kill > 0)
        {
            // if we're locked, check against the locked_since (never happens if !use_tty)
            if (locked_since && (time(NULL) - locked_since > timeout_kill - timeout_lock))
            {
                printdbg("parent: timeout_watcher: kill (locked)\r\n");
                kill(child, SIGTERM);
            }
            // handle kill cont'd: if we're not locked, check against the last_activity
            if (!locked_since && (time(NULL) - last_activity > timeout_kill))
            {
                printdbg("parent: timeout_watcher: kill (unlocked), now=%d last_activity=%d timeout_kill=%ld\r\n", (int)time(NULL), (int)last_activity, timeout_kill);
                kill(child, SIGTERM);
            }
        }
    }
}


// called by child
void dooutput(void)
{
    int                cc;
    char               obuf[BUFSIZ];
    int                waitpid;
    unsigned long long bytes_out          = 0;
    int                stdout_pipe_opened = 1;
    int                stderr_pipe_opened = 1;
    int                target_fd          = 1; // stdout by default

    setbuf(stdout, NULL);
    (void)close(0);                            // the subchild will consume it, not us
#ifdef HAVE_openpty
    if (openpty_used)
    {
        // openpty opens the master and the slave in a single call to getmaster(),
        // but in that case we don't want the slave (we won't call getslave())
        (void)close(slave);
    }
#endif
    struct sigaction act;
    memset(&act, '\0', sizeof(act));
    act.sa_handler = &sigwinch_handler_child;
    act.sa_flags   = SA_RESTART;
    if (sigaction(SIGWINCH, &act, NULL))
    {
        perror("sigaction");
        fail();
    }

    if (!use_tty)
    {
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
    }
    else
    {
        // ensure that our user's terminal is not in altscreen on launch,
        // or (sh)he might get surprised when (s)he launches then exits programs such as vim
        (void)fputs(ansi_restore, stdout);
    }

    for ( ; ; )
    {
        Header h;

        // we have a tty
        if (use_tty)
        {
            cc = read(master, obuf, BUFSIZ);
        }
        // we don't have a tty and use pipes
        else
        {
            fd_set rfds;
            int    nfds = 0;
            FD_ZERO(&rfds);
            if (stdout_pipe_opened)
            {
                FD_SET(stdout_pipe[0], &rfds);
                if (stdout_pipe[0] > nfds)
                {
                    nfds = stdout_pipe[0];
                }
            }
            if (stderr_pipe_opened)
            {
                FD_SET(stderr_pipe[0], &rfds);
                if (stderr_pipe[0] > nfds)
                {
                    nfds = stderr_pipe[0];
                }
            }
            printdbg2("[select:%d:%d]", stdout_pipe_opened, stderr_pipe_opened);
            int retval     = select(nfds + 1, &rfds, NULL, NULL, NULL);
            int current_fd = -1;

            cc = 0;
            if (retval == -1)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                else
                {
                    perror("select()");
                }
            }
            else if (retval)
            {
                if (FD_ISSET(stderr_pipe[0], &rfds))
                {
                    current_fd = stderr_pipe[0];
                    target_fd  = 2; // stderr
                }
                else if (FD_ISSET(stdout_pipe[0], &rfds))
                {
                    current_fd = stdout_pipe[0];
                    target_fd  = 1; // stdout
                }
                else
                {
                    perror("select() returned invalid fd");
                }
            }
            if (current_fd != -1)
            {
                cc = read(current_fd, obuf, BUFSIZ);
            }
            if (cc <= 0)
            {
                if (current_fd == stderr_pipe[0])
                {
                    printdbg2("[stderr:eof]");
                    stderr_pipe_opened = 0;
                    close(stderr_pipe[0]);
                }
                if (current_fd == stdout_pipe[0])
                {
                    printdbg2("[stdout:eof]");
                    stdout_pipe_opened = 0;
                    close(stdout_pipe[0]);
                }
                if ((stderr_pipe_opened == 0) && (stdout_pipe_opened == 0))
                {
                    printdbg2("[alleof]");
                    break;
                }
            }
        }

        // here, we have cc with the number of bytes read, from either the tty or the pipes

        printdbg2("[out:%d]", cc);
        if (cc == 0)
        {
            printdbg("\r\n%s(" PID_T_FORMAT "): got EOF, there's nothing left to read from\r\n", me, getpid());
            break;
        }
        if (cc < 0)
        {
            printdbg2("[out:%s]", strerror(errno));
            if (errno != EINTR)
            {
                printdbg("\r\n%s(" PID_T_FORMAT "): got fatal error when reading, there's nothing left to read from\r\n", me, getpid());
                break;
            }
        }

        if (!locked_since && (cc > 0))
        {
            h.len = cc;
            gettimeofday(&h.tv, NULL);
            if (write(target_fd, obuf, cc) == -1)
            {
                if (errno != EINTR)
                {
                    printdbg("write(child-stdout,len=%d): %s", cc, strerror(errno));
                    if (stdout_pipe_opened)
                    {
                        close(stdout_pipe[0]);
                    }
                    if (stderr_pipe_opened)
                    {
                        close(stderr_pipe[0]);
                    }
                    break;
                }
            }
            (void)write_header(fscript, &h);
            (void)fwrite(obuf, 1, cc, fscript);
            bytes_out    += cc;
            last_activity = time(NULL);
        }
    }

    printdbg("child: end dooutput, waiting can_exit (== %d)\r\n", can_exit);

    while (can_exit == 0)
    {
        waitpid = wait3(&childexit, 0, 0);
        if (waitpid < 0) // oops, all our children are already dead (ECHILD)
        {
            printdbg("child: oops, subchild is already dead!\r\n");
            can_exit = 1;
        }
    }

    printdbg("child: end dooutput, can_exit done, status %d, exiting\r\n", childexit);
    if (opt_count_bytes)
    {
        fprintf(stderr, "\r\nTTY_BYTES_OUT=%llu\r\n", bytes_out);
    }
    done(childexit);
}


// called by subchild
void doshell(const char *command, char **params)
{
    (void)fclose(fscript);
    if (use_tty)
    {
        getslave();
        print_termios_info(slave, "subchild slave termios");
        (void)close(master);
        (void)dup2(slave, 0);
        (void)dup2(slave, 1);
        (void)dup2(slave, 2);
        (void)close(slave);
    }
    else
    {
        (void)dup2(stdout_pipe[1], 1);
        (void)dup2(stderr_pipe[1], 2);
        (void)close(stdout_pipe[1]);
        (void)close(stderr_pipe[1]);
    }

    execvp(command, params);

    perror(command);
    fail();
}


void fixtty(void)
{
    struct termios rtt;

    rtt = parent_stdin_termios;
#ifdef HAVE_cfmakeraw
    cfmakeraw(&rtt);
    rtt.c_lflag &= ~ECHO;
#else
    rtt.c_iflag      = 0;
    rtt.c_lflag     &= ~(ISIG | ICANON | XCASE | ECHO | ECHOE | ECHOK | ECHONL);
    rtt.c_oflag      = OPOST;
    rtt.c_cc[VINTR]  = CDEL;
    rtt.c_cc[VQUIT]  = CDEL;
    rtt.c_cc[VERASE] = CDEL;
    rtt.c_cc[VKILL]  = CDEL;
    rtt.c_cc[VEOF]   = 1;
    rtt.c_cc[VEOL]   = 0;
#endif
    if (tcsetattr(0, TCSAFLUSH, &rtt))
    {
        perror("tcsetattr(0) in fixtty");
    }
}


void fail(void)
{
    fprintf(stderr, "\r\nttyrec: aborting!\r\n");
    (void)kill(0, SIGTERM);
    done(EXIT_FAILURE);
}


void sigterm_handler(int signal)
{
    (void)signal;
    if (subchild > 0)
    {
        // unlock_session() is also called in done(), but we need to do it first here
        // or the below message won't be visible if we happen to be locked
        unlock_session(SIGUSR2);
        (void)puts("\r\nttyrec: ending your session, sorry (kill timeout expired, you manually typed the kill key sequence, or we got a SIGTERM).\r");
        kill(subchild, SIGTERM);
    }
    done(EXIT_SUCCESS);
}


void done(int status)
{
    if (subchild)
    {
        printdbg("child: done, cleaning up and exiting with %d (child=%d subchild=%d)\r\n", WEXITSTATUS(status), child, subchild);
        // if we were locked, unlock before exiting to avoid leaving the real terminal of our user stuck in altscreen
        unlock_session(SIGUSR2);
        (void)fclose(fscript);
        (void)close(master);
    }
    else
    {
        printdbg("parent: done, cleaning up and exiting with %d (child=%d subchild=%d)\r\n", WEXITSTATUS(status), child, subchild);
        if (use_tty && parent_stdin_isatty)
        {
            if (tcsetattr(0, TCSAFLUSH, &parent_stdin_termios))
            {
                perror("parent: tcsetattr(0) in done()");
            }
        }
    }

    free(dname);
    free(uuid);
    exit(WEXITSTATUS(status));
}


void getmaster(void)
{
    if (parent_stdin_isatty)
    {
        if (tcgetattr(0, &parent_stdin_termios))
        {
            perror("tcgetattr(0, parent_stdin_termios)");
        }
        if (ioctl(0, TIOCGWINSZ, (char *)&parent_stdin_winsize))
        {
            perror("ioctl(0, TIOCGWINSZ)");
        }
    }

#ifdef HAVE_openpty
    if (openpty_disable)
    {
#endif

#ifdef HAVE_grantpt
    // with getpt, posix_openpt and ptmx, we need grantpt in getslave()
# if defined(HAVE_getpt)
    if ((master = getpt()) < 0)
    {
        perror("getpt()");
        fail();
    }
    return;
# elif defined(HAVE_posix_openpt)
    if ((master = posix_openpt(O_RDWR | O_NOCTTY)) < 0)
    {
        perror("posix_openpt()");
        fail();
    }
    return;
# else
    if (access("/dev/ptmx", F_OK) == 0)
    {
        if ((master = open("/dev/ptmx", O_RDWR | O_NOCTTY)) < 0)
        {
            perror("open(\"/dev/ptmx\", O_RDWR)");
            fail();
        }
        return;
    }
# endif
#endif /* !HAVE_grantpt */

#ifdef HAVE_openpty
}
#endif

//#if !defined(HAVE_grantpt) || ( !defined(HAVE_getpt) && !defined(HAVE_posix_openpt) )
# ifdef HAVE_openpty
    // BUG HERE: the getpt(), posix_openpt() and /dev/ptmx ways work correctly under Linux,
    // where openpty() doesn't in the case of: ssh -T user@bastion -- -t user@remote -- 'pwd' </dev/null
    // when the remote machine is a Solaris, 'pwd' gives no output.
    // For now, we just rely on openpty() as a last resort
    if (openpty(&master, &slave, NULL, &parent_stdin_termios, &parent_stdin_winsize) < 0)
    {
        perror("openpty failed");
        fail();
    }
    openpty_used = 1;

    // fixup master pty if its lflag is 0, to make some OSes happy (OmniOS at least)
    // this is mainly useful when -T always has been specified, and our stdin is not a tty,
    // in that case openpty() gives us a very raw terminal, and it prevents some OSes to output
    // anything... which would have to be expected when you know it, but in most cases
    // it's not *actually* expected by people using -T always without a tty
    struct termios mastert;
    memset(&mastert, '\0', sizeof(mastert));
    if ((tcgetattr(master, &mastert) == 0) && (mastert.c_lflag == 0))
    {
        printdbg("fixing master pty attrs\r\n");
        mastert.c_iflag = IXON + ICRNL;  // 02400
        mastert.c_oflag = OPOST + ONLCR; // 05
        mastert.c_cflag = 0277;          //B38400 + CSIZE + CREAD;
        mastert.c_lflag = ISIG + ICANON + ECHO + ECHOE + ECHOK + ECHOKE + ECHOCTL + IEXTEN;
        // apply the c_cc config of a classic pseudotty given by posix_openpt()
        mastert.c_cc[VINTR]  = 3;
        mastert.c_cc[VQUIT]  = 28;
        mastert.c_cc[VERASE] = 127;
        mastert.c_cc[VKILL]  = 21;
        mastert.c_cc[VEOF]   = 4;
        mastert.c_cc[VTIME]  = 0;
        mastert.c_cc[VMIN]   = 1;
#ifdef VSWTC /* not defined under at least OmniOS */
        mastert.c_cc[VSWTC] = 0;
#endif
        mastert.c_cc[VSTART]   = 17;
        mastert.c_cc[VSTOP]    = 19;
        mastert.c_cc[VSUSP]    = 26;
        mastert.c_cc[VEOL]     = 0;
        mastert.c_cc[VREPRINT] = 18;
        mastert.c_cc[VDISCARD] = 15;
        mastert.c_cc[VWERASE]  = 23;
        mastert.c_cc[VLNEXT]   = 22;

        print_termios_info(master, "termios master before fix");
        if (tcsetattr(master, TCSANOW, &mastert))
        {
            printdbg("tcsetattr returned %s\r\n", strerror(errno));
        }
        print_termios_info(master, "termios master after fix");
    }

    print_termios_info(master, "openpty master termios");
    print_termios_info(slave, "openpty slave termios");

    return;
# else
    // else, try the /dev/ptyXX way
    char        *pty, *bank, *cp;
    struct stat stb;

    pty = &line[strlen("/dev/ptyp")];
    unsigned int tries = 0;
    for (bank = "pqrs"; *bank; bank++)
    {
        line[strlen("/dev/pty")] = *bank;
        *pty = '0';
        if (stat(line, &stb) < 0)
        {
            break;
        }
        tries++;
        for (cp = "0123456789abcdef"; *cp; cp++)
        {
            *pty   = *cp;
            master = open(line, O_RDWR);
            if (master >= 0)
            {
                char *tp = &line[strlen("/dev/")];
                int  ok;

                /* verify slave side is usable */
                *tp = 't';
                ok  = access(line, R_OK | W_OK) == 0;
                *tp = 'p';
                if (ok)
                {
                    return;
                }
                (void)close(master);
            }
        }
    }
    if (tries == 0)
    {
        fprintf(stderr, "Found no way to allocate a pseudo-tty on your system!\r\n");
    }
    else
    {
        fprintf(stderr, "Out of pty's (tried %u pty's)\r\n", tries);
    }
    fail();
# endif
//#endif
}


void getslave(void)
{
#ifdef HAVE_openpty
    printdbg("openpty_used == %d\r\n", openpty_used);
    if (openpty_used)
    {
        if (setsid() < 0)
        {
            perror("setsid");
            fail();
        }
        if (ioctl(slave, TIOCSCTTY, 0) < 0)
        {
#ifndef SUN_OS
            perror("ioctl");
            fail();
#endif
        }
        return;
    }
#endif

#if defined(HAVE_grantpt)
    if (setsid() < 0)
    {
        perror("setsid");
        fail();
    }
    if (grantpt(master) != 0)
    {
        perror("grantpt");
        fail();
    }
    if (unlockpt(master) != 0)
    {
        perror("unlockpt");
        fail();
    }
    const char *slavename = ptsname(master);
    printdbg("ptsname(master) is %s\r\n", slavename);
    if ((slave = open(slavename, O_RDWR)) < 0)
    {
        perror("slave = open(fd, O_RDWR)");
        fail();
    }
    if (ioctl(slave, TIOCSCTTY, 0) < 0)
    {
#ifndef SUN_OS
        perror("ioctl");
        fail();
#endif
    }
# ifdef HAVE_isastream
    if (isastream(slave))
    {
        if (ioctl(slave, I_PUSH, "ptem") < 0)
        {
            perror("ioctl(fd, I_PUSH, ptem)");
            fail();
        }
        if (ioctl(slave, I_PUSH, "ldterm") < 0)
        {
            perror("ioctl(fd, I_PUSH, ldterm)");
            fail();
        }
#  ifndef _HPUX_SOURCE
        if (ioctl(slave, I_PUSH, "ttcompat") < 0)
        {
            perror("ioctl(fd, I_PUSH, ttcompat)");
            fail();
        }
#  endif
        if (parent_stdin_isatty)
        {
            if (ioctl(0, TIOCGWINSZ, (char *)&parent_stdin_winsize))
            {
                perror("ioctl(0, TIOCGWINSZ)");
            }
        }
    }
# endif /* !HAVE_isastream */
#elif !defined(HAVE_openpty) /* !HAVE_grantpt */
    line[strlen("/dev/")] = 't';
    slave = open(line, O_RDWR);
    if (slave < 0)
    {
        perror(line);
        fail();
    }
    if (parent_stdin_isatty)
    {
        if (tcsetattr(slave, TCSAFLUSH, &parent_stdin_termios))
        {
            perror("tcsetattr(slave, TCSAFLUSH, parent_stdin_termios)");
        }
        if (ioctl(slave, TIOCSWINSZ, (char *)&parent_stdin_winsize))
        {
            perror("ioctl(slave, TIOCSWINSZ, parent_stdin_winsize)");
        }
    }
#endif /* HAVE_grantpt */
}


void sigwinch_handler_parent(int signal)
{
    (void)signal;
    if (parent_stdin_isatty)
    {
        (void)ioctl(0, TIOCGWINSZ, (char *)&parent_stdin_winsize);
        (void)ioctl(master, TIOCSWINSZ, (char *)&parent_stdin_winsize);
    }

    kill(child, SIGWINCH);
}


void sigwinch_handler_child(int signal)
{
    (void)signal;
    kill(child, SIGWINCH);
}


void print_termios_info(int fd, const char *prefix)
{
    struct termios t;

    if (opt_debug)
    {
        memset(&t, '\0', sizeof(t));
        if (tcgetattr(fd, &t))
        {
            fprintf(stderr, "%35s: %s\r\n", prefix, strerror(errno));
        }
        else
        {
            fprintf(stderr, "%35s: iflag=%06lo oflag=%02lo cflag=%06lo lflag=%06lo c_cc=", prefix, (unsigned long)t.c_iflag, (unsigned long)t.c_oflag, (unsigned long)t.c_cflag, (unsigned long)t.c_lflag);
            for (cc_t i = 0; i < NCCS; i++)
            {
                fprintf(stderr, "%02x/", t.c_cc[i]);
            }
            fprintf(stderr, "\r\n");
        }
    }
}


void help(void)
{
    fprintf(stderr,                                                                                                                                            \
            "Usage: ttyrec [options] -- <command> [command options]\n"                                                                                         \
            "\n"                                                                                                                                               \
            "Usage (legacy compatibility mode): ttyrec -e <command> [options] [ttyrec file name]\n"                                                            \
            "\n"                                                                                                                                               \
            "Options:\n"                                                                                                                                       \
            "  -z UUID     specify an UUID that will appear in the ttyrec output file names and kept with SIGUSR1 rotations (default: own PID)\n"              \
            "  -d FOLDER   folder where to write the ttyrec files (taken from -f if omitted, defaulting to working directory if both -f and -d are omitted)\n" \
            "  -f TTYREC   full path of the first ttyrec file to write to (autogenerated if omitted)\n"                                                        \
            "  -a          open the ttyrec output file in append mode instead of write-clobber mode\n"                                                         \
            "  -n          count the number of bytes out and print it on termination (experimental)\n"                                                         \
            "  -t SECONDS  lock session on input timeout after SECONDS seconds\n"                                                                              \
            "  -k SECONDS  kill session on input timeout after SECONDS seconds\n"                                                                              \
            "  -C          disable cheat-codes (see below), this is the default\n"                                                                             \
            "  -c          enable cheat-codes (see below)\n"                                                                                                   \
            "  -p          don't use openpty() even when it's available\n"                                                                                     \
            "  -T never    never allocate a pseudotty, even if stdin is a tty, and use pipes to handle stdout/stderr instead\n"                                \
            "  -T auto     allocate a pseudotty if stdin is a tty, use pipes otherwise (default)\n"                                                            \
            "  -T always   always allocate a pseudotty, even if stdin is not a tty\n"                                                                          \
            "  -v          verbose (debug) mode, use twice for more verbosity\n"                                                                               \
            "  -V          show version information\n"                                                                                                         \
            "  -e CMD      enables legacy compatibility mode, and specifies the command to be run under the user's $SHELL -c\n"                                \
            "\n"                                                                                                                                               \
            "Examples:\n"                                                                                                                                      \
            "  Run some shell commands in legacy mode: ttyrec -e 'for i in a b c; do echo $i; done' outfile.ttyrec\n"                                          \
            "  Run some shell commands in normal mode: ttyrec -f /tmp/normal.ttyrec -- sh -c 'for i in a b c; do echo $i; done'\n"                             \
            "  Connect to a remote machine interactively: ttyrec -t 60 -k 300 -- ssh remoteserver\n"                                                           \
            "  Execute a local script remotely with the default remote shell: ttyrec -- ssh remoteserver < script.sh\n"                                        \
            "  Record a screen session: ttyrec screen\n"                                                                                                       \
            "\n"                                                                                                                                               \
            "Handled signals:\n"                                                                                                                               \
            "  SIGUSR1     close current ttyrec file and reopen a new one (log rotation)\n"                                                                    \
            "  SIGURG      lock session\n"                                                                                                                     \
            "  SIGUSR2     unlock session\n"                                                                                                                   \
            "\n"                                                                                                                                               \
            "Cheat-codes (magic keystrokes combinations):\n"                                                                                                   \
            "  ^L^L^L^L^L^L^L^L   lock your session (that's 8 CTRL+L's)\n"                                                                                     \
            "  ^K^I^L^L^K^I^L^L   kill your session\n"                                                                                                         \
            "\n"                                                                                                                                               \
            "Remark about session lock and session kill:\n"                                                                                                    \
            "  If we don't have a tty, we can't lock, so -t will be ignored,\n"                                                                                \
            "  whereas -k will be applied without warning, as there's no tty to output a warning to.\n"                                                        \
            "\n");
}
