#include "lain/rigplan/serialize/rigplan.h"

#include <lain/camera/serialize/cameramodel.h> // ImageGeometry and Distortion as the camera-model document has them
#include <lain/data/data.h>
#include <lain/data/strict.h>
#include <lain/data/versioned.h>
#include <lain/media/serialize/manifest.h> // FrameRate as {numerator, denominator}
#include <lain/string/format.h>

#include <cmath>
#include <cstddef>
#include <utility>
#include <variant>

namespace lain::rigplan
{
	// A document's parts with nothing checked, as the document holds them: what is read before it is
	// judged, as the rig document's are. Private: a caller has the rigplan types. A length is a number
	// in the unit its key names, an identity its string, a vector its components.
	struct Vec2Doc
	{
		double x = 0;
		double y = 0;
	};
	LAIN_SERIALIZE(Vec2Doc, x, y)

	struct Vec3Doc
	{
		double x = 0;
		double y = 0;
		double z = 0;
	};
	LAIN_SERIALIZE(Vec3Doc, x, y, z)

	struct ColourDoc
	{
		double r = 1;
		double g = 1;
		double b = 1;
	};
	LAIN_SERIALIZE(ColourDoc, r, g, b)

	struct BodyDoc
	{
		Vec3Doc size;
		Vec3Doc opticalCentre;
	};
	LAIN_SERIALIZE(BodyDoc, size, opticalCentre)

	struct SensorDoc
	{
		std::string name;
		camera::ImageGeometry image;
		double pixelPitchUm = 0;
		double principalPointToleranceUm = 0;
		BodyDoc body;
	};
	LAIN_SERIALIZE(SensorDoc, name, image, pixelPitchUm, principalPointToleranceUm, body)

	struct LensDoc
	{
		std::string name;
		double focalLengthMm = 0;
		camera::Distortion distortion;
		double focalLengthTolerance = 0;
		double distortionTolerance = 0;
	};
	LAIN_SERIALIZE(LensDoc, name, focalLengthMm, distortion, focalLengthTolerance, distortionTolerance)

	LAIN_SERIALIZE(CameraSpecification, name, sensor, lens)

	struct LightDoc
	{
		std::string name;
		LightKind kind = LightKind::Point; // its name: "Point", "Spot"
		double intensity = 1;
		ColourDoc colour;
		double innerConeDegrees = 0;
		double outerConeDegrees = 0;
	};
	LAIN_SERIALIZE(LightDoc, name, kind, intensity, colour, innerConeDegrees, outerConeDegrees)

	struct SpecificationsDoc
	{
		std::vector<SensorDoc> sensors;
		std::vector<LensDoc> lenses;
		std::vector<CameraSpecification> cameras;
		std::vector<LightDoc> lights;
	};
	LAIN_SERIALIZE(SpecificationsDoc, sensors, lenses, cameras, lights)

	struct PoleDoc
	{
		Vec2Doc floor;
		double height = 0;
		double diameter = 0;
	};
	LAIN_SERIALIZE(PoleDoc, floor, height, diameter)
	LAIN_SERIALIZE_VARIANT_ARM(PoleDoc, "pole")

	struct FixedPointDoc
	{
		Vec3Doc position;
	};
	LAIN_SERIALIZE(FixedPointDoc, position)
	LAIN_SERIALIZE_VARIANT_ARM(FixedPointDoc, "fixedPoint")

	struct MountToleranceDoc
	{
		double positionMm = 0;
		double yawDegrees = 0;
	};
	LAIN_SERIALIZE(MountToleranceDoc, positionMm, yawDegrees)

	struct MountDoc
	{
		std::string id;
		std::string name;
		std::variant<PoleDoc, FixedPointDoc> kind;
		double yawDegrees = 0;
		MountToleranceDoc tolerance;
	};
	LAIN_SERIALIZE(MountDoc, id, name, kind, yawDegrees, tolerance)

	LAIN_SERIALIZE(Aim, panDegrees, tiltDegrees, rollDegrees)

