#include "lain/rigplan/serialize/rigplan.h"

#include "strict.h"

#include <lain/camera/serialize/board.h>	   // a board as a board specification document spells it
#include <lain/camera/serialize/cameramodel.h> // ImageGeometry and Distortion as the camera-model document has them
#include <lain/data/data.h>
#include <lain/data/strict.h>
#include <lain/data/versioned.h>
#include <lain/io/documentpath.h>		   // the document-path rule for one path (ADR-0027)
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

	// A sensor model's numbers need no conversion (each key names its unit, or has none), so the types
	// are their own parts. Enums by name: "Png", "Grey", "sRGB".
	LAIN_SERIALIZE(SensorOutput, format, bits, curve, quality)
	LAIN_SERIALIZE(SensorModel, gain, fullWellElectrons, readNoiseElectrons, adcBits, channels, output)

	struct SensorDoc
	{
		std::string name;
		camera::ImageGeometry image;
		double pixelPitchUm = 0;
		double principalPointToleranceUm = 0;
		BodyDoc body;
		SensorModel sensorModel;
	};
	LAIN_SERIALIZE(SensorDoc, name, image, pixelPitchUm, principalPointToleranceUm, body, sensorModel)

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

	struct SolidColourDoc
	{
		ColourDoc colour;
	};
	LAIN_SERIALIZE(SolidColourDoc, colour)
	LAIN_SERIALIZE_VARIANT_ARM(SolidColourDoc, "colour")

	struct ImageTextureDoc
	{
		std::string image; // a path, which relativizePaths / resolvePaths rebase at the file boundary
		Vec2Doc tile;
	};
	LAIN_SERIALIZE(ImageTextureDoc, image, tile)
	LAIN_SERIALIZE_VARIANT_ARM(ImageTextureDoc, "image")

	struct ProceduralDoc
	{
		std::uint64_t seed = 0;
		double cell = 0; // metres, as every position is
	};
	LAIN_SERIALIZE(ProceduralDoc, seed, cell)
	LAIN_SERIALIZE_VARIANT_ARM(ProceduralDoc, "procedural")

	struct MaterialDoc
	{
		std::variant<SolidColourDoc, ImageTextureDoc, ProceduralDoc> surface;
		ColourDoc specular;
		double shininess = 0;
	};
	LAIN_SERIALIZE(MaterialDoc, surface, specular, shininess)

	struct RoomSurfaceDoc
	{
		bool present = true;
		MaterialDoc material;
	};
	LAIN_SERIALIZE(RoomSurfaceDoc, present, material)

	struct RoomDoc
	{
		Vec2Doc centre;
		Vec3Doc size;
		RoomSurfaceDoc floor;
		RoomSurfaceDoc ceiling;
		RoomSurfaceDoc north;
		RoomSurfaceDoc south;
		RoomSurfaceDoc east;
		RoomSurfaceDoc west;
	};
	LAIN_SERIALIZE(RoomDoc, centre, size, floor, ceiling, north, south, east, west)

	struct PlaneDoc
	{
		std::string id;
		std::string name;
		Vec3Doc centre;
		Aim aim;
		Vec2Doc size;
		MaterialDoc material;
	};
	LAIN_SERIALIZE(PlaneDoc, id, name, centre, aim, size, material)

	struct BoxDoc
	{
		std::string id;
		std::string name;
		Vec3Doc centre;
		Vec3Doc size;
		double yawDegrees = 0;
		MaterialDoc material;
	};
	LAIN_SERIALIZE(BoxDoc, id, name, centre, size, yawDegrees, material)

	struct PathKeyDoc
	{
		std::uint64_t frame = 0;
		Vec3Doc position;
		Aim aim;
	};
	LAIN_SERIALIZE(PathKeyDoc, frame, position, aim)

	struct PathDoc
	{
		std::vector<PathKeyDoc> keys;
	};
	LAIN_SERIALIZE(PathDoc, keys)

	struct PropDoc
	{
		std::string id;
		std::string name;
		std::string mesh; // a path, rebased at the file boundary
		bool clip = false;
		media::FrameRate clipRate;
		UpAxis up = UpAxis::Y; // its name: "Y", "Z"
		double unitMetres = 1;
		Vec3Doc position;
		double yawDegrees = 0;
		std::optional<PathDoc> path; // absent for a prop that stands still
	};
	LAIN_SERIALIZE(PropDoc, id, name, mesh, clip, clipRate, up, unitMetres, position, yawDegrees, path)

	struct BoardDoc
	{
		std::string id;
		std::string name;
		camera::board::PatternParameters pattern;
		camera::board::Instance instance;
		double margin = 0;
		std::uint32_t pixelsPerSquare = 0;
		double backing = 0;
		PathDoc path;
	};
	LAIN_SERIALIZE(BoardDoc, id, name, pattern, instance, margin, pixelsPerSquare, backing, path)

	struct RigPlanDoc
	{
		SpecificationsDoc specifications;
		std::optional<RoomDoc> room; // absent when the rig plan stands in no room
		std::vector<PlaneDoc> planes;
		std::vector<BoxDoc> boxes;
		std::vector<PropDoc> props;
		std::vector<BoardDoc> boards;
		std::vector<MountDoc> mounts;
		std::vector<CameraDoc> cameras;
		std::optional<std::string> reference; // absent when the first camera is the reference
		std::vector<LightPlacementDoc> lights;
		TimelineDoc timeline;
		std::uint64_t seed = 0;
		Scales scales;
	};
	LAIN_SERIALIZE(RigPlanDoc, specifications, room, planes, boxes, props, boards, mounts, cameras, reference, lights, timeline, seed, scales)

	// --- to a document ---------------------------------------------------------------------

	static Vec2Doc docOf(const math::Vec2d& v)
	{
		return {v.x, v.y};
	}

	static Vec3Doc docOf(const math::Vec3d& v)
	{
		return {v.x, v.y, v.z};
	}

	static PathDoc docOf(const Path& path)
	{
		PathDoc doc;
		for (const PathKey& key : path.keys)
			doc.keys.push_back({std::uint64_t(key.frame), docOf(key.position), key.aim});
		return doc;
	}

	static SpecificationsDoc docOf(const Specifications& specifications)
	{
		SpecificationsDoc doc;
		for (const SensorSpecification& s : specifications.sensors)
		{
			doc.sensors.push_back({s.name, s.image, s.pixelPitch.as<core::Length::Micrometres>(), s.principalPointTolerance.as<core::Length::Micrometres>(), {docOf(s.body.size), docOf(s.body.opticalCentre)}, s.sensorModel});
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

	static ColourDoc docOf(const image::ColorRGBf& c)
	{
		return {c.r, c.g, c.b};
	}

	static MaterialDoc docOf(const Material& m)
	{
		MaterialDoc doc;
		if (const auto* colour = std::get_if<SolidColour>(&m.surface))
			doc.surface = SolidColourDoc{docOf(colour->colour)};
		else if (const auto* picture = std::get_if<ImageTexture>(&m.surface))
			doc.surface = ImageTextureDoc{picture->image.toString(), docOf(picture->tile)};
		else
			doc.surface = ProceduralDoc{std::get<image::ProceduralTexture>(m.surface).seed, std::get<image::ProceduralTexture>(m.surface).cell};
		doc.specular = docOf(m.specular);
		doc.shininess = m.shininess;
		return doc;
	}

	static RoomSurfaceDoc docOf(const RoomSurface& s)
	{
		return {s.present, docOf(s.material)};
	}

	static RigPlanDoc docOf(const RigPlan& plan)
	{
		RigPlanDoc doc;
		doc.specifications = docOf(plan.specifications);
		if (plan.room)
		{
			const Room& r = *plan.room;
			doc.room = RoomDoc{docOf(r.centre), docOf(r.size), docOf(r.floor), docOf(r.ceiling), docOf(r.north),
							   docOf(r.south), docOf(r.east), docOf(r.west)};
		}
		for (const Plane& p : plan.planes)
			doc.planes.push_back({p.id.toString(), p.name, docOf(p.centre), p.aim, docOf(p.size), docOf(p.material)});
		for (const Box& b : plan.boxes)
			doc.boxes.push_back({b.id.toString(), b.name, docOf(b.centre), docOf(b.size), b.yawDegrees, docOf(b.material)});
		for (const Prop& p : plan.props)
		{
			std::optional<PathDoc> path;
			if (p.path)
				path = docOf(*p.path);
			doc.props.push_back({p.id.toString(), p.name, p.mesh.toString(), p.clip, p.clipRate, p.up, p.unitMetres,
								 docOf(p.position), p.yawDegrees, std::move(path)});
		}
		for (const BoardMover& b : plan.boards)
		{
			doc.boards.push_back({b.id.toString(), b.name, b.pattern, b.instance, b.margin, b.pixelsPerSquare, b.backing, docOf(b.path)});
		}
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
		if (plan.reference)
			doc.reference = plan.reference->toString();
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

	static Path pathOf(const PathDoc& doc)
	{
		Path path;
		for (const PathKeyDoc& key : doc.keys)
			path.keys.push_back({std::size_t(key.frame), vecOf(key.position), key.aim});
		return path;
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
			sensor.sensorModel = s.sensorModel;
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

	static image::ColorRGBf colourOf(const ColourDoc& d)
	{
		return image::ColorRGBf{float(d.r), float(d.g), float(d.b)};
	}

	static Material materialOf(const MaterialDoc& doc)
	{
		Material m;
		if (const auto* colour = std::get_if<SolidColourDoc>(&doc.surface))
			m.surface = SolidColour{colourOf(colour->colour)};
		else if (const auto* picture = std::get_if<ImageTextureDoc>(&doc.surface))
			m.surface = ImageTexture{core::Uri{picture->image}, vecOf(picture->tile)};
		else
			m.surface = image::ProceduralTexture{std::get<ProceduralDoc>(doc.surface).seed, std::get<ProceduralDoc>(doc.surface).cell};
		m.specular = colourOf(doc.specular);
		m.shininess = doc.shininess;
		return m;
	}

	static RoomSurface surfaceOf(const RoomSurfaceDoc& doc)
	{
		return {doc.present, materialOf(doc.material)};
	}

	static RigPlan planOf(const RigPlanDoc& doc, Conversion& convert)
	{
		RigPlan plan;
		plan.specifications = specificationsOf(doc.specifications, "specifications.", convert);
		if (doc.room)
		{
			const RoomDoc& r = *doc.room;
			plan.room = Room{vecOf(r.centre), vecOf(r.size), surfaceOf(r.floor), surfaceOf(r.ceiling),
							 surfaceOf(r.north), surfaceOf(r.south), surfaceOf(r.east), surfaceOf(r.west)};
		}
		for (std::size_t i = 0; i < doc.planes.size(); ++i)
		{
			const PlaneDoc& p = doc.planes[i];
			plan.planes.push_back({convert.identity(p.id, string::format("planes[{}].id", i)), p.name, vecOf(p.centre), p.aim,
								   vecOf(p.size), materialOf(p.material)});
		}
		for (std::size_t i = 0; i < doc.boxes.size(); ++i)
		{
			const BoxDoc& b = doc.boxes[i];
			plan.boxes.push_back({convert.identity(b.id, string::format("boxes[{}].id", i)), b.name, vecOf(b.centre), vecOf(b.size),
								  b.yawDegrees, materialOf(b.material)});
		}
		for (std::size_t i = 0; i < doc.props.size(); ++i)
		{
			const PropDoc& p = doc.props[i];
			std::optional<Path> path;
			if (p.path)
				path = pathOf(*p.path);
			plan.props.push_back({convert.identity(p.id, string::format("props[{}].id", i)), p.name, core::Uri{p.mesh}, p.clip,
								  p.clipRate, p.up, p.unitMetres, vecOf(p.position), p.yawDegrees, std::move(path)});
		}
		for (std::size_t i = 0; i < doc.boards.size(); ++i)
		{
			const BoardDoc& b = doc.boards[i];
			plan.boards.push_back({convert.identity(b.id, string::format("boards[{}].id", i)), b.name, b.pattern, b.instance, b.margin,
								   b.pixelsPerSquare, b.backing, pathOf(b.path)});
		}
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
		if (doc.reference)
			plan.reference = convert.identity(*doc.reference, "reference");
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

	RigPlanRead rigPlanFromValue(const data::Value& document)
	{
		RigPlanRead result;
		const std::optional<RigPlanDoc> parts =
			detail::readStrictly<RigPlanDoc>(document, "rig-plan", kRigPlanDocumentVersion, result.problems);
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
			detail::readStrictly<SpecificationsDoc>(document, "specification-catalogue", kCatalogueDocumentVersion, result.problems);
		if (!parts)
			return result;
		Conversion convert;
		Specifications specifications = specificationsOf(*parts, "", convert);
		result.problems = std::move(convert.problems);
		if (result.problems.empty())
			result.specifications = std::move(specifications);
		return result;
	}
	// --- paths at the file boundary (ADR-0027) ---------------------------------------------

	// A member of an object Value, mutably. data::Value::find is const-only.
	static data::Value* member(data::Value& object, std::string_view key)
	{
		data::Value::Object* entries = object.asObject();
		if (!entries)
			return nullptr;
		for (auto& [name, value] : *entries)
		{
			if (name == key)
				return &value;
		}
		return nullptr;
	}

	// Rewrite, through `rewrite`, every path a rig plan document holds: an image material's picture,
	// wherever a material is (the room's surfaces, every plane and box), and every prop's mesh. A
	// document that is not one is left as it is: reading it is what reports that.
	template <typename Rewrite>
	static void rewritePaths(data::Value& document, const Rewrite& rewrite)
	{
		const auto rewriteString = [&](data::Value* value)
		{
			if (value && value->asString())
				*value = data::Value(rewrite(*value->asString()));
		};
		const auto rewriteMaterial = [&](data::Value* material)
		{
			data::Value* surface = material ? member(*material, "surface") : nullptr;
			const data::Value* type = surface ? surface->find("type") : nullptr;
			data::Value* value = surface ? member(*surface, "value") : nullptr;
			if (type && type->asString() && *type->asString() == "image" && value)
				rewriteString(member(*value, "image"));
		};
		if (data::Value* room = member(document, "room"))
		{
			for (const char* side : {"floor", "ceiling", "north", "south", "east", "west"})
			{
				if (data::Value* surface = member(*room, side))
					rewriteMaterial(member(*surface, "material"));
			}
		}
		for (const char* list : {"planes", "boxes"})
		{
			data::Value* items = member(document, list);
			if (!items || !items->asArray())
				continue;
			for (data::Value& item : *items->asArray())
				rewriteMaterial(member(item, "material"));
		}
		if (data::Value* props = member(document, "props"); props && props->asArray())
		{
			for (data::Value& prop : *props->asArray())
				rewriteString(member(prop, "mesh"));
		}
	}

	void relativizePaths(data::Value& document, const std::filesystem::path& folder)
	{
		rewritePaths(document, [&folder](const std::string& text)
					 { return io::relativizePath(text, folder); });
	}

	void resolvePaths(data::Value& document, const std::filesystem::path& folder)
	{
		rewritePaths(document, [&folder](const std::string& text)
					 { return io::resolvePath(text, folder); });
	}
} // namespace lain::rigplan
