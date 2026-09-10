# lain::testing — test-support helpers, header-only and built only under LAIN_BUILD_TESTING.
# Currently one thing: scratch paths on disk that no concurrently-running test can collide with
# (see scratch.h for why a per-file counter cannot do that job).
#
# It deliberately holds no test target of its own: what it provides is exercised by every test
# suite that links it, and a fixture asserting "this path is unique" could only ever check the one
# process it runs in — which is precisely the assumption that made the private copies wrong.

add_library(lain_testing INTERFACE)
add_library(lain::testing ALIAS lain_testing)

target_compile_features(lain_testing INTERFACE cxx_std_17)
target_include_directories(lain_testing INTERFACE include)
target_link_libraries(lain_testing INTERFACE lain::core)
