# Provides OpenCV's `calib` modules — opencv_core, opencv_imgproc, opencv_flann,
# opencv_features2d, opencv_calib3d, opencv_objdetect — as imported SHARED targets, from a pinned,
# verified prebuilt archive (ADR-0026).
#
# FETCHED, NOT BUILT. luckyneko/opencv-prebuilt publishes shared OpenCV with a fixed module profile
# and no optional third party, each archive checked against its name before it is published. The
# archive is a standard OpenCV install tree, so its own OpenCVConfig.cmake supplies the targets —
# including, on Windows, the per-configuration choice between the Release and Debug DLL sets the
# archive carries, and the remap of RelWithDebInfo/MinSizeRel onto Release (OpenCV's config does
# that itself under MSVC, because a non-Debug consumer cannot use the Debug DLLs). Set
# LAIN_OPENCV_ROOT to use another install instead; the gate below says what that costs.
#
# SHARED, deliberately, and not for licensing (ADR-0004, amended): OpenCV compiles its own zlib
# into opencv_core, and lain statically links a different zlib into every binary that has the png
# or tiff codec. Shared, with OpenCV's hidden visibility, the two cannot meet — the prebuilt proves
# none of its zlib is exported.
#
# Included by the OpenCV camera plugin, so nothing is fetched unless that plugin is enabled;
# guarded so a second include is a no-op.
#
# Sets LAIN_OPENCV_FOUND. FALSE means this platform has no archive, with the reason in
# LAIN_OPENCV_UNAVAILABLE: nothing is fetched, and the plugin is not built. A BROKEN source — a
# failed download, a hash or manifest mismatch — still fails configure, since quietly building
# without camera support there would hide a fault rather than report a fact about the platform.

if(TARGET opencv_core)
	set(LAIN_OPENCV_FOUND TRUE)
	return()
endif()
set(LAIN_OPENCV_FOUND FALSE)
set(LAIN_OPENCV_UNAVAILABLE "")

set(LAIN_OPENCV_VERSION "4.14.0")
set(LAIN_OPENCV_PROFILE "calib")
set(LAIN_OPENCV_MODULES core imgproc flann features2d calib3d objdetect)
set(LAIN_OPENCV_RELEASE "opencv-${LAIN_OPENCV_VERSION}-${LAIN_OPENCV_PROFILE}")

# ---- Which artifact ---------------------------------------------------------
# One archive per platform, each pinned by SHA-256 from the release's SHA256SUMS (each also matches
# GitHub's own digest for the asset). The pin is what makes "the build is a function of this
# repository" true for a binary dependency: a re-cut release would otherwise change what lain links.
if(APPLE)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
		set(_opencv_target "macos-arm64")
		set(_opencv_sha256 "b86f52bb474ff6372b36d9bb60298c09375af1e3c6b8a3ad00f938883bd486c7")
	else()
		# The same gap, for the same reason, as addFFmpeg.cmake: no macos-x86_64 artifact is published.
		set(LAIN_OPENCV_UNAVAILABLE "no prebuilt OpenCV for macOS x86_64 (arm64 only)")
	endif()
elseif(WIN32)
	# The Windows archive is built with MSVC, and OpenCV's API is C++: std::vector and std::string
	# cross the DLL boundary, so neither a MinGW toolchain nor an ARM64 target can use it.
	if(NOT MSVC)
		set(LAIN_OPENCV_UNAVAILABLE
			"the prebuilt OpenCV for Windows is MSVC-built, and its C++ API cannot be consumed by ${CMAKE_CXX_COMPILER_ID}")
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
		set(LAIN_OPENCV_UNAVAILABLE "no prebuilt OpenCV for Windows ARM64 (x86_64 only)")
	else()
		set(_opencv_target "windows-x86_64")
		set(_opencv_sha256 "58b937c10040b3443f1f7d35133b1309dc6818cb6e9d29dbccb1f5d1402cc325")
	endif()
elseif(UNIX)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
		set(_opencv_target "linux-arm64")
		set(_opencv_sha256 "478919da64908f5005aa62b2cbcbb56d53c3c5728fda797ee2d83aaf69bd1c4d")
	else()
		set(_opencv_target "linux-x86_64")
		set(_opencv_sha256 "2bce5f2d7b7d329f3ee80563ecfdc309c51c26db28c03f04bd9dc00d0df6c901")
	endif()
else()
	set(LAIN_OPENCV_UNAVAILABLE "no prebuilt OpenCV for this platform")
endif()

if(LAIN_OPENCV_UNAVAILABLE)
	return()
endif()

