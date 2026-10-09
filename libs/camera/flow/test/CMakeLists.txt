# lain::camera::flow tests (Catch2), over stand-in backends: no OpenCV. flowview's tests cover the
# real backend through the production registration.
#
# Several executables, because a core::Factory only grows: registration is staged from EMPTY
# registries, one backend kind at a time, and that needs a process in which nothing has registered
# yet; and two sets of stand-ins that register under the same keys cannot share a process.

add_executable(test-camera-flow-registration test_registration.cpp)
target_compile_features(test-camera-flow-registration PRIVATE cxx_std_17)
target_include_directories(test-camera-flow-registration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test")
target_link_libraries(test-camera-flow-registration PRIVATE lain::camera::flow lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-flow-registration TEST_PREFIX "(camera::flow) ")

# The same staging from empty registries for targetless registration, which needs no board backend:
# a process in which no board backend ever registers.
add_executable(test-camera-flow-targetlesskinds test_targetlesskinds.cpp)
target_compile_features(test-camera-flow-targetlesskinds PRIVATE cxx_std_17)
target_link_libraries(test-camera-flow-targetlesskinds PRIVATE lain::camera::flow lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-flow-targetlesskinds TEST_PREFIX "(camera::flow) ")

add_executable(test-camera-flow test_nodes.cpp)
target_compile_features(test-camera-flow PRIVATE cxx_std_17)
# The method module's synthetic scene: a detector and an estimator that answer from a known truth.
target_include_directories(test-camera-flow PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test")
target_link_libraries(test-camera-flow PRIVATE lain::camera::flow lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-flow TEST_PREFIX "(camera::flow) ")

# registerCameras over the registration method module's stand-in rig. Its own executable: the rig's
# stand-ins register under the same keys as the calibration scene's.
add_executable(test-camera-flow-registercameras test_registercameras.cpp)
target_compile_features(test-camera-flow-registercameras PRIVATE cxx_std_17)
target_include_directories(test-camera-flow-registercameras PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test")
target_link_libraries(test-camera-flow-registercameras PRIVATE lain::camera::flow lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-flow-registercameras TEST_PREFIX "(camera::flow) ")

# registerCamerasTargetless over the targetless method module's stand-in scene.
add_executable(test-camera-flow-registertargetless test_registertargetless.cpp)
target_compile_features(test-camera-flow-registertargetless PRIVATE cxx_std_17)
target_include_directories(test-camera-flow-registertargetless PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../test")
target_link_libraries(test-camera-flow-registertargetless PRIVATE lain::camera::flow lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-flow-registertargetless TEST_PREFIX "(camera::flow) ")
