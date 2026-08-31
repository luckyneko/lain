# Provides FFmpeg::avutil / avcodec / avformat / swscale / swresample behind the
# lain::io::video seam, from a pinned LGPL-configured prebuilt archive.
#
# FETCHED, NOT FOUND (ADR-0019, amended). luckyneko/ffmpeg-prebuilt publishes tier-verified
# LGPL *shared* archives, and a release archive fits FetchContent perfectly — the original
# objection was to building autotools, not to fetching. Set LAIN_FFMPEG_ROOT to point at a
# system install instead; that path is subject to the same gate below, since nothing is
# trusted for being local.
#
# THE LICENCE LIVES IN THE CONFIGURATION, NOT THE API CALLED. An FFmpeg built --enable-gpl
# relicenses whatever links it whether or not a GPL codec is ever invoked, so this file FAILS
# CONFIGURE on one. The check reads the archive's MANIFEST.txt rather than running a probe:
# text needs no execution, so it still works when cross-compiling, which is exactly when a
# consumer can least inspect what it linked. The runtime second opinion is the [video] test in
# plugins/io/video/ffmpeg/test, which asks the linked libavutil directly.
#
# SHARED, deliberately: LGPL's relinking requirement is satisfied by dynamic linking alone,
# while a static build would additionally owe consumers relinkable object files. This is
# lain's only shared-linked dependency (ADR-0004, amended).
#
# Included by the ffmpeg video codec plugin, so nothing is fetched unless that plugin is
# enabled; guarded so a second include is a no-op.

if(TARGET FFmpeg::avutil)
	return()
endif()

set(LAIN_FFMPEG_VERSION "8.1.2")
set(LAIN_FFMPEG_TIER "lgpl")
set(LAIN_FFMPEG_RELEASE "ffmpeg-${LAIN_FFMPEG_VERSION}-${LAIN_FFMPEG_TIER}")

# ---- Which artifact ---------------------------------------------------------
# One archive per platform, each pinned by SHA-256 from the release's SHA256SUMS. A pinned
# hash is what makes "the build is a function of this repository" true for a binary dependency
# — without it, a re-cut release would silently change what lain links.
if(APPLE)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
		set(_ffmpeg_target "macos-arm64")
		set(_ffmpeg_sha256 "0721bc6582ffa3337ecedd272dab436eed81628c34ddf32a82d91cc99b61844c")
	else()
		# Not an oversight: GitHub retired the macos-13 image, Apple has discontinued x86_64,
		# and macOS Intel runners disappear entirely in August 2027, so no such artifact is
		# published. Failing here beats requesting a url that 404s.
		message(FATAL_ERROR
			"LAIN_IO_VIDEO_FFMPEG: no prebuilt FFmpeg for macOS x86_64 (arm64 only). "
			"Point LAIN_FFMPEG_ROOT at an LGPL-configured FFmpeg, or disable the plugin.")
	endif()
elseif(WIN32)
	set(_ffmpeg_target "windows-x86_64")
	set(_ffmpeg_sha256 "e2b3186e7be3a7c1ca9580cd33fab431afe4f48a6c7cdfe672e549fb59d71a06")
elseif(UNIX)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
		set(_ffmpeg_target "linux-arm64")
		set(_ffmpeg_sha256 "d604ddcfb5b27203a4853f19921b720616c1852a495a5b952c6b725b03adbb34")
	else()
		set(_ffmpeg_target "linux-x86_64")
		set(_ffmpeg_sha256 "93fb12550aeda463523c9b3e9d370dc01d953e3830d45d8684e5fd8deb68a083")
	endif()
else()
	message(FATAL_ERROR "LAIN_IO_VIDEO_FFMPEG: no prebuilt FFmpeg for this platform")
endif()

if(WIN32)
	set(_ffmpeg_archive "${LAIN_FFMPEG_RELEASE}-shared-${_ffmpeg_target}.zip")
else()
	set(_ffmpeg_archive "${LAIN_FFMPEG_RELEASE}-shared-${_ffmpeg_target}.tar.xz")
endif()

# ---- Obtain it --------------------------------------------------------------
if(LAIN_FFMPEG_ROOT)
	set(_ffmpeg_root "${LAIN_FFMPEG_ROOT}")
	message(STATUS "FFmpeg: using LAIN_FFMPEG_ROOT=${_ffmpeg_root}")
