# bench-camera-ceres — what a registration through the Ceres refiner costs in time, reported rather than
# checked (WORK.md, Measurements out of ctest). Each case is a measurement test-camera-ceres used to hold
# to a bound; that test keeps what lain's code guarantees, and this prints the numbers.
#
# Not registered with ctest, and Release only (a Debug build skips every case): run it by hand, e.g.
#
#   build-release/plugins/camera/ceres/bench/bench-camera-ceres
#
# Its own executable, apart from bench-camera-opencv: a backend registry only grows, and this one's
# stand-in feature backends would otherwise sit beside OpenCV's (opencv/bench/CMakeLists.txt).

add_executable(bench-camera-ceres bench_targetless.cpp)
target_compile_features(bench-camera-ceres PRIVATE cxx_std_17)
target_link_libraries(bench-camera-ceres
	PRIVATE lain::camera::ceres lain::camera::testing lain::string lain::testing
	PRIVATE lain::warnings Catch2::Catch2WithMain
)
