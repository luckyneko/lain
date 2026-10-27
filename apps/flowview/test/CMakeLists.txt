# flowview unit tests (Catch2). Driver-free — no window, no device. Compiles the production
# session.cpp / groupnav.cpp / canvasids.cpp / validation.cpp / runner.cpp / runpolicy.cpp directly (not
# copies), so the tests exercise the real session file shape, the real group-navigation model, the real
# imnodes id mapping, the real Issues-panel rules, the real coordinator thread and the real run triggers.
# (The undo stack moved to lain::editor, and its tests with it.)

# The production sources the tests run, compiled ONCE into an object library both test executables
# link (test-flowview below, test-flowview-capture further down), so they test one flowview and cannot
# drift into two. An OBJECT library rather than a static one: every object is linked whether or not a
# test names it, exactly as compiling the files into each executable did. Its link libraries are what
# those sources need, PUBLIC so each test inherits them.
add_library(flowview-tested OBJECT
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/session.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/runner.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/runpolicy.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/freshness.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/groupnav.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/validation.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/canvasids.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/graphio.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/framepattern.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/runmode.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/scene.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/dump.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/clibinders.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/camerafiles.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/valueviews.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/imagecanvas.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/imageview.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../src/sequenceview.cpp"
)
target_compile_features(flowview-tested PUBLIC cxx_std_17)
target_include_directories(flowview-tested PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../src")
# lain::flow::serialize: groupnav's layout tree is an EditorTree (the serialize layer's type).
# lain::image + lain::io::data::codecs: graphio names image::Image / ColorRGBf and reads/writes json.
# lain::gui + Vulkan::Loader: the value views draw with ImGui, so they link it (the same recipe
# libs/gui's own tests use). Still driver-free — the POSTER half is what the tests call, and no
# gui::Context is ever constructed; graphio touches the file system, but no device and no window.
target_link_libraries(flowview-tested
	PUBLIC lain::data lain::flow lain::flow::serialize lain::flow-example lain::image
		   lain::io lain::io::sequence lain::media::serialize lain::io::image lain::io::data::codecs lain::io::image::codecs
		   lain::io::video::codecs
		   lain::camera::flow lain::camera::backends lain::camera::serialize
		   lain::log lain::string
		   lain::editor # session.cpp's recents and file, and the undo stack AppContext holds
		   lain::gui Vulkan::Loader
	PRIVATE lain::warnings
)

add_executable(test-flowview
	test_session.cpp
	test_groupnav.cpp
	test_canvasids.cpp
	test_clibinders.cpp
	test_camerafiles.cpp
	test_graphio.cpp
	test_mapscene.cpp
	test_loopscene.cpp
	test_pinkey.cpp
	test_framepattern.cpp
	test_sweep.cpp
	test_valueviews.cpp
	test_catalog.cpp
	test_clone.cpp
	test_payloadscene.cpp
	test_validation.cpp
	test_runner.cpp
	test_runpolicy.cpp
	test_freshness.cpp
	test_runmode.cpp
	test_examples.cpp
	test_cameravertical.cpp
	"${CMAKE_CURRENT_SOURCE_DIR}/../examples/src/examples.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../examples/src/camerafootage.cpp"
)

target_compile_features(test-flowview PRIVATE cxx_std_17)
target_include_directories(test-flowview PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../examples/src")
# The synthetic footage the camera vertical draws, shared with the camera fixture harness.
target_include_directories(test-flowview PRIVATE "${lain_SOURCE_DIR}/libs/camera/test")
# The committed examples, which test_examples loads, runs and compares with what the builder writes.
target_compile_definitions(test-flowview PRIVATE LAIN_FLOWVIEW_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../examples")
# Whether this build has a camera backend, so should OFFER the camera kinds: the catalog test holds the
# capability rule to the configuration, both ways, so a build that offers none when it should (or any
# when it cannot) fails rather than quietly passing. Asked of the plugin target: the option alone says
# nothing on a platform with no prebuilt.
if(TARGET lain::camera::opencv)
	target_compile_definitions(test-flowview PRIVATE LAIN_EXPECT_CAMERA_BACKEND=1)
else()
	target_compile_definitions(test-flowview PRIVATE LAIN_EXPECT_CAMERA_BACKEND=0)
endif()
# ... and whether it can refine a rig, which both registration kinds need besides OpenCV's backends.
if(TARGET lain::camera::ceres)
	target_compile_definitions(test-flowview PRIVATE LAIN_EXPECT_CAMERA_REFINER=1)
else()
	target_compile_definitions(test-flowview PRIVATE LAIN_EXPECT_CAMERA_REFINER=0)
endif()
target_link_libraries(test-flowview PRIVATE flowview-tested lain::testing lain::warnings Catch2::Catch2WithMain)

# catch_discover_tests RUNS this executable at build time to enumerate its cases, so on Windows a
# missing FFmpeg DLL fails the BUILD rather than the test — stage them beside it.
if(TARGET lain::io::video::ffmpeg)
	lain_ffmpeg_stage_runtime(test-flowview)
endif()
if(TARGET lain::camera::opencv)
	lain_opencv_stage_runtime(test-flowview)
endif()

catch_discover_tests(test-flowview TEST_PREFIX "(flowview) " ${LAIN_TEST_DL_PATHS})

# The synthetic-capture vertical (WORK.md M15 slice 7): the committed example rig plan rendered with
# lain::rigplan::capture on every kind of device the machine offers, then registered and scored through
# flowview's committed register.json and compare-rigs.json, as a user runs them. It renders, so unlike
# test-flowview it needs a device; and it draws M9's board, so it is built only where a board renderer
# is. The flowview it runs is test-flowview's (flowview-tested above).
if(TARGET lain::camera::opencv)
	add_executable(test-flowview-capture
		test_capturevertical.cpp
	)
	target_compile_features(test-flowview-capture PRIVATE cxx_std_17)
	# The device a [gpu] case draws on (libs/render/test/gpu.h).
	target_include_directories(test-flowview-capture PRIVATE "${lain_SOURCE_DIR}/libs/render/test")
	target_compile_definitions(test-flowview-capture PRIVATE
		LAIN_FLOWVIEW_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../examples"
		LAIN_RIGPLAN_EXAMPLES_DIR="${lain_SOURCE_DIR}/apps/rigplan/examples"
	)
	target_link_libraries(test-flowview-capture PRIVATE
		flowview-tested
		lain::rigplan::capture lain::rigplan::serialize # the capture, and the rig plan document it renders
		lain::io::mesh::codecs							 # the meshes a rig plan names, as rigplan render registers them
		lain::testing lain::warnings Catch2::Catch2WithMain
	)

	acm_stage_vulkan_runtime(test-flowview-capture)
	# Where the CPU driver is staged beside it, a missing CPU device fails the case rather than skipping
	# it (gpu.h), so a build whose staging broke cannot pass by drawing nothing.
	if(ACM_SWIFTSHADER_ICD)
		target_compile_definitions(test-flowview-capture PRIVATE LAIN_EXPECT_CPU_DEVICE)
	endif()
	if(TARGET lain::io::video::ffmpeg)
		lain_ffmpeg_stage_runtime(test-flowview-capture)
	endif()
	lain_opencv_stage_runtime(test-flowview-capture)

	catch_discover_tests(test-flowview-capture TEST_PREFIX "(flowview::capture) " ${LAIN_TEST_DL_PATHS})
endif()
