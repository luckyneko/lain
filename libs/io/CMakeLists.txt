# lain::io — the transport front seam. Two shapes over one backend per scheme: the
# whole-asset read(uri) -> memory::Buffer / write(uri, bytes), and the incremental,
# seekable Stream a video container needs (WORK.md M10). Media-agnostic; it moves
# bytes and never decodes (codecs live in lain::io::image et al., WORK.md M3). Only
# the local/file scheme is served today; remote/s3 dispatch behind the same entry
# points later. Depends on lain::memory (Buffer in the API) + lain::log (reasons).

add_library(io STATIC
	src/read.cpp
	src/write.cpp
	src/stream.cpp
	src/localstream.cpp
	src/uri.cpp
)
add_library(lain::io ALIAS io)

target_compile_features(io PUBLIC cxx_std_17)
target_include_directories(io PUBLIC include)
target_link_libraries(io
	PUBLIC lain::memory
	PRIVATE lain::log lain::warnings
)

# The per-media codec-loader seams nest under io (lain::io::image, lain::io::data, later
# ::video / ::audio). Codec-free themselves; codec plugins live under plugins/io/<media>.
add_subdirectory(image)
add_subdirectory(data)
add_subdirectory(sequence) # the medium-neutral frame-sequence opener over them (WORK.md M10)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