	struct PlacementToleranceDoc
	{
		double offsetMm = 0;
		double panDegrees = 0;
		double tiltDegrees = 0;
		double rollDegrees = 0;
	};
	LAIN_SERIALIZE(PlacementToleranceDoc, offsetMm, panDegrees, tiltDegrees, rollDegrees)

	struct CameraDoc
	{
		std::string id;
		std::string name;
		std::string camera;
		std::string mount;
		Vec3Doc offset;
		Aim aim;
		PlacementToleranceDoc tolerance;
	};
	LAIN_SERIALIZE(CameraDoc, id, name, camera, mount, offset, aim, tolerance)

	struct LightPlacementDoc
	{
		std::string id;
		std::string name;
		std::string light;
		std::string mount;
		Vec3Doc offset;
		Aim aim;
	};
	LAIN_SERIALIZE(LightPlacementDoc, id, name, light, mount, offset, aim)

	struct TimelineDoc
	{
		std::uint64_t frames = 0;
		media::FrameRate rate;
	};
	LAIN_SERIALIZE(TimelineDoc, frames, rate)

	LAIN_SERIALIZE(Scales, intrinsic, installation, locked, noise)

	struct RigPlanDoc
	{
		SpecificationsDoc specifications;
		std::vector<MountDoc> mounts;
		std::vector<CameraDoc> cameras;
		std::vector<LightPlacementDoc> lights;
		TimelineDoc timeline;
		std::uint64_t seed = 0;
		Scales scales;
	};
	LAIN_SERIALIZE(RigPlanDoc, specifications, mounts, cameras, lights, timeline, seed, scales)

	// --- to a document ---------------------------------------------------------------------

	static Vec2Doc docOf(const math::Vec2d& v)
	{
		return {v.x, v.y};
	}

	static Vec3Doc docOf(const math::Vec3d& v)
	{
		return {v.x, v.y, v.z};
	}

	static SpecificationsDoc docOf(const Specifications& specifications)
	{
		SpecificationsDoc doc;
		for (const SensorSpecification& s : specifications.sensors)
		{
			doc.sensors.push_back({s.name, s.image, s.pixelPitch.as<core::Length::Micrometres>(), s.principalPointTolerance.as<core::Length::Micrometres>(), {docOf(s.body.size), docOf(s.body.opticalCentre)}});
		}
		for (const LensSpecification& l : specifications.lenses)
		{
			doc.lenses.push_back({l.name, l.focalLength.as<core::Length::Millimetres>(), l.distortion, l.focalLengthTolerance,
								  l.distortionTolerance});
		}
		doc.cameras = specifications.cameras;
		for (const LightSpecification& l : specifications.lights)
		{
			doc.lights.push_back({l.name, l.kind, l.intensity, {l.colour.r, l.colour.g, l.colour.b}, l.innerConeDegrees, l.outerConeDegrees});
		}
		return doc;
	}

	static RigPlanDoc docOf(const RigPlan& plan)
	{
		RigPlanDoc doc;
		doc.specifications = docOf(plan.specifications);
		for (const Mount& m : plan.mounts)
		{
			MountDoc mount{m.id.toString(), m.name, PoleDoc{}, m.yawDegrees, {m.tolerance.position.as<core::Length::Millimetres>(), m.tolerance.yawDegrees}};
			if (const auto* pole = std::get_if<Pole>(&m.kind))
				mount.kind = PoleDoc{docOf(pole->floor), pole->height, pole->diameter};
			else
				mount.kind = FixedPointDoc{docOf(std::get<FixedPoint>(m.kind).position)};
			doc.mounts.push_back(std::move(mount));
		}
		for (const PlacedCamera& c : plan.cameras)
		{
			const PlacementTolerance& t = c.tolerance;
			doc.cameras.push_back({c.id.toString(), c.name, c.camera, c.mount.toString(), docOf(c.offset), c.aim, {t.offset.as<core::Length::Millimetres>(), t.panDegrees, t.tiltDegrees, t.rollDegrees}});
		}
		for (const PlacedLight& l : plan.lights)
			doc.lights.push_back({l.id.toString(), l.name, l.light, l.mount.toString(), docOf(l.offset), l.aim});
		doc.timeline = {plan.timeline.frames, plan.timeline.rate};
		doc.seed = plan.seed;
		doc.scales = plan.scales;
		return doc;
	}

