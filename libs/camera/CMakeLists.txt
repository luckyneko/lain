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
)
add_library(lain::camera ALIAS camera)

target_compile_features(camera PUBLIC cxx_std_17)
target_include_directories(camera PUBLIC include)
target_link_libraries(camera
	PUBLIC lain::math  # math::AxisConvention names the camera frame (cameramodel.h)
	PUBLIC lain::core  # Length, Sha256 and Factory are in board values and backend registries
	PUBLIC lain::media # an observation names its frame (media::FrameRef); image::Image comes with it
	PRIVATE lain::log  # a render that cannot happen says why
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
