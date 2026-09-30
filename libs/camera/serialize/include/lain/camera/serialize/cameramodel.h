#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/camera/distortion.h>
#include <lain/data/data.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Camera models as documents. Each distortion variant has a stable SERIALIZATION IDENTITY (ADR-0016):
// its arm key below, which a document names and a reader resolves. Renaming one breaks every document
// that holds a model of that variant, so a key is never changed, only added.
namespace lain::camera
{
	inline void serialize(data::Archive&, NoDistortion&)
	{
	}
	LAIN_SERIALIZE(BrownConrady5, k1, k2, p1, p2, k3)
	LAIN_SERIALIZE(InverseBrownConrady5, k1, k2, p1, p2, k3)
	LAIN_SERIALIZE(ModifiedBrownConrady5, k1, k2, p1, p2, k3)
	LAIN_SERIALIZE(RationalBrownConrady8, k1, k2, p1, p2, k3, k4, k5, k6)
	LAIN_SERIALIZE(KannalaBrandt4, k1, k2, k3, k4)

	LAIN_SERIALIZE_VARIANT_ARM(NoDistortion, "none")
	LAIN_SERIALIZE_VARIANT_ARM(BrownConrady5, "brownConrady5")
	LAIN_SERIALIZE_VARIANT_ARM(InverseBrownConrady5, "inverseBrownConrady5")
	LAIN_SERIALIZE_VARIANT_ARM(ModifiedBrownConrady5, "modifiedBrownConrady5")
	LAIN_SERIALIZE_VARIANT_ARM(RationalBrownConrady8, "rationalBrownConrady8")
	LAIN_SERIALIZE_VARIANT_ARM(KannalaBrandt4, "kannalaBrandt4")

	LAIN_SERIALIZE(ImageGeometry, width, height)
	LAIN_SERIALIZE(Intrinsics, fx, fy, cx, cy)
	LAIN_SERIALIZE(CameraModelParameters, image, intrinsics, distortion)

	// The version a camera-model document is written at, and the newest this build reads.
	constexpr std::uint32_t kCameraModelDocumentVersion = 1;

	// A camera model as a document: {version, image, intrinsics, distortion}.
	data::Value cameraModelToValue(const CameraModel& model);

	// The outcome of reading a camera-model document: a model, or every reason there is none.
	struct CameraModelRead
	{
		std::optional<CameraModel> model;
		std::vector<std::string> problems; // empty exactly when `model` is set
	};

	// A camera model from a document, read STRICTLY, where lain::data reads a member best-effort: an
	// unknown or missing key, an unknown distortion variant, or a value of the wrong kind is a problem
	// named by its path, never a silent default. A hand-typed manufacturer model is exactly where a
	// misspelled "k2" would otherwise load as zero. What is read then goes through CameraModel::create,
	// whose diagnostics join the problems.
	CameraModelRead cameraModelFromValue(const data::Value& document);
} // namespace lain::camera
