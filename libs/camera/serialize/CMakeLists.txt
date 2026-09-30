# lain::camera::serialize — camera values as documents (M9 slice 1, sub-slice 8): camera models, board
# specifications and capture records. A separate target over lain::camera + lain::data by the boundary
# rule lain::media::serialize follows: lain::camera stays free of lain::data, as no foundational
# library in the set links it.
#
# Its namespace is lain::camera, not a serialize sub-namespace: a serialize() must sit in its type's
# own namespace for ADL to find it, and a namespace cannot share a name with a function in the same
# scope (the media::serialize note).

add_library(camera-serialize STATIC
	src/cameramodel.cpp
	src/board.cpp
	src/strict.cpp
)
add_library(lain::camera::serialize ALIAS camera-serialize)

target_compile_features(camera-serialize PUBLIC cxx_std_17)
target_include_directories(camera-serialize PUBLIC include)
target_link_libraries(camera-serialize
	PUBLIC lain::camera # it describes camera values
	PUBLIC lain::data	# and data::Value / Archive appear in its public surface
	PRIVATE lain::string
	PRIVATE lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
