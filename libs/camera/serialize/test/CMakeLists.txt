# lain::camera::serialize tests (Catch2): camera values to documents and back, in memory. The file
# round trip through a real codec is flowview's (test_clibinders, test_cameravertical).

add_executable(test-camera-serialize test_serialize.cpp)
target_compile_features(test-camera-serialize PRIVATE cxx_std_17)
target_link_libraries(test-camera-serialize PRIVATE lain::camera::serialize lain::warnings Catch2::Catch2WithMain)
catch_discover_tests(test-camera-serialize TEST_PREFIX "(camera::serialize) ")