else()
	include(FetchContent)
	set(_ffmpeg_url "https://github.com/luckyneko/ffmpeg-prebuilt/releases/download/${LAIN_FFMPEG_RELEASE}/${_ffmpeg_archive}")
	FetchContent_Declare(ffmpeg_prebuilt
		URL          "${_ffmpeg_url}"
		URL_HASH     "SHA256=${_ffmpeg_sha256}"
		DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/ffmpeg-prebuilt/${LAIN_FFMPEG_RELEASE}"
	)
	# No CMakeLists.txt in the archive, so this populates and does not add_subdirectory.
	FetchContent_MakeAvailable(ffmpeg_prebuilt)
	set(_ffmpeg_root "${ffmpeg_prebuilt_SOURCE_DIR}")
endif()

if(NOT EXISTS "${_ffmpeg_root}/include/libavcodec/avcodec.h")
	message(FATAL_ERROR "FFmpeg: no include/libavcodec/avcodec.h under ${_ffmpeg_root}")
endif()

# ---- The licence gate -------------------------------------------------------
# A prebuilt archive states its tier, full configure string and corresponding source as text,
# so this establishes the licence without executing anything. Both halves are checked: the
# declared tier AND the configure string it publishes, so two things must be wrong at once.
set(_ffmpeg_manifest "${_ffmpeg_root}/MANIFEST.txt")
if(EXISTS "${_ffmpeg_manifest}")
	file(READ "${_ffmpeg_manifest}" _ffmpeg_manifest_text)

	if(_ffmpeg_manifest_text MATCHES "--enable-gpl")
		message(FATAL_ERROR
			"FFmpeg at ${_ffmpeg_root} is configured --enable-gpl. Linking it would relicense "
			"the whole distribution under GPL whether or not a GPL codec is ever called "
			"(ADR-0019). Use an LGPL-configured build.")
	endif()
	if(_ffmpeg_manifest_text MATCHES "--enable-nonfree")
		message(FATAL_ERROR
			"FFmpeg at ${_ffmpeg_root} is configured --enable-nonfree, which makes the result "
			"non-redistributable (ADR-0019). Use an LGPL-configured build.")
	endif()
	if(NOT _ffmpeg_manifest_text MATCHES "licence tier[ \t]+lgpl")
		message(FATAL_ERROR
			"FFmpeg at ${_ffmpeg_root} does not declare the 'lgpl' licence tier in its "
			"MANIFEST.txt (ADR-0019).")
	endif()

	string(REGEX MATCH "full configure string[^\n]*\n[ \t]*([^\n]*)" _ffmpeg_m "${_ffmpeg_manifest_text}")
	set(LAIN_FFMPEG_CONFIGURATION "${CMAKE_MATCH_1}")
	message(STATUS "FFmpeg ${LAIN_FFMPEG_VERSION} (${LAIN_FFMPEG_TIER}, ${_ffmpeg_target}): licence tier verified")
else()
	# A system root has no manifest, and establishing its tier would mean building and running
	# a probe — impossible when cross-compiling. Rather than pretend, say so plainly: the
	# [video] runtime test is then the only gate, and it still fails the build's test suite.
	set(LAIN_FFMPEG_CONFIGURATION "")
	message(WARNING
		"FFmpeg at ${_ffmpeg_root} has no MANIFEST.txt, so its licence tier cannot be "
		"established without executing it. The [video] runtime test remains the gate — run "
		"ctest before distributing anything built against it (ADR-0019).")
endif()

# ---- Imported targets -------------------------------------------------------
set(LAIN_FFMPEG_ROOT_DIR "${_ffmpeg_root}" CACHE INTERNAL "resolved FFmpeg prefix")
set(LAIN_FFMPEG_LIB_DIR "${_ffmpeg_root}/lib" CACHE INTERNAL "resolved FFmpeg library dir")

