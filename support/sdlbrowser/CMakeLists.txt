cmake_minimum_required(VERSION 3.20)

project(sdlbrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
if(TARGET SDL2_image::SDL2_image)
	set(SDL2_IMAGE_TARGET SDL2_image::SDL2_image)
elseif(TARGET SDL2_image::SDL2_image-static)
	set(SDL2_IMAGE_TARGET SDL2_image::SDL2_image-static)
else()
	message(FATAL_ERROR "SDL2_image package did not export a supported CMake target")
endif()
find_package(PkgConfig REQUIRED)
set(PKG_CONFIG_ARGN --static)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
find_package(CURL CONFIG REQUIRED)
find_package(Threads REQUIRED)

set(LITEHTML_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(LITEHTML_LIBRARY "${LITEHTML_ROOT}/outX64Debug/liblitehtml.a" CACHE FILEPATH
	"Path to the already-built litehtml static library")
set(LITEHTML_GUMBO_LIBRARY "${LITEHTML_ROOT}/outX64Debug/src/gumbo/libgumbo.a" CACHE FILEPATH
	"Path to the gumbo static library used by litehtml")

if(NOT TARGET litehtml)
	if(NOT EXISTS "${LITEHTML_LIBRARY}")
		message(FATAL_ERROR "litehtml static library not found: ${LITEHTML_LIBRARY}")
	endif()
	add_library(litehtml STATIC IMPORTED GLOBAL)
	set_target_properties(litehtml PROPERTIES
		IMPORTED_LOCATION "${LITEHTML_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${LITEHTML_ROOT}/include"
	)
endif()

add_executable(sdlbrowser
	main.cpp
	sdl_browser.cpp
	../draw_buffer/draw_buffer.cpp
	../webpage/http_request.cpp
	../webpage/http_requests_pool.cpp
	../webpage/web_history.cpp
	../webpage/web_page.cpp
	${LITEHTML_ROOT}/containers/cairo/container_cairo.cpp
	${LITEHTML_ROOT}/containers/cairo/container_cairo_pango.cpp
	${LITEHTML_ROOT}/containers/cairo/cairo_borders.cpp
	${LITEHTML_ROOT}/containers/cairo/conic_gradient.cpp
)

target_include_directories(sdlbrowser PRIVATE
	${LITEHTML_ROOT}/include
	${LITEHTML_ROOT}/containers/cairo
	../webpage
	../draw_buffer
)

target_link_libraries(sdlbrowser PRIVATE
	litehtml
	SDL2::SDL2
	${SDL2_IMAGE_TARGET}
	PkgConfig::PANGOCAIRO
	PkgConfig::CAIRO
	CURL::libcurl
	Threads::Threads
)

if(EXISTS "${LITEHTML_GUMBO_LIBRARY}")
	target_link_libraries(sdlbrowser PRIVATE "${LITEHTML_GUMBO_LIBRARY}")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(sdlbrowser PRIVATE -Wall -Wextra -Wpedantic)
	# Pango is built against the vcpkg HarfBuzz; keep that ABI-compatible library
	# ahead of older system copies even when LD_LIBRARY_PATH is set.
	target_link_options(sdlbrowser PRIVATE "LINKER:--disable-new-dtags")
endif()
