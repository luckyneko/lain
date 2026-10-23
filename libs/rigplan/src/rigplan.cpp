#include "lain/rigplan/rigplan.h"

#include <lain/string/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <set>
#include <string>
#include <unordered_map>

namespace lain::rigplan
{
	static bool finite(const math::Vec2d& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y);
	}

	static bool finite(const math::Vec3d& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	static bool finite(const Aim& aim)
	{
		return std::isfinite(aim.panDegrees) && std::isfinite(aim.tiltDegrees) && std::isfinite(aim.rollDegrees);
	}

	static bool positive(const math::Vec2d& v)
	{
		return finite(v) && v.x > 0 && v.y > 0;
	}

	static bool positive(const math::Vec3d& v)
	{
		return finite(v) && v.x > 0 && v.y > 0 && v.z > 0;
	}

	static bool validBound(double tolerance)
	{
		return std::isfinite(tolerance) && tolerance >= 0;
	}

	static bool validScale(double scale)
	{
		return std::isfinite(scale) && scale >= 0 && scale <= 1;
	}

	// ASCII lower case: what a case-insensitive file system folds, for every name a camera is likely
	// to have. A non-ASCII byte is compared as it is.
	static std::string folded(std::string_view name)
	{
		std::string out(name);
		for (char& c : out)
			c = char(std::tolower(static_cast<unsigned char>(c)));
		return out;
	}

	bool validFileName(std::string_view name)
	{
		if (name.empty() || name == "." || name == "..")
			return false;
		for (const char c : name)
		{
			const auto u = static_cast<unsigned char>(c);
			if (u < 0x20 || u == 0x7f || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
				return false;
		}
		if (name.back() == ' ' || name.back() == '.')
			return false; // Windows drops them, so two names would be one folder
		// Windows reserves these names whatever follows a dot: "con.json" is the console too.
		const std::string base = folded(name.substr(0, name.find('.')));
		static const std::array<std::string_view, 4> devices = {"con", "prn", "aux", "nul"};
		if (std::find(devices.begin(), devices.end(), base) != devices.end())
			return false;
		const bool numbered = base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0);
		return !(numbered && base[3] >= '1' && base[3] <= '9');
	}

	std::vector<PlanDiagnostic> check(const RigPlan& plan)
	{
		std::vector<PlanDiagnostic> out;
		for (const SpecificationDiagnostic& d : check(plan.specifications))
			out.push_back({PlanProblem::InvalidSpecification, d.detail});

		// Every identity, across mounts, cameras and lights: a draw is keyed by its owner's, so two
		// owners sharing one would draw alike.
		std::set<core::Uuid> identities;
		const auto identify = [&](const core::Uuid& id, std::string_view kind, std::string_view name)
		{
			if (id.isNil())
				out.push_back({PlanProblem::MissingIdentity, string::format("{} '{}' has no identity", kind, name)});
			else if (!identities.insert(id).second)
				out.push_back({PlanProblem::DuplicateIdentity, string::format("{} '{}' shares the identity {}", kind, name, id.toString())});
		};

		const auto environment = [&](std::string detail)
		{
			out.push_back({PlanProblem::InvalidEnvironment, std::move(detail)});
		};
		const auto checkMaterial = [&](const Material& material, std::string_view where)
		{
			bool valid = validBound(material.specular.r) && validBound(material.specular.g) && validBound(material.specular.b) &&
						 validBound(material.shininess);
			if (const auto* colour = std::get_if<SolidColour>(&material.surface))
			{
				valid = valid && validBound(colour->colour.r) && validBound(colour->colour.g) && validBound(colour->colour.b);
			}
			else if (const auto* picture = std::get_if<ImageTexture>(&material.surface))
			{
				valid = valid && !picture->image.empty() && positive(picture->tile);
			}
			else
			{
				const double cell = std::get<image::ProceduralTexture>(material.surface).cell;
				valid = valid && std::isfinite(cell) && cell > 0;
			}
			if (!valid)
			{
				environment(string::format("{} has a material that cannot be one: a colour or specular must be finite and not "
										   "negative, a picture needs a file and a positive tile, a texture a positive cell",
										   where));
			}
		};

		if (plan.room)
		{
			const Room& room = *plan.room;
			if (!finite(room.centre) || !positive(room.size))
				environment("the room needs a finite centre and a positive size");
			for (const auto& [surface, name] : {std::pair<const RoomSurface*, const char*>{&room.floor, "floor"},
												{&room.ceiling, "ceiling"},
												{&room.north, "north wall"},
												{&room.south, "south wall"},
												{&room.east, "east wall"},
												{&room.west, "west wall"}})
			{
				if (surface->present)
					checkMaterial(surface->material, string::format("the room's {}", name));
			}
		}
		for (const Plane& p : plan.planes)
		{
			identify(p.id, "plane", p.name);
			if (!finite(p.centre) || !finite(p.aim) || !positive(p.size))
				environment(string::format("plane '{}' needs a finite centre and aim and a positive size", p.name));
			checkMaterial(p.material, string::format("plane '{}'", p.name));
		}
		for (const Box& b : plan.boxes)
		{
			identify(b.id, "box", b.name);
			if (!finite(b.centre) || !std::isfinite(b.yawDegrees) || !positive(b.size))
				environment(string::format("box '{}' needs a finite centre and yaw and a positive size", b.name));
			checkMaterial(b.material, string::format("box '{}'", b.name));
		}
		for (const Prop& p : plan.props)
		{
			identify(p.id, "prop", p.name);
			if (p.mesh.empty() || !std::isfinite(p.unitMetres) || p.unitMetres <= 0 || !finite(p.position) ||
				!std::isfinite(p.yawDegrees))
				environment(string::format("prop '{}' needs a mesh, a positive unit and a finite position and yaw", p.name));
		}

		std::unordered_map<core::Uuid, const Mount*> mounts;
		for (const Mount& m : plan.mounts)
		{
			identify(m.id, "mount", m.name);
			mounts.emplace(m.id, &m);
			bool valid = std::isfinite(m.yawDegrees);
			if (const auto* pole = std::get_if<Pole>(&m.kind))
			{
				valid = valid && finite(pole->floor) && std::isfinite(pole->height) && pole->height > 0 &&
						std::isfinite(pole->diameter) && pole->diameter > 0;
			}
			else
			{
				valid = valid && finite(std::get<FixedPoint>(m.kind).position);
			}
			if (!valid)
			{
				out.push_back({PlanProblem::InvalidMount,
							   string::format("mount '{}' needs a finite position and yaw, and a pole a positive height and diameter",
											  m.name)});
			}
			if (m.tolerance.position.nanometres() < 0 || !validBound(m.tolerance.yawDegrees))
				out.push_back({PlanProblem::InvalidTolerance, string::format("mount '{}' has a negative tolerance", m.name)});
		}

		std::set<std::string> names;
		for (const PlacedCamera& c : plan.cameras)
		{
			identify(c.id, "camera", c.name);
			if (c.name.empty())
				out.push_back({PlanProblem::Unnamed, "a placed camera has no name"});
			else if (!validFileName(c.name))
			{
				out.push_back({PlanProblem::InvalidName,
							   string::format("camera '{}' cannot be a folder's name on every platform", c.name)});
			}
			else if (!names.insert(folded(c.name)).second)
			{
				// Folded: on a case-insensitive file system "Cam0" and "cam0" are one folder.
				out.push_back({PlanProblem::DuplicateName, string::format("two cameras are named '{}', ignoring case", c.name)});
			}
			if (!findCamera(plan.specifications, c.camera))
			{
				out.push_back({PlanProblem::UnknownSpecification,
							   string::format("camera '{}' names a camera specification '{}' the rig plan does not have", c.name,
											  c.camera)});
			}
			if (!mounts.count(c.mount))
				out.push_back({PlanProblem::UnknownMount, string::format("camera '{}' is on a mount the rig plan does not have", c.name)});
			if (!finite(c.offset) || !finite(c.aim))
				out.push_back({PlanProblem::InvalidPlacement, string::format("camera '{}' has an offset or aim that is not finite", c.name)});
			const PlacementTolerance& t = c.tolerance;
			if (t.offset.nanometres() < 0 || !validBound(t.panDegrees) || !validBound(t.tiltDegrees) || !validBound(t.rollDegrees))
				out.push_back({PlanProblem::InvalidTolerance, string::format("camera '{}' has a negative tolerance", c.name)});
		}

		for (const PlacedLight& l : plan.lights)
		{
			identify(l.id, "light", l.name);
			if (!findLight(plan.specifications, l.light))
			{
				out.push_back({PlanProblem::UnknownSpecification,
							   string::format("light '{}' names a light specification '{}' the rig plan does not have", l.name,
											  l.light)});
			}
			if (!mounts.count(l.mount))
				out.push_back({PlanProblem::UnknownMount, string::format("light '{}' is on a mount the rig plan does not have", l.name)});
			if (!finite(l.offset) || !finite(l.aim))
				out.push_back({PlanProblem::InvalidPlacement, string::format("light '{}' has an offset or aim that is not finite", l.name)});
		}

		if (plan.timeline.frames == 0)
			out.push_back({PlanProblem::InvalidTimeline, "the capture timeline has no frames"});
		if (!validScale(plan.scales.intrinsic) || !validScale(plan.scales.installation) || !validScale(plan.scales.noise))
			out.push_back({PlanProblem::InvalidScale, "every scale must lie between 0 and 1"});
		return out;
	}
} // namespace lain::rigplan