if(WIN32)
	set(_opencv_archive "${LAIN_OPENCV_RELEASE}-shared-${_opencv_target}.zip")
else()
	set(_opencv_archive "${LAIN_OPENCV_RELEASE}-shared-${_opencv_target}.tar.xz")
endif()

# ---- Obtain it --------------------------------------------------------------
if(LAIN_OPENCV_ROOT)
	set(_opencv_root "${LAIN_OPENCV_ROOT}")
	message(STATUS "OpenCV: using LAIN_OPENCV_ROOT=${_opencv_root}")
else()
	include(FetchContent)
	set(_opencv_url "https://github.com/luckyneko/opencv-prebuilt/releases/download/${LAIN_OPENCV_RELEASE}/${_opencv_archive}")
	FetchContent_Declare(opencv_prebuilt
		URL          "${_opencv_url}"
		URL_HASH     "SHA256=${_opencv_sha256}"
		DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/opencv-prebuilt/${LAIN_OPENCV_RELEASE}"
	)
	# No CMakeLists.txt in the archive, so this populates and does not add_subdirectory.
	FetchContent_MakeAvailable(opencv_prebuilt)
	set(_opencv_root "${opencv_prebuilt_SOURCE_DIR}")
endif()

# ---- The manifest gate ------------------------------------------------------
# The archive states what it is as text: the profile, the exact module set, the linkage, the target
# and the third-party inventory, each of which the prebuilt's check.sh established from the built
# libraries before publishing. Reading it executes nothing, so the gate still works when
# cross-compiling — the case where a consumer can least inspect what it linked. A mismatch is a
# FATAL_ERROR: an archive that is not the approved configuration is not a dependency lain has
# reviewed (ADR-0015), however it arrived.
set(_opencv_manifest "${_opencv_root}/MANIFEST.txt")
if(EXISTS "${_opencv_manifest}")
	file(READ "${_opencv_manifest}" _opencv_manifest_text)

	# The value of a `  key   value` line. The first key follows the "opencv-prebuilt" heading, so
	# every key line is preceded by a newline and two spaces.
	function(_lain_opencv_manifest_field key outVar)
		if(_opencv_manifest_text MATCHES "\n  ${key}[ \t]+([^\n]*)")
			string(STRIP "${CMAKE_MATCH_1}" _value)
			set(${outVar} "${_value}" PARENT_SCOPE)
		else()
			set(${outVar} "" PARENT_SCOPE)
		endif()
	endfunction()

	string(REPLACE ";" " " _opencv_modules_line "${LAIN_OPENCV_MODULES}")
	foreach(_pair
			"opencv version=${LAIN_OPENCV_VERSION}"
			"profile=${LAIN_OPENCV_PROFILE}"
			"modules=${_opencv_modules_line}"
			"linkage=shared"
			"target=${_opencv_target}")
		string(FIND "${_pair}" "=" _eq)
		string(SUBSTRING "${_pair}" 0 ${_eq} _key)
		math(EXPR _eq "${_eq} + 1")
		string(SUBSTRING "${_pair}" ${_eq} -1 _want)
		_lain_opencv_manifest_field("${_key}" _got)
		if(NOT _got STREQUAL _want)
			message(FATAL_ERROR
				"OpenCV at ${_opencv_root}: MANIFEST.txt says ${_key} '${_got}', but lain pins "
				"'${_want}' (ADR-0026). Use the pinned opencv-prebuilt archive.")
		endif()
	endforeach()

	# Exactly the bundled zlib, hidden inside opencv_core — the premise ADR-0004's amendment rests
	# on. Any other third-party library is a dependency nobody here has reviewed.
	_lain_opencv_manifest_field("third party" _opencv_thirdparty)
	if(NOT _opencv_thirdparty MATCHES "^zlib [0-9.]+ \\(bundled, statically inside opencv_core, symbols hidden\\)$")
		message(FATAL_ERROR
			"OpenCV at ${_opencv_root}: MANIFEST.txt lists third party '${_opencv_thirdparty}'; "
			"lain accepts only the bundled, hidden zlib (ADR-0026).")
	endif()

	if(_opencv_manifest_text MATCHES "\n  commit ([0-9a-f]+)")
		set(LAIN_OPENCV_COMMIT "${CMAKE_MATCH_1}")
	endif()
	message(STATUS "OpenCV ${LAIN_OPENCV_VERSION} (${LAIN_OPENCV_PROFILE}, ${_opencv_target}): manifest verified")
