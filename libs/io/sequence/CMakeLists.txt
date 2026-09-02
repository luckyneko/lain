# lain::io::sequence — the medium-neutral frame-sequence opener. Dispatches a uri to whichever
# medium's io seam can open it, so a graph names one node kind regardless of the footage.
#
# It is a level above io::image / io::video rather than inside lain::media, because media depends
# on no io: a medium-neutral library that knew how to open things would depend on every medium.
#
# The split between the two sources is deliberate, and each has its own header: open.h/open.cpp are
# the registry and name no medium, openers.h/openers.cpp are the wiring and name both. Only the
# second pair has to change when a medium is added, and a medium outside this tree needs neither.

add_library(io-sequence STATIC
	src/open.cpp
	src/openers.cpp
)
add_library(lain::io::sequence ALIAS io-sequence)

target_compile_features(io-sequence PUBLIC cxx_std_17)
target_include_directories(io-sequence PUBLIC include)
target_link_libraries(io-sequence
	PUBLIC lain::media				# open() returns a media::FrameSequence
	PRIVATE lain::io				# io::extensionKey — the dispatch key
	PRIVATE lain::io::image lain::io::video	# the built-in media (openers.cpp only)
	PRIVATE lain::log lain::warnings
)

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
