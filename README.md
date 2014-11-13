rpc-mt
======

Multi-threaded RPC for Linux

RPC-MT is a multi-threaded implementation of Sun ONC RPC for Linux.  It is a
replacement for the single-threaded implementation that comes with GNU glibc.

The Problem
===========
RPC-MT was designed to solve a real-world problem.  The GNU glibc
implementation of RPC is single-threaded.  It services one request at a time.

There are companies that have existing software that uses RPC and runs under
Solaris.  Some want to migrate from Solaris to Linux.  Others want to support
both Solaris and Linux.

Much of the legacy Sun RPC-based software has come to depend on the fact
that Sun's implementation of RPC can handle multiple requests simultaneously.
This can be a problem, depending on whether all requests are very short
duration, or if there are some requests that can take a while.  It also
depends on how much of a delay can be tolerated.

Implementation
==============
RPC-MT code is derived from the Glibc code.  The data structures and functions
were modified to make the service thread safe.  For example, things that
Glibc code allocates statically or automatically, in a thread-unsafe manner,
are allocated in an extension to the data structure used to control each
connection (SVCXPRT), instead.

RPC-MT is a drop-in "replacement" for the RPC in Glibc, in the sense that
its librpc.so is linked ahead of Glibc, and its definitions of RPC symbols,
such as `svc_tcp()`, occult the names in Glibc.  But, much of the lower-level
code in Glibc, such as XDR, is thread safe and is still used.

The files of interest that have been replaced are:

`    svc.c`
`    svc_run.c`
`    svc_tcp.c`
`    svc_udp.c`

In practice, this has been easier to manage.  RPC-MT can be built and deployed
in minutes, because there is no need to download, modify and rebuild Glibc.
RPC-MT has been run on several Linux distributions, including Ubuntu, CentOS,
OpenSUSE and Fedora.

Portability
===========
Since RPC-MT is Linux-specific and is derived from Glibc code, it is not
portable to other Unix or Unix-like systems.  Liberal use is made of GCC
extensions and of Linux-specific system calls.

Cleanliness
===========
RPC-MT runs Valgrind-clean.  It is clean with respect to
`gcc -Wall -Wextra`.  I have not used any brand of lint on it,
also partly because of GCC extensions.

When it is appropriate, I run both GCC and Clang to compile code, but
because RPC-MT code uses GCC extensions, it does not seem to be worth while,
in this case.

Coding style
============
The coding style used for RPC-MT can most briefly be described
as Sun cstyle, but with certain changes.  They are: tabs instead of spaces;
4 character indent, instead of 8.  No cuddling of braces.

History
=======
RPC-MT was written in 2011, by Guy Shaw, under contract
with Themis Computer, http://www.themis.com.

License
=======
Since RPC-MT code is derived from the Glibc RPC code, written by Oracle,
RPC-MT inherits the license from that code.  That is probably the kind of
license I would want to distribute my code under, anyway.  The preamble
comment section at the top of each source code file is the Oracle license
comment, intact, followed by my additional comments.
