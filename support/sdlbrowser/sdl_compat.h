// sdl_compat.h - Windows build compatibility shim for the litehtml support sources.
//
// The shared sources under support/webpage were written for Linux and use a few
// POSIX-isms (u_int32_t / u_int64_t typedefs). This header is force-included
// (/FI) for every translation unit of the sdlbrowser target so those sources
// compile on MSVC.
//
// NOTE: we deliberately do NOT macro-ize POSIX names like write/close/unlink -
// doing so breaks the MSVC standard library (e.g. std::ostream::write). The few
// actual POSIX call sites in the shared sources are guarded with #ifdef _WIN32.
#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

// POSIX fixed-width types used by http_request.h / web_page.h
#ifndef u_int32_t
#define u_int32_t uint32_t
#endif
#ifndef u_int64_t
#define u_int64_t uint64_t
#endif

// POSIX file API headers used by image_file (web_page.cpp)
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

#endif // _WIN32
