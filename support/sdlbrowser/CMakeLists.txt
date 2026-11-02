cmake_minimum_required(VERSION 3.15)
project(sdlbrowser CXX)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
# All third-party libraries are prebuilt as static libs into the vcpkg
# x86-windows-static triplet. Point the build at that prefix directly.
set(VCPKG_PREFIX "D:/Code/github/vcpkg/installed/x86-windows-static" CACHE PATH "vcpkg static triplet prefix")

set(LITEHTML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(DIR_WEBPAGE   "${CMAKE_CURRENT_SOURCE_DIR}/../webpage")
set(DIR_DRAWBUF   "${CMAKE_CURRENT_SOURCE_DIR}/../draw_buffer")
set(DIR_CAIROCONT "${LITEHTML_ROOT}/containers/cairo")
set(DIR_SELF      "${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# C++ standard / static CRT
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")   # /MT to match vcpkg static triplet

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(sdlbrowser
    # this project
    ${DIR_SELF}/main.cpp
    ${DIR_SELF}/sdl_html_widget.cpp
    ${DIR_SELF}/sdl_notifier.cpp
    ${DIR_SELF}/container_cairo_toy.cpp
    # shared webpage logic (reused from support/webpage)
    ${DIR_WEBPAGE}/web_page.cpp
    ${DIR_WEBPAGE}/http_request.cpp
    ${DIR_WEBPAGE}/http_requests_pool.cpp
    ${DIR_WEBPAGE}/web_history.cpp
    # off-screen cairo draw buffer (reused from support/draw_buffer)
    ${DIR_DRAWBUF}/draw_buffer.cpp
    # litehtml cairo container (non-pango parts)
    ${DIR_CAIROCONT}/container_cairo.cpp
    ${DIR_CAIROCONT}/cairo_borders.cpp
    ${DIR_CAIROCONT}/conic_gradient.cpp
)

target_include_directories(sdlbrowser PRIVATE
    ${VCPKG_PREFIX}/include
    ${VCPKG_PREFIX}/include/cairo     # code uses <cairo.h>
    ${VCPKG_PREFIX}/include/litehtml # code uses <litehtml.h>
    ${VCPKG_PREFIX}/include/SDL2     # code uses <SDL.h>
    ${DIR_SELF}
    ${DIR_WEBPAGE}
    ${DIR_DRAWBUF}
    ${DIR_CAIROCONT}
)

target_compile_definitions(sdlbrowser PRIVATE
    LITEHTML_SDLBROWSER_CAIRO_TOY=1
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _USE_MATH_DEFINES
)

# Force-include the Windows compat shim so the shared (Linux-origin) support
# sources compile unchanged on MSVC.
if(MSVC)
    target_compile_options(sdlbrowser PRIVATE /FI"${DIR_SELF}/sdl_compat.h" /utf-8)
    set_target_properties(sdlbrowser PROPERTIES LINK_FLAGS "/MAP:${CMAKE_CURRENT_BINARY_DIR}/sdlbrowser.map")
endif()

target_link_directories(sdlbrowser PRIVATE ${VCPKG_PREFIX}/lib)

# Static third-party libs
target_link_libraries(sdlbrowser PRIVATE
    litehtml.lib
    gumbo.lib
    cairo.lib
    pixman-1.lib
    freetype.lib
    fontconfig.lib
    libpng16.lib
    zs.lib
    brotlidec.lib
    brotlicommon.lib
    brotlienc.lib
    bz2.lib
    libexpatMT.lib
    libcurl.lib
    SDL2-static.lib
    pthreadVC3.lib
    # Windows system libs required by the static builds above
    user32.lib
    gdi32.lib
    shell32.lib
    advapi32.lib
    version.lib
    winmm.lib
    imm32.lib
    ole32.lib
    ws2_32.lib
    crypt32.lib
    normaliz.lib
    # curl: IPv6 if_nametoindex + SSPI + BCrypt RNG
    iphlpapi.lib
    secur32.lib
    bcrypt.lib
    # SDL2 static: HID device enumeration + gaming input
    setupapi.lib
    cfgmgr32.lib
)
