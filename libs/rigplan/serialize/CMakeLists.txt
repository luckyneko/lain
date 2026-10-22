# lain::rigplan::serialize — rig plans and specification catalogues as documents (M15 slice 1c). A
# separate target over lain::rigplan + lain::data by the boundary rule lain::camera::serialize follows:
# lain::rigplan stays free of lain::data.
#
# Its namespace is lain::rigplan, not a serialize sub-namespace: a serialize() must sit in its type's
# own namespace for ADL to find it (the media::serialize note).

add_library(rigplan-serialize STATIC
	src/rigplan.cpp
)
add_library(lain::rigplan::serialize ALIAS rigplan-serialize)

target_compile_features(rigplan-serialize PUBLIC cxx_std_17)
target_include_directories(rigplan-serialize PUBLIC include)
target_link_libraries(rigplan-serialize
	PUBLIC lain::rigplan lain::data
	PRIVATE lain::camera::serialize # ImageGeometry and Distortion as the camera-model document writes them
	PRIVATE lain::media::serialize	# FrameRate as {numerator, denominator}
	PRIVATE lain::string lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
