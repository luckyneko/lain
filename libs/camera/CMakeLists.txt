# lain::camera — camera geometry (M9, ADR-0016): immutable validated camera models, the closed set
# of distortion models, and scalar-generic projection. std + lain only, so it is always built; the
# optional OpenCV adapter lives in plugins/camera/opencv, and no OpenCV type crosses into here.
#
# It depends on no lain::data, as lain::media and lain::flow do not: serialization lands as its own
# target with its first reader.

add_library(camera STATIC
	src/cameramodel.cpp
)
add_library(lain::camera ALIAS camera)

target_compile_features(camera PUBLIC cxx_std_17)
target_include_directories(camera PUBLIC include)
target_link_libraries(camera
	PUBLIC lain::math # math::AxisConvention names the camera frame (cameramodel.h)
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
