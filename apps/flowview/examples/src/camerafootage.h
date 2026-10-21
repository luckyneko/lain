#pragma once

#include "syntheticfootage.h" // camera::synthetic::Pinhole, the board footage's camera

#include <lain/camera/board/specification.h>
#include <lain/camera/cameramodel.h>
#include <lain/math/rigidtransform.h>

#include <cstddef>
#include <filesystem>

// Camera footage for the camera examples, drawn in plain C++ (libs/camera/test's synthetic footage and
// textured scene, the board from the production renderer): what the camera verticals run the committed
// examples on, and what `flowview-examples --camera-footage <folder>` writes for a person to bind by
// hand. ONE writer per kind of footage, so what a person opens in the gui is what the tests checked.
// Never committed: it is hundreds of stills, and drawn again in seconds.
//
// Every writer throws std::runtime_error, naming what failed. A board needs a board renderer
// (camera::board::canRender()); `rendered` false writes blank frames of the right size instead, so a
// build that cannot render still has footage to fail on for the backend it lacks.
namespace flowview::examples::footage
{
	// The pinhole the board footage is seen through: 640x480 with distinct intrinsics.
	const lain::camera::synthetic::Pinhole& boardCamera();
	lain::camera::CameraModel boardCameraModel();

	// The board every board footage shows: 7 x 5 squares of 24 mm, ArUco 5x5_100, markers 0.75 of a
	// square, as the calibrate examples' boardSpecification says.
	lain::camera::board::Specification board();

	// One camera's footage of the board sweeping through its view, `frames` stills in `dir`/footage,
	// which is returned. Blank when `rendered` is false.
	std::filesystem::path writeBoardFootage(const std::filesystem::path& dir, std::size_t frames, bool rendered = true);

	// A moving camera's footage of a textured scene with no board in it: boardCamera along the textured
	// scene's arc, `frames` stills in `dir`/footage, which is returned. Drawn on the process pool when
	// one is started.
	std::filesystem::path writeMovingFootage(const std::filesystem::path& dir, std::size_t frames);

	// A rig of three cameras, as a user binds one: `dir`/rig/cam0..cam2, each `frames` stills of one
	// board sweeping through their shared view, and `dir`/models/cam0..cam2.json, each camera's model.
	// boardRigCamera(c) is camera c's referenceFromCamera, relative to cam0, and boardRigModel(c) its
	// pinhole: cam0's is boardCamera, and the others' focal lengths and principal points differ from it
	// and from each other, so a camera paired with another's model is seen rather than hidden.
	void writeBoardRig(const std::filesystem::path& dir, std::size_t frames, bool rendered);
	lain::math::RigidTransformd boardRigCamera(std::size_t camera);
	lain::camera::CameraModel boardRigModel(std::size_t camera);

	// A rig of three fixed cameras round the textured scene: `dir`/rig/cam0..cam2, three stills each
	// from a point on an arc, and `dir`/models/cam0..cam2.json. texturedRigCamera(c) is camera c's pose
	// on the arc, in the scene's frame.
	void writeTexturedRig(const std::filesystem::path& dir, bool rendered);
	lain::math::RigidTransformd texturedRigCamera(std::size_t camera);

	// Everything above, for a person to bind by hand, under `folder`: board/footage (24 frames),
	// moving/footage (40), board-rig (24 frames a camera) and textured-rig, with a README.txt saying
	// which example each is for and the command that runs it.
	void writeAll(const std::filesystem::path& folder);
} // namespace flowview::examples::footage
