# rigplan's example rig plans (Catch2), held to the catalog that writes them, and the editor's units
# (M15 slice 8d) through rigplan-tested, the objects the app itself links. Driver-free: a capture is asked
# only for its estimate and for its refusal, neither of which reaches a device, and the editor's units
# open no window. Compiles the production builder (examples.cpp) directly, not a copy.

add_executable(test-rigplan-app
	test_examples.cpp
	test_document.cpp
	test_edits.cpp
	test_orbit.cpp
	test_picking.cpp
	test_overlays.cpp
	test_gizmo.cpp
	test_panes.cpp
	"${CMAKE_CURRENT_SOURCE_DIR}/../examples/src/examples.cpp"
)

target_compile_features(test-rigplan-app PRIVATE cxx_std_17)
target_include_directories(test-rigplan-app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../examples/src")
# The committed examples, which the test reads and compares with what the builder writes.
target_compile_definitions(test-rigplan-app PRIVATE LAIN_RIGPLAN_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../examples")
# No camera backend is registered here, deliberately: it is how the test sees a capture refused for
# want of a board renderer in every build, since a registry only grows.
target_link_libraries(test-rigplan-app PRIVATE
	rigplan-tested
	lain::rigplan lain::rigplan::serialize lain::rigplan::capture lain::camera lain::image
	lain::io lain::io::data lain::io::data::codecs lain::io::image::codecs lain::io::mesh::codecs
	lain::testing lain::warnings Catch2::Catch2WithMain
)

catch_discover_tests(test-rigplan-app TEST_PREFIX "(rigplan) " ${LAIN_TEST_DL_PATHS})
