#pragma once

#include <lain/core/factory.h>
#include <lain/flow/node.h>

#include <string>
#include <vector>

namespace lain::camera
{
	// The camera node kinds' factory keys. They are SERIALIZED: a saved document names a node by its
	// kind, and an unknown kind drops the node and its edges.
	inline constexpr const char* kBoardSpecificationKey = "boardSpecification";
	inline constexpr const char* kRenderBoardKey = "renderBoard";
	inline constexpr const char* kDetectBoardKey = "detectBoard";
	inline constexpr const char* kCalibrateCameraKey = "calibrateCamera";
	inline constexpr const char* kCameraModelKey = "cameraModel";

	// The camera node kinds this build can RUN, in display order: each kind is here only when the
	// backend it needs is registered (ADR-0016). Unlike video, where the node stays and reports a
	// missing format, a camera node without its backend cannot do anything at all, so it is not
	// offered.
	//
	// Asked of the backend registries at the time of the call, so call it after
	// registerCameraBackends(). A host's menu reads this and registerCameraNodes() registers exactly
	// it, so the two cannot disagree.
	std::vector<std::string> availableCameraNodeKeys();

	// Register the kinds availableCameraNodeKeys() names into `factory`.
	void registerCameraNodes(core::Factory<flow::Node>& factory);

	// Register the camera payload types as addable port types (flow::registerPortType): a board
	// specification, a detection report, a calibration report and a camera model. ALWAYS, whatever
	// the backends: a port type is vocabulary, and an unregistered one cannot be named on save, so a
	// boundary pin of that type would be dropped with its wiring.
	void registerCameraPortTypes();
} // namespace lain::camera
