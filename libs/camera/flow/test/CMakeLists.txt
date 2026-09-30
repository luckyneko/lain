# lain::camera::flow tests (Catch2), over stand-in backends: no OpenCV. flowview's tests cover the
# real backend through the production registration.
#
# Two executables, because a core::Factory only grows: registration is staged from EMPTY registries,
# one backend kind at a time, and that needs a process in which nothing has registered yet.

add_executable(test-camera-flow-registration test_registration.cpp)
target_compile_features(test-camera-flow-registration PRIVATE cxx_std_17)
target_include_directories(test-camera-flow-registration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test")
target_link_libraries(test-camera-flow-registration PRIVATE lain::camera::flow lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-flow-registration TEST_PREFIX "(camera::flow) ")

add_executable(test-camera-flow test_nodes.cpp)
target_compile_features(test-camera-flow PRIVATE cxx_std_17)
# The method module's synthetic scene: a detector and an estimator that answer from a known truth.
target_include_directories(test-camera-flow PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test")
target_link_libraries(test-camera-flow PRIVATE lain::camera::flow lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-flow TEST_PREFIX "(camera::flow) ")
