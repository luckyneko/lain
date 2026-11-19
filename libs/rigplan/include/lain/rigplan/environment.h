#pragma once

#include "lain/rigplan/aim.h"
#include "lain/rigplan/mover.h"

#include <lain/core/uri.h>
#include <lain/core/uuid.h>
#include <lain/image/color.h>
#include <lain/image/procedural.h>
#include <lain/math/axisconvention.h>
#include <lain/math/types.h>
#include <lain/media/framespec.h>

#include <cstddef>
#include <optional>
#include <string>
#include <variant>

namespace lain::rigplan
{
	// What a rig is installed in and what stands around it: the room, planes, boxes and props
	// (CONTEXT.md, "Room", "Prop", "Material"). None of it has tolerances, so a realisation leaves it as
	// planned.

	// A surface of one flat colour.
	struct SolidColour
	{
		image::ColorRGBf colour{0.8f, 0.8f, 0.8f}; // linear RGB reflectance
	};

	// A surface showing a picture. On a surface the rig plan lays out itself (a room's, a plane's, a
	// box's), one copy of the picture covers `tile` metres and repeats; a mesh that gives texture
	// coordinates uses its own instead.
	struct ImageTexture
	{
		core::Uri image;			// absolute in memory, relative on disk inside the document's tree (ADR-0027)
		math::Vec2d tile{1.0, 1.0}; // metres one copy covers, across and up
	};

	// How a surface looks (CONTEXT.md, "Material"): a flat colour, a picture, or a seeded procedural
	// texture, rich in the features targetless methods need. Specular shading is MTL's (Blinn-Phong): a
	// colour and an exponent, black by default, so a surface is matte unless it says otherwise.
	struct Material
	{
		std::variant<SolidColour, ImageTexture, image::ProceduralTexture> surface;
		image::ColorRGBf specular{0.0f, 0.0f, 0.0f};
		double shininess = 0;
	};

	// One of a room's six surfaces.
	struct RoomSurface
	{
		bool present = true;
		Material material;
	};

	// A parametric axis-aligned box in the plan frame (CONTEXT.md, "Room"): its floor at Z = 0 centred on
	// `centre`, `size` metres across X, along Y and up Z. The walls are named as a floor plan is drawn:
	// north is +Y, east is +X. Each surface faces in, and resizing the room moves its walls.
	struct Room
	{
		math::Vec2d centre{0.0, 0.0};
		math::Vec3d size{8.0, 6.0, 3.0};
		RoomSurface floor;
		RoomSurface ceiling;
		RoomSurface north;
		RoomSurface south;
		RoomSurface east;
		RoomSurface west;
	};

	// A flat rectangle anywhere, for a partition or a patch of floor: `size` metres across and up,
	// centred on `centre`. At rest it stands upright facing -Y, so a camera at rest (looking along +Y)
	// sees it face on; `aim` turns it as it turns a camera (roll, tilt, pan, about the plan's axes). Both
	// faces show its material.
	struct Plane
	{
		core::Uuid id;
		std::string name;
		math::Vec3d centre{0.0, 0.0, 0.0};
		Aim aim;
		math::Vec2d size{1.0, 1.0};
		Material material;
	};

	// A box, for furniture and pillars: `size` metres across X, along Y and up Z before it is turned,
	// centred on `centre`, turned by `yawDegrees` about +Z.
	struct Box
	{
		core::Uuid id;
		std::string name;
		math::Vec3d centre{0.0, 0.0, 0.5};
		math::Vec3d size{1.0, 1.0, 1.0};
		double yawDegrees = 0;
		Material material;
	};

	// A mesh file's own up axis: an OBJ is Y-up by convention (Blender's export, with -Z forward), and
	// much CAD output is Z-up.
	enum class UpAxis
	{
		Y, // X right, Y up, Z toward the viewer
		Z, // X right, Y forward, Z up: the plan frame's own
	};

	// The axis convention a mesh with this up axis is written in.
	math::AxisConvention conventionOf(UpAxis up);

	// A mesh asset placed in a rig plan (CONTEXT.md, "Prop"), in its file's own materials. Its mesh is
	// converted to the plan frame once, where a render scene is built: its up axis to the plan frame's,
	// each unit to `unitMetres` metres, turned by `yawDegrees` about +Z and stood at `position`. A prop
	// that follows a path is a prop mover: its path places it at every frame instead, and its position
	// and yaw stand unused.
	struct Prop
	{
		core::Uuid id;
		std::string name;
		core::Uri mesh; // absolute in memory, relative on disk inside the document's tree (ADR-0027)
		// A numbered run of meshes (a mesh clip), `mesh` then naming its frame pattern, played on a loop
		// at `clipRate`. Stated, not detected from the name, so a mesh whose name happens to hold a
		// pattern's brackets is not taken for one.
		bool clip = false;
		media::FrameRate clipRate;
		UpAxis up = UpAxis::Y;
		double unitMetres = 1.0;
		math::Vec3d position{0.0, 0.0, 0.0};
		double yawDegrees = 0;
		// Where it is at each frame, aimed as a camera is but upright at rest (a pan is a yaw, so a key
		// with only a pan stands it as `position` and `yawDegrees` would).
		std::optional<Path> path;
	};

	// The prop's mesh coordinates into the plan frame at capture frame `frame`: the basis change from
	// its up axis, the unit scale, then its yaw and position, or its path's pose at `frame`. Not rigid,
	// so a matrix: a point (x, y, z, 1) of the file maps to the plan.
	math::Mat4d planFromProp(const Prop& prop, std::size_t frame);
} // namespace lain::rigplan
