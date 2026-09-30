#include "lain/camera/serialize/cameramodel.h"

#include "strict.h"

#include <lain/string/format.h>

#include <utility>

namespace lain::camera
{
	// {version, image, intrinsics, distortion}: the version first, so a reader meets it before anything
	// it might not understand.
	static data::Value documentOf(const CameraModelParameters& parameters)
	{
		data::Value document = data::Value::object();
		document.set("version", data::Value(std::uint64_t{kCameraModelDocumentVersion}));
		data::Value body = data::toValue(parameters);
		for (auto& [key, value] : *body.asObject())
			document.set(key, std::move(value));
		return document;
	}

	data::Value cameraModelToValue(const CameraModel& model)
	{
		return documentOf(model.parameters());
	}

	CameraModelRead cameraModelFromValue(const data::Value& document)
	{
		CameraModelRead result;
		if (!document.isObject())
		{
			result.problems.push_back("a camera-model document is an object");
			return result;
		}
		const data::Value* version = document.find("version");
		const std::optional<std::uint64_t> number = version ? version->asUInt64() : std::nullopt;
		if (!number)
		{
			result.problems.push_back("the document has no version");
			return result;
		}
		if (*number > kCameraModelDocumentVersion)
		{
			result.problems.push_back(lain::string::format(
				"the document is version {}, newer than this build reads ({})", *number, kCameraModelDocumentVersion));
			return result;
		}

		const std::optional<CameraModelParameters> parameters = data::fromValue<CameraModelParameters>(document);
		if (!parameters)
		{
			result.problems.push_back("the document is not a camera model");
			return result;
		}
		result.problems = detail::shapeDifferences(document, documentOf(*parameters));
		if (!result.problems.empty())
			return result;

		ModelResult created = CameraModel::create(*parameters);
		for (const ModelDiagnostic& diagnostic : created.diagnostics)
			result.problems.push_back(diagnostic.detail);
		result.model = std::move(created.model);
		return result;
	}
} // namespace lain::camera