else()
	# A system install has no manifest. Rather than pretend, say so: the plugin's [opencv] runtime
	# test then becomes the only gate, and it asks the linked library the same questions.
	set(LAIN_OPENCV_COMMIT "")
	message(WARNING
		"OpenCV at ${_opencv_root} has no MANIFEST.txt, so its configuration cannot be established "
		"without executing it. The [opencv] runtime test remains the gate — run ctest before "
		"distributing anything built against it (ADR-0026).")
endif()

# ---- Imported targets -------------------------------------------------------
# OpenCV's own config, from the archive and nowhere else. EXACT because the manifest gate above
# (and the runtime test) describe this version, and GLOBAL so a later camera plugin sees the same
# targets without including this file again. Imported include directories are SYSTEM by default,
# so lain's strict flags never fire on OpenCV's headers.
find_package(OpenCV ${LAIN_OPENCV_VERSION} EXACT REQUIRED CONFIG
	COMPONENTS ${LAIN_OPENCV_MODULES}
	PATHS "${_opencv_root}"
	NO_DEFAULT_PATH
	GLOBAL
)

set(LAIN_OPENCV_ROOT_DIR "${_opencv_root}" CACHE INTERNAL "resolved OpenCV prefix")

# CMake derives a consumer's build-tree RPATH from the directories of the shared libraries it links,
# so no manual rpath is needed on macOS/Linux; each library finds its siblings through its own
# $ORIGIN / @loader_path rpath, which the prebuilt verifies.

# ---- Runtime staging (Windows only) -----------------------------------------
# Copy the OpenCV DLLs `target` needs beside its executable. $<TARGET_RUNTIME_DLLS> rather than
# addFFmpeg's glob, because it resolves per CONFIGURATION: a Debug build gets the `d` DLLs, a
# Release build the others, which a glob of the archive's bin/ cannot tell apart. A no-op off
# Windows, so a caller writes it once, unconditionally. Call it BEFORE catch_discover_tests: test
# discovery runs the executable at build time, and POST_BUILD commands run in the order added.
function(lain_opencv_stage_runtime target)
	if(WIN32)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
			COMMAND_EXPAND_LISTS
			COMMENT "Staging the OpenCV runtime beside ${target}")
	endif()
endfunction()

# ---- Distribution obligations -----------------------------------------------
# Apache-2.0 and the permissive licences of the code OpenCV vendors ask a redistributor to pass on
# their texts. Those travel with the binaries, so they are staged into the build tree now, beside
# them. The archive keeps them at share/licenses/opencv4 (etc/licenses on Windows).
set(_opencv_notice_dir "${CMAKE_BINARY_DIR}/third-party/opencv")
file(MAKE_DIRECTORY "${_opencv_notice_dir}")
foreach(_f LICENSE COPYRIGHT MANIFEST.txt)
	if(EXISTS "${_opencv_root}/${_f}")
		configure_file("${_opencv_root}/${_f}" "${_opencv_notice_dir}/${_f}" COPYONLY)
	endif()
endforeach()
file(GLOB _opencv_licenses "${_opencv_root}/share/licenses/opencv4/*" "${_opencv_root}/etc/licenses/*")
foreach(_f IN LISTS _opencv_licenses)
	get_filename_component(_name "${_f}" NAME)
	configure_file("${_f}" "${_opencv_notice_dir}/${_name}" COPYONLY)
endforeach()

# The notice --licenses prints, GENERATED from the pins and the manifest rather than transcribed:
# a transcription goes on claiming the old version after a bump (see lainNotices.cmake).
string(REPLACE ";" ", " _opencv_modules_text "${LAIN_OPENCV_MODULES}")
set(_opencv_notice "${CMAKE_BINARY_DIR}/notices/opencv.txt")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/notices")
file(WRITE "${_opencv_notice}"
"OpenCV ${LAIN_OPENCV_VERSION} (${_opencv_target}) — Apache-2.0\n"
"  This software uses OpenCV (https://github.com/opencv/opencv), linked dynamically and\n"
"  unmodified: ${_opencv_modules_text}.\n"
"  Upstream commit: ${LAIN_OPENCV_COMMIT}\n"
"  Code compiled into these modules: zlib (Zlib); Berkeley SoftFloat, KAZE, AKAZE and MSER's\n"
"  chi-square table (BSD-3-Clause); DLPack (Apache-2.0).\n"
"  Build recipe: https://github.com/luckyneko/opencv-prebuilt\n"
"  Full licence texts and build manifest: third-party/opencv/\n")
set_property(GLOBAL APPEND PROPERTY LAIN_THIRD_PARTY_NOTICES "${_opencv_notice}")

set(LAIN_OPENCV_FOUND TRUE)
