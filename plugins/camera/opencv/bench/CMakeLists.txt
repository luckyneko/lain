# bench-camera-opencv — what the OpenCV feature producer costs in time and memory, reported rather than
# checked (WORK.md, Measurements out of ctest). Each case is a measurement test-camera-opencv-features
# used to hold to a bound; that test keeps what lain's code guarantees, and this prints the numbers.
#
# Not registered with ctest, and Release only (a Debug build skips every case): run it by hand, all of
# it or one case by name, e.g.
#
#   build-release/plugins/camera/opencv/bench/bench-camera-opencv "exact and approximate*"
#
# Its own executable, apart from bench-camera-ceres, because a backend registry only grows and a facade
# asks the first key it holds: the Ceres bench's stand-in matcher ("exact") sorts before "opencv", so in
# one process it would serve these cases' matches.

add_executable(bench-camera-opencv bench_features.cpp bench_sequencefeatures.cpp)
target_compile_features(bench-camera-opencv PRIVATE cxx_std_17)
target_link_libraries(bench-camera-opencv
	PRIVATE lain::camera::opencv lain::camera::testing lain::image lain::media lain::string lain::testing
	PRIVATE lain::warnings Catch2::Catch2WithMain
)
lain_opencv_stage_runtime(bench-camera-opencv)
