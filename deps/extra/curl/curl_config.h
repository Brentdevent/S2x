#pragma once

/*
 * Based on curl's Windows configuration headers.
 * Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
 * SPDX-License-Identifier: curl
 * See deps/curl/COPYING or https://curl.se/docs/copyright.html.
 */

/*
 * Configuration for S2x's MSVC x64 Premake build.
 * curl's config-win32.h is maintained only for VS2010-2013 project files.
 * Keep these platform settings aligned with curl's CMake/win32-cache.cmake
 * and preserve the features previously selected by config-win32.h.
 * TLS and protocol options are selected in deps/premake/curl.lua.
 */
#if !defined(_MSC_VER) || !defined(_M_X64)
#error This curl configuration requires MSVC targeting x64
#endif

/* Available Windows/UCRT headers and functions. */
#define HAVE_FCNTL_H 1
#define HAVE_IO_H 1
#define HAVE_LOCALE_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_BOOL_T 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UTIME_H 1

#define HAVE_CLOSESOCKET 1
#define HAVE_FREEADDRINFO 1
#define HAVE_GETADDRINFO 1
#define HAVE_GETADDRINFO_THREADSAFE 1
#define HAVE_GETHOSTNAME 1
#define HAVE_GETPEERNAME 1
#define HAVE_GETSOCKNAME 1
#define HAVE_IOCTLSOCKET 1
#define HAVE_IOCTLSOCKET_FIONBIO 1
#define HAVE_SETLOCALE 1
#define HAVE_SIGNAL 1
#define HAVE_SOCKET 1
#define HAVE_UTIME 1

/* Winsock uses int lengths/results and a pointer-sized socket handle. */
#define HAVE_RECV 1
#define RECV_TYPE_ARG1 SOCKET
#define RECV_TYPE_ARG2 char *
#define RECV_TYPE_ARG3 int
#define RECV_TYPE_ARG4 int
#define RECV_TYPE_RETV int
#define HAVE_SEND 1
#define SEND_TYPE_ARG1 SOCKET
#define SEND_TYPE_ARG2 char *
#define SEND_TYPE_ARG3 int
#define SEND_TYPE_ARG4 int
#define SEND_TYPE_RETV int

/* Windows x64 uses the LLP64 data model. */
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_OFF_T 4
#define SIZEOF_SIZE_T 8
#define SIZEOF_CURL_OFF_T 8
#define SIZEOF_CURL_SOCKET_T 8
#define SIZEOF_TIME_T 8
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#define ssize_t __int64
#endif

#define HAVE_STRUCT_SOCKADDR_STORAGE 1
#define HAVE_STRUCT_TIMEVAL 1
#define HAVE_SOCKADDR_IN6_SIN6_SCOPE_ID 1

#define USE_RESOLV_THREADED 1
#define HAVE_LDAP_SSL 1
#define USE_WIN32_LDAP 1
#define USE_WIN32_CRYPTO 1
#define USE_UNIX_SOCKETS 1
#define CURL_OS "x86_64-pc-win32"