	data::Value rigPlanToValue(const RigPlan& plan)
	{
		return data::versionedDocument(kRigPlanDocumentVersion, data::toValue(docOf(plan)));
	}

	data::Value catalogueToValue(const Specifications& specifications)
	{
		return data::versionedDocument(kCatalogueDocumentVersion, data::toValue(docOf(specifications)));
	}

	// --- from a document -------------------------------------------------------------------

	// What turning a document's parts into rigplan values can find wrong, each named by its path, as a
	// shape problem is.
	struct Conversion
	{
		std::vector<std::string> problems;

		core::Uuid identity(const std::string& text, const std::string& path)
		{
			if (const std::optional<core::Uuid> id = core::Uuid::parse(text))
				return *id;
			problems.push_back(string::format("'{}' is '{}', which is not a uuid", path, text));
			return {};
		}

		// A length in `Units`, checked before core::Length is asked to hold it: Length asserts on a
		// value it cannot represent, and a hand-edited document can say anything.
		template <class Units>
		core::Length length(double value, const std::string& path)
		{
			const double nanometres = value * double(Units::num) * 1e9 / double(Units::den);
			if (!std::isfinite(nanometres) || std::abs(nanometres) > 9.0e18)
			{
				problems.push_back(string::format("'{}' is {}, which is not a length", path, value));
				return {};
			}
			return core::Length::from<Units>(value);
		}
	};

	static math::Vec2d vecOf(const Vec2Doc& d)
	{
		return {d.x, d.y};
	}

	static math::Vec3d vecOf(const Vec3Doc& d)
	{
		return {d.x, d.y, d.z};
	}

	static Specifications specificationsOf(const SpecificationsDoc& doc, const std::string& at, Conversion& convert)
	{
		Specifications out;
		for (std::size_t i = 0; i < doc.sensors.size(); ++i)
		{
			const SensorDoc& s = doc.sensors[i];
			const std::string path = string::format("{}sensors[{}]", at, i);
			SensorSpecification sensor;
			sensor.name = s.name;
			sensor.image = s.image;
			sensor.pixelPitch = convert.length<core::Length::Micrometres>(s.pixelPitchUm, path + ".pixelPitchUm");
			sensor.principalPointTolerance =
				convert.length<core::Length::Micrometres>(s.principalPointToleranceUm, path + ".principalPointToleranceUm");
			sensor.body = {vecOf(s.body.size), vecOf(s.body.opticalCentre)};
			out.sensors.push_back(std::move(sensor));
		}
		for (std::size_t i = 0; i < doc.lenses.size(); ++i)
		{
			const LensDoc& l = doc.lenses[i];
			LensSpecification lens;
			lens.name = l.name;
			lens.focalLength =
				convert.length<core::Length::Millimetres>(l.focalLengthMm, string::format("{}lenses[{}].focalLengthMm", at, i));
			lens.distortion = l.distortion;
			lens.focalLengthTolerance = l.focalLengthTolerance;
			lens.distortionTolerance = l.distortionTolerance;
			out.lenses.push_back(std::move(lens));
		}
		out.cameras = doc.cameras;
		for (const LightDoc& l : doc.lights)
		{
			LightSpecification light;
			light.name = l.name;
			light.kind = l.kind;
			light.intensity = l.intensity;
			light.colour = image::ColorRGBf{float(l.colour.r), float(l.colour.g), float(l.colour.b)};
			light.innerConeDegrees = l.innerConeDegrees;
			light.outerConeDegrees = l.outerConeDegrees;
			out.lights.push_back(std::move(light));
		}
		return out;
	}

