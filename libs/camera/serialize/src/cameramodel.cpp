#include "lain/camera/serialize/cameramodel.h"

#include "versioned.h"

#include <lain/data/strict.h>

#include <utility>

namespace lain::camera
{
	// {version, image, intrinsics, distortion}.
	static data::Value documentOf(const CameraModelParameters& parameters)
	{
		return versionedDocument(kCameraModelDocumentVersion, data::toValue(parameters));
	}

	data::Value cameraModelToValue(const CameraModel& model)
	{
		return documentOf(model.parameters());
	}

	CameraModelRead cameraModelFromValue(const data::Value& document)
	{
		CameraModelRead result;
		result.problems = versionProblems(document, "camera-model", kCameraModelDocumentVersion);
		if (!result.problems.empty())
			return result;

		const std::optional<CameraModelParameters> parameters = data::fromValue<CameraModelParameters>(document);
		if (!parameters)
		{
			result.problems.push_back("the document is not a camera model");
			return result;
		}
		// Not fromValueStrict: the version is a document field outside the parameters, so the
		// comparison is against the whole document written back.
		result.problems = data::shapeDifferences(document, documentOf(*parameters));
		if (!result.problems.empty())
			return result;

		ModelResult created = CameraModel::create(*parameters);
		for (const ModelDiagnostic& diagnostic : created.diagnostics)
			result.problems.push_back(diagnostic.detail);
		result.model = std::move(created.model);
		return result;
	}
} // namespace lain::camera
