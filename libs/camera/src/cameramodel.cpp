#include "lain/camera/cameramodel.h"

#include "lain/camera/projection.h"

#include <lain/string/format.h>

#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace lain::camera
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	static std::string describe(double value)
	{
		return std::to_string(value);
	}

	// Every coefficient of a distortion model, by name, in the model's order: what the finiteness
	// check names and what toString shows.
	static std::vector<std::pair<std::string_view, double>> namedCoefficients(const Distortion& distortion)
	{
		const std::vector<std::string_view> names = coefficientNames(modelOf(distortion));
		const std::vector<double> values = coefficients(distortion);
		std::vector<std::pair<std::string_view, double>> named;
		for (std::size_t i = 0; i < names.size(); ++i)
			named.emplace_back(names[i], values[i]);
		return named;
	}

	// --- CameraModel ------------------------------------------------------------

	ModelResult CameraModel::create(const CameraModelParameters& parameters)
	{
		ModelResult result;
		const auto problem = [&result](ModelProblem kind, std::string detail)
		{ result.diagnostics.push_back({kind, std::move(detail)}); };

		const ImageGeometry& image = parameters.image;
		const Intrinsics& k = parameters.intrinsics;
		if (image.width == 0 || image.height == 0)
			problem(ModelProblem::EmptyImage,
					"image is " + std::to_string(image.width) + "x" + std::to_string(image.height));

		const std::pair<const char*, double> pinhole[] = {{"fx", k.fx}, {"fy", k.fy}, {"cx", k.cx}, {"cy", k.cy}};
		for (const auto& [name, value] : pinhole)
		{
			if (!std::isfinite(value))
				problem(ModelProblem::NonFiniteParameter, std::string(name) + " is " + describe(value));
		}
		for (const auto& [name, value] : namedCoefficients(parameters.distortion))
		{
			if (!std::isfinite(value))
				problem(ModelProblem::NonFiniteParameter, std::string(name) + " is " + describe(value));
		}
		for (const auto& [name, value] : {std::pair<const char*, double>{"fx", k.fx}, {"fy", k.fy}})
		{
			if (std::isfinite(value) && value <= 0)
				problem(ModelProblem::NonPositiveFocalLength, std::string(name) + " is " + describe(value));
		}
		// The fold test needs finite numbers and an image to have corners, so it runs only on
		// parameters that passed everything above.
		if (!result.diagnostics.empty())
			return result;

		CameraModel model{parameters, std::visit([](const auto& d)
												 { return d.domain(); }, parameters.distortion)};

		// Every corner of the image must have a ray: a distortion that folds inside the image leaves
		// part of it with none, or with two, and no downstream use of the model survives that.
		const double right = double(image.width) - 0.5;
		const double bottom = double(image.height) - 0.5;
		const std::pair<double, double> corners[] = {{-0.5, -0.5}, {right, -0.5}, {-0.5, bottom}, {right, bottom}};
		for (const auto& [u, v] : corners)
		{
			if (!unproject(model, u, v).ok())
				problem(ModelProblem::FoldsInsideImage,
						"the distortion has no ray at image corner (" + describe(u) + ", " + describe(v) + ")");
		}
		if (!result.diagnostics.empty())
			return result;

		result.model = std::move(model);
		return result;
	}

	std::string CameraModel::toString() const
	{
		const ImageGeometry& image = m_parameters.image;
		const Intrinsics& k = m_parameters.intrinsics;
		std::string text = lain::string::format("{}, {}x{}, fx {:.6g} fy {:.6g}, cx {:.6g} cy {:.6g}",
												displayName(m_parameters.distortion), image.width, image.height, k.fx,
												k.fy, k.cx, k.cy);
		const std::vector<std::pair<std::string_view, double>> named = namedCoefficients(m_parameters.distortion);
		for (std::size_t i = 0; i < named.size(); ++i)
			text += lain::string::format("{}{} {:.6g}", i == 0 ? ", " : " ", named[i].first, named[i].second);
		return text;
	}

	// --- free functions ---------------------------------------------------------

	Applicability applicability(const CameraModel& model, const ImageGeometry& footage)
	{
		return model.image() == footage ? Applicability::Unknown : Applicability::Incompatible;
	}

	bool contains(const CameraModel& model, double u, double v)
	{
		const ImageGeometry& image = model.image();
		return u >= -0.5 && v >= -0.5 && u < double(image.width) - 0.5 && v < double(image.height) - 0.5;
	}
} // namespace lain::camera
