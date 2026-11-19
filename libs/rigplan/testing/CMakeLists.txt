# lain::rigplan::testing — what tests of rig plans share, header-only: a small rig plan whose every
# tolerance is non-zero (testplan.h), a stand-in board renderer (standinboard.h), the committed example
# rig plans read as `rigplan render` reads them (exampleplan.h), a capture rendered and required whole,
# what a capture was rendered from without a device (capture.h), and a rig plan's render scene asked on
# the CPU what a camera's ray met (scenecast.h): the still scene behind the movers, or the whole scene
# at a frame. One target, so a test names what it uses by its <lain/rigplan/testing/...> path. The
# driver-free rig plan tests link it for testplan.h alone, and so link the render stack they never call.
#
# Built with the tests only, so every header may assert through Catch2.

add_library(rigplan-testing INTERFACE)
add_library(lain::rigplan::testing ALIAS rigplan-testing)

target_compile_features(rigplan-testing INTERFACE cxx_std_17)
target_include_directories(rigplan-testing INTERFACE include)
target_compile_definitions(rigplan-testing INTERFACE
	LAIN_RIGPLAN_EXAMPLES_DIR="${lain_SOURCE_DIR}/apps/rigplan/examples" # where examplePlan reads from
)
target_link_libraries(rigplan-testing
	INTERFACE lain::rigplan lain::core # a rig plan and the identities in it (testplan.h)
	INTERFACE lain::testing			   # its paths, rooted on this platform (testplan.h)
	INTERFACE lain::image			   # what the stand-in board renderer draws into (standinboard.h)
	INTERFACE lain::rigplan::capture   # a capture, and the plan one is rendered from (capturedPlan)
	INTERFACE lain::rigplan::scene	   # what a capture draws, and the still scene behind its movers
	INTERFACE lain::rigplan::serialize # the example rig plans as documents
	INTERFACE lain::io::data		   # read through the io seam
	INTERFACE lain::render lain::camera # the ray cast, a camera's ray through its model, the board seam
	INTERFACE Catch2::Catch2
)
