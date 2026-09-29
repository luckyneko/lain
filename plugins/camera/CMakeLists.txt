# Camera plugins — the optional, third-party-backed implementations behind lain's camera contracts
# (ADR-0016). Opt-in like video: an all-disabled configure must resolve nothing at all, which is
# M9's dependency-footprint rule, so every option here defaults OFF.
#
# Today there is one, and it implements nothing yet: M9 slice 0 lands OpenCV (ADR-0026) with only a
# probe of the linked library, so the dependency is proven on every platform before the ChArUco
# detection and calibration that need it are written (slice 1).

option(LAIN_CAMERA_OPENCV "Build the OpenCV camera plugin (Apache-2.0, fetched prebuilt)" OFF)
if(LAIN_CAMERA_OPENCV)
	add_subdirectory(opencv)
endif()
