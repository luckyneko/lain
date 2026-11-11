# lain::rigplan::testing — what tests of rig plans' captures share, header-only: the committed example
# rig plans read as `rigplan render` reads them (exampleplan.h), a capture rendered and required whole,
# what a capture was rendered from without a device (capture.h), and a rig plan's render scene asked on
# the CPU what a camera's ray met (scenecast.h): the still scene behind the movers, or the whole scene
# at a frame. One target, so a test names what it uses by its <lain/rigplan/testing/...> path.
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
	INTERFACE lain::rigplan::capture   # a capture, and the plan one is rendered from (capturedPlan)
	INTERFACE lain::rigplan::scene	   # what a capture draws, and the still scene behind its movers
	INTERFACE lain::rigplan::serialize # the example rig plans as documents
	INTERFACE lain::io::data		   # read through the io seam
	INTERFACE lain::render lain::camera # the ray cast, and a camera's ray through its model
	INTERFACE Catch2::Catch2
)
