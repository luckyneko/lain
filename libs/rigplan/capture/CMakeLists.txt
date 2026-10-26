# lain::rigplan::capture — a rig plan's synthetic capture written to disk (WORK.md M15 slice 5e): the
# refusals, the truth, the frames rendered camera by camera and recorded on the process pool, and
# capture.json last; and the size estimate given before any of it. Above lain::rigplan::scene and
# lain::render, since it drives the device, and over the io seams only: the codecs are its caller's to
# register.
#
# Its namespace is lain::rigplan, as lain::rigplan::scene's is.

add_library(rigplan-capture STATIC
	src/capture.cpp
	src/estimate.cpp
	src/outputs.cpp
)
add_library(lain::rigplan::capture ALIAS rigplan-capture)

target_compile_features(rigplan-capture PUBLIC cxx_std_17)
target_include_directories(rigplan-capture PUBLIC include)
target_link_libraries(rigplan-capture
	PUBLIC lain::rigplan lain::render
	PRIVATE lain::rigplan::scene lain::rigplan::serialize # the render scene; the truth and the manifest as documents
	PRIVATE lain::camera::serialize						  # camera models and the rig as camera documents
	PRIVATE lain::io lain::io::image lain::io::data		  # frames and documents through the io seams
	PRIVATE lain::task lain::meta lain::string lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
