# rigplan — rig design (WORK.md M15). A lain::app client: the editor, a window over lain::gui (M15
# slices 8 and 9), and the `render` subcommand, which writes a rig plan's synthetic capture headless
# (lain::rigplan::capture).

# The editor's driver-free units: the document, the selection, the edits, the orbit, the picking, the
# overlays and the gizmo bridge (M15 slice 8d), and the session file and the panes (8e). Compiled ONCE
# into an object library that rigplan and its test both link, as flowview-tested is, so the test
# exercises the one editor the app runs and the two cannot drift. An OBJECT library, so every object is
# linked whether or not something names it. Its link libraries are what these sources need, PUBLIC so
# each consumer inherits them. Driver-free: the panes draw with ImGui, which a headless ImGui frame runs
# in the test, lain::render is here for its View and Segment, and nothing opens a window or a device
# (Vulkan::Loader resolves archimedes' vk* symbols at link time, as for flowview-tested).
add_library(rigplan-tested OBJECT
	src/document.cpp
	src/selection.cpp
	src/edits.cpp
	src/orbit.cpp
	src/picking.cpp
	src/overlays.cpp
	src/gizmo.cpp
	src/session.cpp
	src/panes/fields.cpp
	src/panes/inspector.cpp
	src/panes/issues.cpp
	src/panes/menubar.cpp
	src/panes/outliner.cpp
	src/panes/statusbar.cpp
)
target_compile_features(rigplan-tested PUBLIC cxx_std_17)
target_include_directories(rigplan-tested PUBLIC src)
target_link_libraries(rigplan-tested
	PUBLIC lain::rigplan lain::rigplan::serialize lain::rigplan::scene lain::render lain::camera lain::core lain::data
		   lain::io lain::io::data # a rig plan read and written as a file
		   lain::editor			   # the undo history a document holds, the session, the guard
		   lain::gui			   # ImGuizmo, through gizmo.h, and the panes' ImGui
		   lain::io::image lain::io::mesh # the pickers' filters: what a picture and a mesh can be
		   lain::string archimedes Vulkan::Loader
	PRIVATE lain::warnings
)

# The app: the headless `render`, and the editor's window (M15 slice 8e), which needs a device and so is
# not under ctest; its panes are rigplan-tested's.
add_executable(rigplan
	src/main.cpp
	src/rigplanapp.cpp
	src/mainwindow.cpp
)

target_compile_features(rigplan PRIVATE cxx_std_17)
target_link_libraries(rigplan
	PRIVATE rigplan-tested lain::app lain::core lain::rigplan::capture lain::rigplan::serialize
			lain::io lain::io::data lain::io::data::codecs lain::io::image::codecs lain::io::mesh::codecs # the rig plan document, the frames, the meshes a plan names
			lain::camera::backends # the board renderer, where a camera plugin is built
			lain::string lain::log archimedes Vulkan::Loader lain::warnings
)

# The vendored MoltenVK ICD beside the binary (macOS; a no-op elsewhere), and archimedes' CPU device,
# as flowview stages them: a capture needs a device, and a machine with no GPU renders on the CPU one.
acm_stage_vulkan_runtime(rigplan)
# The OpenCV DLLs, for the board renderer. Asked of the plugin TARGET, not the option, as flowview does.
if(TARGET lain::camera::opencv)
	lain_opencv_stage_runtime(rigplan)
endif()
# And the loader itself on Windows, where the executable resolves vulkan-1.dll from its own directory.
if(WIN32)
	add_custom_command(TARGET rigplan POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Vulkan::Loader>" "$<TARGET_FILE_DIR:rigplan>"
		COMMENT "Staging the Vulkan loader beside rigplan")
endif()

# rigplan-examples writes the example rig plans in examples/, as flowview-examples writes flowview's
# example documents. It is BUILT with everything, so every platform keeps compiling it, but only RUN
# through `rig-plans` (flowview's `examples` target already has that name), which mirrors `format`:
# commit what it wrote. The example test holds the committed documents to what this builds.
add_executable(rigplan-examples
	examples/src/main.cpp
	examples/src/examples.cpp
)
target_compile_features(rigplan-examples PRIVATE cxx_std_17)
target_include_directories(rigplan-examples PRIVATE examples/src)
target_link_libraries(rigplan-examples
	PRIVATE lain::core lain::data lain::rigplan lain::rigplan::serialize lain::camera lain::image lain::io::data
			lain::io::data::codecs lain::log lain::warnings
)

add_custom_target(rig-plans
	COMMAND rigplan-examples "${CMAKE_CURRENT_SOURCE_DIR}/examples"
	COMMENT "Writing the example rig plans in apps/rigplan/examples"
	VERBATIM)

# synthetic-capture renders the example rig plan into the build folder (build/synthetic-capture), as
# flowview's camera-footage draws M9's footage, with a README.txt of the flowview commands that register
# and score it. Never committed. The plan waves M9's board, so it is defined only where a board renderer
# is; it renders on the machine's best device (`--device cpu` is rigplan's own flag).
if(TARGET lain::camera::opencv)
	set(syntheticCapture "${CMAKE_BINARY_DIR}/synthetic-capture")
	set(flowviewExamples "${lain_SOURCE_DIR}/apps/flowview/examples")
	string(CONFIGURE [[
A synthetic capture of apps/rigplan/examples/three-poles.json, rendered by rigplan render (the
synthetic-capture target). Run the commands from this folder.

rig/              the footage, one folder per camera
nominal/          each camera's nominal model: what it was meant to be
truth/models/     each camera's realised model: what it was rendered through
truth/rig.json    the rig it was rendered as, relative to the plan's reference camera
truth/poses.json  every camera's pose in the plan frame; truth/board.json the board's corners in view
capture.json      the manifest, with the rig plan it was rendered from

Registered with its true models, and scored against the truth:
  "$<TARGET_FILE:flowview>" run -g "@flowviewExamples@/register.json" \
      --footage rig --models truth/models --report report.txt --rig rig.json
  "$<TARGET_FILE:flowview>" run -g "@flowviewExamples@/compare-rigs.json" \
      --expected truth/rig.json --found rig.json --comparison comparison.txt

With --models nominal instead, the rig is registered with the models the cameras were meant to have,
and the comparison shows what not calibrating them costs.
]] syntheticCaptureReadme @ONLY)
	# Per configuration, since a multi-config generator puts flowview in a folder per configuration.
	file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/synthetic-capture-README.txt"
		 CONTENT "${syntheticCaptureReadme}")
	add_custom_target(synthetic-capture
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${syntheticCapture}"
		COMMAND rigplan render "${CMAKE_CURRENT_SOURCE_DIR}/examples/three-poles.json" --out "${syntheticCapture}"
		COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/synthetic-capture-README.txt"
				"${syntheticCapture}/README.txt"
		COMMENT "Rendering the example rig plan's synthetic capture in ${syntheticCapture}"
		VERBATIM)
	add_dependencies(synthetic-capture flowview) # what the README's commands run
endif()

if(LAIN_BUILD_TESTING)
	add_subdirectory(test)
endif()
