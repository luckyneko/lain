# lain::io::sequence — the medium-neutral frame-sequence opener. Dispatches a uri to whichever
# medium's io seam can open it, so a graph names one node kind regardless of the footage.
#
# It is a level above io::image / io::video rather than inside lain::media, because media depends
# on no io: a medium-neutral library that knew how to open things would depend on every medium.
# Today it dispatches to one; slice 5 makes it a registry (WORK.md M10).

add_library(io-sequence STATIC
	src/open.cpp
)
add_library(lain::io::sequence ALIAS io-sequence)

target_compile_features(io-sequence PUBLIC cxx_std_17)
target_include_directories(io-sequence PUBLIC include)
target_link_libraries(io-sequence
	PUBLIC lain::media				# open() returns a media::FrameSequence
	PRIVATE lain::io::image			# the one medium it dispatches to today
	PRIVATE lain::warnings
)