# Headers are re-exposed SYSTEM so lain's strict flags never fire on FFmpeg's own headers,
# exactly as the other addXXX modules do.
function(_lain_ffmpeg_add_library name)
	add_library(FFmpeg::${name} SHARED IMPORTED GLOBAL)
	set_target_properties(FFmpeg::${name} PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${_ffmpeg_root}/include")
	set_property(TARGET FFmpeg::${name} APPEND PROPERTY
		INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_ffmpeg_root}/include")

	if(WIN32)
		# Windows ships the DLLs in bin/ with both MSVC (.lib, beside them) and MinGW
		# (.dll.a, in lib/) import libraries; pick by toolchain rather than guessing.
		file(GLOB _dll "${_ffmpeg_root}/bin/${name}-*.dll")
		if(MSVC)
			set(_implib "${_ffmpeg_root}/bin/${name}.lib")
		else()
			set(_implib "${_ffmpeg_root}/lib/lib${name}.dll.a")
		endif()
		set_target_properties(FFmpeg::${name} PROPERTIES
			IMPORTED_LOCATION "${_dll}"
			IMPORTED_IMPLIB "${_implib}")
	elseif(APPLE)
		set_target_properties(FFmpeg::${name} PROPERTIES
			IMPORTED_LOCATION "${_ffmpeg_root}/lib/lib${name}.dylib")
	else()
		set_target_properties(FFmpeg::${name} PROPERTIES
			IMPORTED_LOCATION "${_ffmpeg_root}/lib/lib${name}.so")
	endif()
endfunction()

foreach(_lib avutil avcodec avformat swscale swresample)
	_lain_ffmpeg_add_library(${_lib})
endforeach()

# CMake derives a consumer's build-tree RPATH from the directories of the shared libraries it
# links, so no manual rpath is needed on macOS/Linux — and the [video] test proves it, since a
# test executable that cannot resolve @rpath/libavutil simply fails to launch.

# ---- Runtime staging (Windows only) -----------------------------------------
# Windows resolves DLLs by name with no rpath equivalent, so every DLL in the archive — the av*
# set AND the MinGW runtime closure shipped beside it — must sit next to the executable.
file(GLOB LAIN_FFMPEG_RUNTIME_DLLS "${_ffmpeg_root}/bin/*.dll")
set(LAIN_FFMPEG_RUNTIME_DLLS "${LAIN_FFMPEG_RUNTIME_DLLS}" CACHE INTERNAL "FFmpeg DLLs to stage")

# Copy the FFmpeg runtime beside `target`'s executable. A no-op off Windows, so a caller writes
# it once unconditionally rather than branching at every call site.
function(lain_ffmpeg_stage_runtime target)
	if(WIN32 AND LAIN_FFMPEG_RUNTIME_DLLS)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				${LAIN_FFMPEG_RUNTIME_DLLS} "$<TARGET_FILE_DIR:${target}>"
			COMMENT "Staging the FFmpeg runtime beside ${target}")
	endif()
endfunction()

# ---- Distribution obligations -----------------------------------------------
# LGPL asks four things of a redistributor: preserve the notices, identify how to obtain the
# corresponding source, permit relinking (dynamic linking satisfies it), and say prominently
# that the work uses FFmpeg. The first two are files that must travel with the binaries, so
# they are staged into the build tree beside them NOW rather than deferred to a packaging story
# lain does not have yet.
set(_ffmpeg_notice_dir "${CMAKE_BINARY_DIR}/third-party/ffmpeg")
file(MAKE_DIRECTORY "${_ffmpeg_notice_dir}")
foreach(_f COPYING.LGPLv2.1 COPYING.LGPLv3 LICENSE.md CREDITS MANIFEST.txt)
	if(EXISTS "${_ffmpeg_root}/${_f}")
		configure_file("${_ffmpeg_root}/${_f}" "${_ffmpeg_notice_dir}/${_f}" COPYONLY)
	endif()
endforeach()

# The third obligation — the prominent notice — is generated FROM the manifest rather than
# transcribed into a checked-in file. A transcription is a second copy of facts that already
# exist, and the failure mode is silent: it keeps claiming the old version after a bump. This
# cannot, because it is derived. lain::app concatenates every registered notice into the text
# its --licenses flag prints.
set(_ffmpeg_notice "${CMAKE_BINARY_DIR}/notices/ffmpeg.txt")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/notices")
file(WRITE "${_ffmpeg_notice}"
"FFmpeg ${LAIN_FFMPEG_VERSION} (${_ffmpeg_target}) — LGPL-2.1-or-later\n"
"  This software uses libraries from the FFmpeg project under the LGPL v2.1.\n"
"  FFmpeg is used in an LGPL configuration and linked dynamically, which permits\n"
"  relinking against a modified FFmpeg. It is unmodified; no patches are applied.\n"
"  Corresponding source: https://ffmpeg.org/releases/ffmpeg-${LAIN_FFMPEG_VERSION}.tar.xz\n"
"  Build recipe: https://github.com/luckyneko/ffmpeg-prebuilt\n"
"  Full licence texts and build manifest: third-party/ffmpeg/\n"
"  Configuration: ${LAIN_FFMPEG_CONFIGURATION}\n")
set_property(GLOBAL APPEND PROPERTY LAIN_THIRD_PARTY_NOTICES "${_ffmpeg_notice}")
