# Provides Ceres::ceres — Ceres Solver 2.2.0, static, built from source — for the camera
# registration plugin's global refinement (ADR-0017).
#
# BUILT, NOT FETCHED AS A BINARY, unlike OpenCV (ADR-0026). Ceres' public headers include Eigen's,
# so a prebuilt would pin lain's Eigen as well and need a Debug and a Release set for MSVC; a static
# source build has no ABI to keep in step, at a cost in build minutes recorded in WORK.md.
#
# 2.2.0, the latest release, logs through miniglog — a small glog substitute compiled into Ceres
# under Ceres' own BSD licence — so there is no glog, no gflags and no Abseil (only unreleased Ceres
# master needs Abseil). Everything optional is forced off: SuiteSparse (a separate licence decision,
# ADR-0017), METIS, LAPACK, CUDA, Accelerate, and Ceres' tests, examples, benchmarks and docs.
# Eigen's sparse Cholesky stays on, since it is the sparse solver ADR-0017 names; Eigen3::Eigen
# carries EIGEN_MPL2_ONLY (addEigen.cmake), so building Ceres proves no LGPL Eigen code is reached.
#
# The options are set as NORMAL variables inside a function, not cache entries: Ceres' option()
# honours a normal variable (CMP0077), and several of its names — BUILD_TESTING, BUILD_SHARED_LIBS,
# LAPACK — are generic enough that a cache entry would reach other subprojects.
#
# Included by the Ceres camera plugin, so nothing is fetched unless that plugin is enabled; guarded
# so a second include is a no-op. Sets LAIN_CERES_FOUND: always TRUE once configured, since a
# source build is available wherever lain builds. A broken fetch still fails configure.

if(TARGET Ceres::ceres)
	set(LAIN_CERES_FOUND TRUE)
	return()
endif()
set(LAIN_CERES_FOUND FALSE)

include(addEigen) # Eigen3::Eigen, which Ceres' find_package(Eigen3) resolves to
include(FetchContent)

set(LAIN_CERES_VERSION "2.2.0")
set(CERES_FILE "github.com/ceres-solver/ceres-solver/archive/refs/tags/${LAIN_CERES_VERSION}.tar.gz")

FetchContent_Declare(ceres
	URL          "https://${CERES_FILE}"
	DOWNLOAD_DIR "${CMAKE_SOURCE_DIR}/.cache/fetch/${CERES_FILE}"
)

function(_lain_add_ceres)
	set(MINIGLOG ON)
	# Warnings and errors only. Ceres' INFO and VLOG lines would otherwise reach stderr from inside
	# every solve; what a solve did is the registration report's job.
	set(MINIGLOG_MAX_LOG_LEVEL -1)
	set(GFLAGS OFF)
	set(SUITESPARSE OFF)
	set(EIGENSPARSE ON)
	set(EIGENMETIS OFF)
	set(LAPACK OFF)
	set(USE_CUDA OFF)
	set(ACCELERATESPARSE OFF)
	set(ENABLE_BITCODE OFF)
	# Fixed-size Schur specialisations nearly triple Ceres' build, measured 2026-10-05 (Debug, four
	# cores: 3m25s wall and 11m10s CPU against 1m13s and 4m04s), for a speed-up registration does not
	# need: the dynamic path handles its blocks.
	set(SCHUR_SPECIALIZATIONS OFF)
	set(BUILD_SHARED_LIBS OFF)
	set(BUILD_TESTING OFF)
	set(BUILD_EXAMPLES OFF)
	set(BUILD_BENCHMARKS OFF)
	set(BUILD_DOCUMENTATION OFF)
	set(EXPORT_BUILD_DIR OFF)
	set(PROVIDE_UNINSTALL_TARGET OFF)
	FetchContent_MakeAvailable(ceres)
endfunction()
_lain_add_ceres()
FetchContent_GetProperties(ceres) # ceres_SOURCE_DIR, which the function's scope kept to itself

# Re-expose Ceres' headers as SYSTEM, as addGLM.cmake does GLM's, so lain's strict flags never fire
# on Ceres' templates. Ceres itself is compiled with its own flags, never lain::warnings.
get_target_property(_ceres_inc ceres INTERFACE_INCLUDE_DIRECTORIES)
if(_ceres_inc)
	set_target_properties(ceres PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
	target_include_directories(ceres SYSTEM INTERFACE ${_ceres_inc})
endif()

# ---- Distribution obligations -----------------------------------------------
# BSD-3-Clause asks a redistributor to reproduce the copyright notice and licence in the binary's
# materials, and the Abseil-derived headers Ceres compiles in (include/ceres/internal/fixed_array.h and
# memory.h) are Apache-2.0, whose text Ceres' LICENSE also carries. Ceres is linked statically, so the
# whole file is printed with --licenses (lainNotices.cmake).
include(lainNotices)
lain_register_notice(ceres NAME "Ceres Solver" VERSION "${LAIN_CERES_VERSION}" SPDX "BSD-3-Clause AND Apache-2.0"
	SOURCE_DIR "${ceres_SOURCE_DIR}" FILES LICENSE
	DETAILS
		"This software uses Ceres Solver (http://ceres-solver.org), linked statically and unmodified,"
		"with its bundled miniglog. Built without SuiteSparse, METIS, LAPACK, CUDA or gflags."
		"Source: https://${CERES_FILE}")

set(LAIN_CERES_FOUND TRUE)
