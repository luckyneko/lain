# lain::render::testing — what tests that draw through lain::render share, header-only: the devices a
# [gpu] case draws on (gpu.h), one strongly distorted camera per distortion model (testcameras.h), and a
# small render scene with a view of it (testscene.h). One target, so a test names what it uses by its
# <lain/render/testing/...> path instead of reaching into libs/render/test by directory.
#
# Built with the tests only, so every header may assert through Catch2. LAIN_EXPECT_CPU_DEVICE (gpu.h)
# stays each executable's own, since it is true only of one that stages the CPU driver beside itself.

add_library(render-testing INTERFACE)
add_library(lain::render::testing ALIAS render-testing)

target_compile_features(render-testing INTERFACE cxx_std_17)
target_include_directories(render-testing INTERFACE include)
target_link_libraries(render-testing
	INTERFACE lain::render							# a render scene and a view of it (testscene.h)
	INTERFACE lain::camera lain::image lain::mesh	# the cameras, colours and meshes they are made of
	INTERFACE archimedes							# the devices a [gpu] case draws on (gpu.h)
	INTERFACE Catch2::Catch2
)
