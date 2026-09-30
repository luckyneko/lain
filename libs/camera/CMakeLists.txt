# lain::camera — camera geometry and boards (M9, ADR-0016): immutable validated camera models, the
# closed set of distortion models, scalar-generic projection (lain::camera), and board patterns,
# specifications, observations, detection reports and renderings with their backend seams
# (lain::camera::board). std + lain only, so it is always built; the optional OpenCV backend lives in
# plugins/camera/opencv, and no OpenCV type crosses into here.
#
# It depends on no lain::data, as lain::media and lain::flow do not: serialization lands as its own
# target with its first reader.

add_library(camera STATIC
	src/cameramodel.cpp
	src/board/pattern.cpp
	src/board/specification.cpp
	src/board/detection.cpp
	src/board/rendering.cpp
	src/calibration/fitness.cpp
	src/calibration/report.cpp
	src/calibration/estimator.cpp
	src/calibration/board.cpp
)
add_library(lain::camera ALIAS camera)

target_compile_features(camera PUBLIC cxx_std_17)
target_include_directories(camera PUBLIC include)
target_link_libraries(camera
	PUBLIC lain::math  # math::AxisConvention names the camera frame (cameramodel.h)
	PUBLIC lain::core  # Length, Sha256 and Factory are in board values and backend registries
	PUBLIC lain::media # an observation names its frame (media::FrameRef); image::Image comes with it
	PRIVATE lain::task	 # detection and resampling run on the process pool
	PRIVATE lain::log	 # a render that cannot happen says why
	PRIVATE lain::string # the values' toString(), which is what a host shows for one
	PRIVATE lain::meta	 # enum names in that text
	PRIVATE lain::warnings
)

# The node kinds, over lain::flow (added after this directory; a target name is resolved at generate
# time, as media's serialize target resolves lain::data).
add_subdirectory(flow)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