	static RigPlan planOf(const RigPlanDoc& doc, Conversion& convert)
	{
		RigPlan plan;
		plan.specifications = specificationsOf(doc.specifications, "specifications.", convert);
		for (std::size_t i = 0; i < doc.mounts.size(); ++i)
		{
			const MountDoc& m = doc.mounts[i];
			const std::string path = string::format("mounts[{}]", i);
			Mount mount;
			mount.id = convert.identity(m.id, path + ".id");
			mount.name = m.name;
			if (const auto* pole = std::get_if<PoleDoc>(&m.kind))
				mount.kind = Pole{vecOf(pole->floor), pole->height, pole->diameter};
			else
				mount.kind = FixedPoint{vecOf(std::get<FixedPointDoc>(m.kind).position)};
			mount.yawDegrees = m.yawDegrees;
			mount.tolerance.position =
				convert.length<core::Length::Millimetres>(m.tolerance.positionMm, path + ".tolerance.positionMm");
			mount.tolerance.yawDegrees = m.tolerance.yawDegrees;
			plan.mounts.push_back(std::move(mount));
		}
		for (std::size_t i = 0; i < doc.cameras.size(); ++i)
		{
			const CameraDoc& c = doc.cameras[i];
			const std::string path = string::format("cameras[{}]", i);
			PlacedCamera camera;
			camera.id = convert.identity(c.id, path + ".id");
			camera.name = c.name;
			camera.camera = c.camera;
			camera.mount = convert.identity(c.mount, path + ".mount");
			camera.offset = vecOf(c.offset);
			camera.aim = c.aim;
			camera.tolerance.offset =
				convert.length<core::Length::Millimetres>(c.tolerance.offsetMm, path + ".tolerance.offsetMm");
			camera.tolerance.panDegrees = c.tolerance.panDegrees;
			camera.tolerance.tiltDegrees = c.tolerance.tiltDegrees;
			camera.tolerance.rollDegrees = c.tolerance.rollDegrees;
			plan.cameras.push_back(std::move(camera));
		}
		for (std::size_t i = 0; i < doc.lights.size(); ++i)
		{
			const LightPlacementDoc& l = doc.lights[i];
			const std::string path = string::format("lights[{}]", i);
			plan.lights.push_back({convert.identity(l.id, path + ".id"), l.name, l.light, convert.identity(l.mount, path + ".mount"),
								   vecOf(l.offset), l.aim});
		}
		plan.timeline = {std::size_t(doc.timeline.frames), doc.timeline.rate};
		plan.seed = doc.seed;
		plan.scales = doc.scales;
		return plan;
	}

	// The read every document here shares: the version, then the parts read tolerantly, then written
	// back and compared with what was stored, so anything the tolerant read passed over is a problem.
	template <typename Doc>
	static std::optional<Doc> readStrictly(const data::Value& document, const char* kind, std::uint32_t newest,
										   std::vector<std::string>& problems)
	{
		problems = data::versionProblems(document, kind, newest);
		if (!problems.empty())
			return std::nullopt;
		std::optional<Doc> parts = data::fromValue<Doc>(document);
		if (!parts)
		{
			problems.push_back(string::format("the document is not a {}", kind));
			return std::nullopt;
		}
		// Not fromValueStrict: the version is a document field outside the parts, so the comparison is
		// against the whole document written back.
		problems = data::shapeDifferences(document, data::versionedDocument(newest, data::toValue(*parts)));
		if (!problems.empty())
			return std::nullopt;
		return parts;
	}

	RigPlanRead rigPlanFromValue(const data::Value& document)
	{
		RigPlanRead result;
		const std::optional<RigPlanDoc> parts =
			readStrictly<RigPlanDoc>(document, "rig-plan", kRigPlanDocumentVersion, result.problems);
		if (!parts)
			return result;
		Conversion convert;
		RigPlan plan = planOf(*parts, convert);
		result.problems = std::move(convert.problems);
		if (result.problems.empty())
			result.plan = std::move(plan);
		return result;
	}

	CatalogueRead catalogueFromValue(const data::Value& document)
	{
		CatalogueRead result;
		const std::optional<SpecificationsDoc> parts =
			readStrictly<SpecificationsDoc>(document, "specification-catalogue", kCatalogueDocumentVersion, result.problems);
		if (!parts)
			return result;
		Conversion convert;
		Specifications specifications = specificationsOf(*parts, "", convert);
		result.problems = std::move(convert.problems);
		if (result.problems.empty())
			result.specifications = std::move(specifications);
		return result;
	}
} // namespace lain::rigplan
