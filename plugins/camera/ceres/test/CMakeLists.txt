# The Ceres plugin's tests (Catch2): the linked Ceres is the approved build, and lain's projection
# differentiates correctly through Ceres' automatic differentiation — the "one projection
# implementation" claim of M9's review notes, checked rather than argued. The pinned version comes
# from cmake/addCeres.cmake, so the module and this test cannot describe two builds.

add_executable(test-camera-ceres
	test_build.cpp
	test_freemodel.cpp
	test_jet.cpp
	test_landmarks.cpp
	test_refiner.cpp
	test_targetless.cpp
	test_targetlesscalibration.cpp
)

target_compile_features(test-camera-ceres PRIVATE cxx_std_17)
target_compile_definitions(test-camera-ceres PRIVATE
	LAIN_CERES_PINNED_VERSION="${LAIN_CERES_VERSION}"
)
target_link_libraries(test-camera-ceres
	PRIVATE lain::camera::ceres
	# The tests themselves name Ceres: they differentiate lain's projection with its Jets and run a
	# solve. The plugin links it privately.
	PRIVATE Ceres::ceres
	# The shared test camera, stand-in rig and scene, and report printers.
	PRIVATE lain::camera::testing lain::testing lain::warnings Catch2::Catch2WithMain
)

catch_discover_tests(test-camera-ceres TEST_PREFIX "(camera::ceres) " TEST_SPEC "~[scale]")
# The scale cases each keep the process pool busy, and one bounds its wall-clock time, a bound that
# means something only while the case has the cores: under `ctest -j8` beside the end-to-end suites the
# 100-camera targetless registration took 154 to 181 s against its 120, and 36 s alone. So ctest runs
# them with nothing beside them.
catch_discover_tests(test-camera-ceres TEST_PREFIX "(camera::ceres) " TEST_SPEC "[scale]" PROPERTIES RUN_SERIAL TRUE)
