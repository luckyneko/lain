#pragma once

#include <lain/camera/registration/report.h>
#include <lain/data/value.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Registered rigs as documents (CONTEXT.md, "Rig document"): what a registration found, leaving the
// process. Each camera's transform is referenceFromCamera in the camera frame (XRightYDownZForward), its
// rotation the unit quaternion RigidTransform holds and its translation in metres when the scale is
// Metric.
namespace lain::camera::registration
{
	// The version a rig document is written at, and the newest this build reads.
	constexpr std::uint32_t kRegisteredRigDocumentVersion = 1;

	// A registered rig as a document: {version, reference, scale: {scale, evidence}, cameras: [{camera,
	// referenceFromCamera: {rotation: {w, x, y, z}, translation: {x, y, z}}}]}, the cameras in the rig's
	// order (ascending by identity).
	data::Value registeredRigToValue(const RegisteredRig& rig);

	// The outcome of reading a rig document: a rig, or every reason there is none.
	struct RegisteredRigRead
	{
		std::optional<RegisteredRig> rig;
		std::vector<std::string> problems; // empty exactly when `rig` is set
	};

	// A registered rig from a document, read STRICTLY, as a camera-model document is: an unknown or
	// missing key, an unknown scale or a value of the wrong kind is a problem named by its path, never a
	// silent default. Then what makes it a rig: a camera at least, each with an identity and none twice,
	// a reference among them whose transform is the identity, and every rotation a unit quaternion (to
	// 1e-6, which RigidTransform then normalises). A rotation that is not one is refused rather than
	// normalised, as RigidTransform::fromMatrix refuses a matrix that is not rigid. The cameras are read
	// in the rig's order, whatever order the document lists them in.
	RegisteredRigRead registeredRigFromValue(const data::Value& document);
} // namespace lain::camera::registration
