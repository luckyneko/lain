# lain::camera::testing — what the camera tests share, header-only: a test camera and board, stand-in
# backends that answer from a known truth (a board scene, a rig, a scene of static features and a moving
# camera, a refiner that hands back its start), footage drawn in plain C++ (a board, a textured scene),
# and the printers and comparisons the golden and end-to-end cases read reports with. One target, so a
# test names what it uses by its <lain/camera/testing/...> path instead of reaching into
# libs/camera/test by directory.
#
# Built wherever something uses it, which is tests AND apps: flowview-examples draws the camera
# examples' footage with the tests' own scenes (camerafootage.cpp), so a person and a test see the same
# frames, and it is built with the apps whether or not the tests are. That tool includes only headers
# that need no test framework (syntheticfootage.h, texturedscene.h). The rest assert through Catch2, so
# they are usable only in a build that has it, and the target links Catch2 only where it was built.
#
# The golden reports are not here: they are the data of libs/camera/test's own cases, and goldentext.h
# finds them through LAIN_CAMERA_GOLDEN_DIR, which the test that includes it defines.

add_library(camera-testing INTERFACE)
add_library(lain::camera::testing ALIAS camera-testing)

target_compile_features(camera-testing INTERFACE cxx_std_17)
target_include_directories(camera-testing INTERFACE include)
target_link_libraries(camera-testing
	INTERFACE lain::camera # every header describes camera values
	INTERFACE lain::image  # the footage is drawn into image::Image
	INTERFACE lain::meta   # enum names in the report printers
	INTERFACE lain::task   # the textured scene draws its frames on the process pool
)
if(TARGET Catch2::Catch2)
	target_link_libraries(camera-testing INTERFACE Catch2::Catch2)
endif()
