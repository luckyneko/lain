#include "lain/rigplan/specification.h"

#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace lain::rigplan
{
	template <typename Specification>
	static const Specification* findNamed(const std::vector<Specification>& list, std::string_view name)
	{
		const auto it = std::find_if(list.begin(), list.end(), [&](const Specification& s)
									 { return s.name == name; });
		return it == list.end() ? nullptr : &*it;
	}

	const SensorSpecification* findSensor(const Specifications& specifications, std::string_view name)
	{
		return findNamed(specifications.sensors, name);
	}

	const LensSpecification* findLens(const Specifications& specifications, std::string_view name)
	{
		return findNamed(specifications.lenses, name);
	}

	const CameraSpecification* findCamera(const Specifications& specifications, std::string_view name)
	{
		return findNamed(specifications.cameras, name);
	}

	const LightSpecification* findLight(const Specifications& specifications, std::string_view name)
	{
		return findNamed(specifications.lights, name);
	}

	camera::ModelResult nominalModel(const SensorSpecification& sensor, const LensSpecification& lens)
	{
		camera::CameraModelParameters parameters;
		parameters.image = sensor.image;
		// A ratio of two exact counts of nanometres, so a 4 mm lens on a 2 um sensor is exactly 2000.
		const double focal = double(lens.focalLength.nanometres()) / double(sensor.pixelPitch.nanometres());
		parameters.intrinsics.fx = focal;
		parameters.intrinsics.fy = focal;
		parameters.intrinsics.cx = (double(sensor.image.width) - 1.0) / 2.0;
		parameters.intrinsics.cy = (double(sensor.image.height) - 1.0) / 2.0;
		parameters.distortion = lens.distortion;
		return camera::CameraModel::create(parameters);
	}

	static bool validBound(double tolerance)
	{
		return std::isfinite(tolerance) && tolerance >= 0;
	}

	std::vector<std::string> sensorModelProblems(const SensorModel& model)
	{
		std::vector<std::string> out;
		if (!std::isfinite(model.gain) || model.gain <= 0)
			out.push_back(string::format("a gain of {}, which must be positive", model.gain));
		if (!std::isfinite(model.fullWellElectrons) || model.fullWellElectrons <= 0)
			out.push_back(string::format("a full well of {} electrons, which must be positive", model.fullWellElectrons));
		if (!validBound(model.readNoiseElectrons))
			out.push_back(string::format("a read noise of {} electrons, which must be at least 0", model.readNoiseElectrons));
		if (model.adcBits < 1 || model.adcBits > 16)
			out.push_back(string::format("{} ADC bits, which must be 1 to 16", model.adcBits));
		const SensorOutput& output = model.output;
		if (output.bits != 8 && output.bits != 16)
			out.push_back(string::format("an output of {} bits, which must be 8 or 16", output.bits));
		else if (output.format == OutputFormat::Jpeg && output.bits != 8)
			out.push_back(string::format("a JPEG output of {} bits, where JPEG holds 8", output.bits));
		if (output.curve != image::ColorSpace::Linear && output.curve != image::ColorSpace::sRGB &&
			output.curve != image::ColorSpace::BT709)
		{
			out.push_back("an output curve that is not Linear, sRGB or BT709");
		}
		if (output.quality)
		{
			if (output.format != OutputFormat::Jpeg)
				out.push_back("a quality for a lossless format, which has none to set");
			else if (*output.quality < 1 || *output.quality > 100)
				out.push_back(string::format("a quality of {}, which must be 1 to 100", *output.quality));
		}
		return out;
	}

	// The checks every kind of specification shares: a name, and one that no earlier entry of the same
	// kind took. Returns whether this entry passed them.
	template <typename Specification>
	static bool checkName(const Specification& s, std::string_view kind, std::set<std::string>& seen,
						  std::vector<SpecificationDiagnostic>& out)
	{
		if (s.name.empty())
		{
			out.push_back({SpecificationProblem::Unnamed, string::format("a {} has no name", kind)});
			return false;
		}
		if (!seen.insert(s.name).second)
		{
			out.push_back({SpecificationProblem::DuplicateName, string::format("two {}s are named '{}'", kind, s.name)});
			return false;
		}
		return true;
	}

	std::vector<SpecificationDiagnostic> check(const Specifications& specifications)
	{
		std::vector<SpecificationDiagnostic> out;
		// The entries a camera may be built from: those that passed their own checks. A camera on a
		// broken part is reported for naming it, not again for the model it cannot make.
		std::set<const SensorSpecification*> usableSensors;
		std::set<const LensSpecification*> usableLenses;

		std::set<std::string> seen;
		for (const SensorSpecification& s : specifications.sensors)
		{
			bool usable = checkName(s, "sensor", seen, out);
			if (s.image.width == 0 || s.image.height == 0)
			{
				out.push_back({SpecificationProblem::EmptyImage,
							   string::format("sensor '{}' has a {} x {} image", s.name, s.image.width, s.image.height)});
				usable = false;
			}
			if (s.pixelPitch.nanometres() <= 0)
			{
				out.push_back({SpecificationProblem::NonPositivePixelPitch,
							   string::format("sensor '{}' has a pixel pitch of {} nm", s.name, s.pixelPitch.nanometres())});
				usable = false;
			}
			if (s.principalPointTolerance.nanometres() < 0)
			{
				out.push_back({SpecificationProblem::InvalidTolerance,
							   string::format("sensor '{}' has a negative principal-point tolerance", s.name)});
				usable = false;
			}
			for (const std::string& problem : sensorModelProblems(s.sensorModel))
			{
				out.push_back({SpecificationProblem::InvalidSensorModel, string::format("sensor '{}': {}", s.name, problem)});
				usable = false;
			}
			if (usable)
				usableSensors.insert(&s);
		}

		seen.clear();
		for (const LensSpecification& l : specifications.lenses)
		{
			bool usable = checkName(l, "lens", seen, out);
			if (l.focalLength.nanometres() <= 0)
			{
				out.push_back({SpecificationProblem::NonPositiveFocalLength,
							   string::format("lens '{}' has a focal length of {} nm", l.name, l.focalLength.nanometres())});
				usable = false;
			}
			if (!validBound(l.focalLengthTolerance) || l.focalLengthTolerance >= 1)
			{
				out.push_back({SpecificationProblem::InvalidTolerance,
							   string::format("lens '{}' has a focal-length tolerance of {}, which must be at least 0 and below 1",
											  l.name, l.focalLengthTolerance)});
				usable = false;
			}
			if (!validBound(l.distortionTolerance))
			{
				out.push_back({SpecificationProblem::InvalidTolerance,
							   string::format("lens '{}' has a distortion tolerance of {}, which must be at least 0", l.name,
											  l.distortionTolerance)});
				usable = false;
			}
			if (usable)
				usableLenses.insert(&l);
		}

		seen.clear();
		for (const CameraSpecification& c : specifications.cameras)
		{
			checkName(c, "camera", seen, out);
			const SensorSpecification* sensor = findSensor(specifications, c.sensor);
			const LensSpecification* lens = findLens(specifications, c.lens);
			if (!sensor)
			{
				out.push_back({SpecificationProblem::UnknownSensor,
							   string::format("camera '{}' names a sensor '{}' that is not specified", c.name, c.sensor)});
			}
			if (!lens)
			{
				out.push_back({SpecificationProblem::UnknownLens,
							   string::format("camera '{}' names a lens '{}' that is not specified", c.name, c.lens)});
			}
			if (!sensor || !lens || !usableSensors.count(sensor) || !usableLenses.count(lens))
				continue;

			const camera::ModelResult nominal = nominalModel(*sensor, *lens);
			if (!nominal.model)
			{
				std::string why;
				for (const camera::ModelDiagnostic& d : nominal.diagnostics)
					why += (why.empty() ? "" : "; ") + d.detail;
				out.push_back({SpecificationProblem::InvalidNominalModel,
							   string::format("camera '{}' (sensor '{}', lens '{}') makes no camera model: {}", c.name,
											  c.sensor, c.lens, why)});
			}
		}
		seen.clear();
		for (const LightSpecification& l : specifications.lights)
		{
			checkName(l, "light", seen, out);
			const bool colour = validBound(l.colour.r) && validBound(l.colour.g) && validBound(l.colour.b);
			if (!validBound(l.intensity) || !colour)
			{
				out.push_back({SpecificationProblem::InvalidLight,
							   string::format("light '{}' needs a finite, non-negative intensity and colour", l.name)});
			}
			const bool cone = std::isfinite(l.innerConeDegrees) && std::isfinite(l.outerConeDegrees) &&
							  l.innerConeDegrees >= 0 && l.innerConeDegrees <= l.outerConeDegrees && l.outerConeDegrees > 0 &&
							  l.outerConeDegrees < 180;
			if (l.kind == LightKind::Spot && !cone)
			{
				out.push_back({SpecificationProblem::InvalidLight,
							   string::format("spot light '{}' has a cone of {} to {} degrees, which must rise from 0 to below 180",
											  l.name, l.innerConeDegrees, l.outerConeDegrees)});
			}
		}
		return out;
	}
} // namespace lain::rigplan
