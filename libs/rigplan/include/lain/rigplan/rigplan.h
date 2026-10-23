#pragma once

#include "lain/rigplan/aim.h"
#include "lain/rigplan/environment.h"
#include "lain/rigplan/specification.h"

#include <lain/core/uuid.h>
#include <lain/math/axisconvention.h>
#include <lain/media/framespec.h> // FrameRate

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lain::rigplan
{
	// Every rig plan is stored in the plan frame (CONTEXT.md, "Plan frame"): right-handed, X right, Y
	// forward across the floor, Z up, metres, the floor at Z = 0. One named convention, never metadata
	// on a plan; an asset's own up axis is converted to it at import.
	inline constexpr math::AxisConvention kPlanFrame = math::AxisConvention::XRightYForwardZUp();

	// A pole standing on the floor. It is real geometry, in every capture render.
	struct Pole
	{
		math::Vec2d floor{0.0, 0.0}; // where it stands, metres in the plan frame's XY
		double height = 2.5;		 // metres
		double diameter = 0.05;		 // metres
	};

	// A bare frame anywhere, with no geometry: for a camera or light fixed to a wall or a ceiling.
	struct FixedPoint
	{
		math::Vec3d position{0.0, 0.0, 0.0}; // metres in the plan frame
	};

	// How far a real installation's mount may stray from the plan.
	struct MountTolerance
	{
		core::Length position; // on each axis of the floor; a fixed point's height too
		double yawDegrees = 0;
	};

	// A rigid fixture carrying cameras and lights (CONTEXT.md, "Mount"). Its frame is the plan frame
	// translated to where it stands and turned by its yaw about +Z; what it carries is posed in that.
	struct Mount
	{
		core::Uuid id; // stable for the mount's life: its installation draws are keyed by it
		std::string name;
		std::variant<Pole, FixedPoint> kind;
		double yawDegrees = 0;
		MountTolerance tolerance;
	};

	// How far a real camera's placement on its mount may stray from the plan.
	struct PlacementTolerance
	{
		core::Length offset; // on each axis of the mount's frame
		double panDegrees = 0;
		double tiltDegrees = 0;
		double rollDegrees = 0;
	};

	// A camera specification put into a rig plan (CONTEXT.md, "Placed camera"). Its mounting point sits
	// at `offset` in its mount's frame, aimed by `aim`, and its optical centre is the sensor body's
	// offset from that.
	struct PlacedCamera
	{
		core::Uuid id;					   // stable for the camera's life: its draws are keyed by it, never by its name
		std::string name;				   // unique among cameras, and a valid file name: the capture's folder for it
		std::string camera;				   // a camera specification of the rig plan's
		core::Uuid mount;				   // the mount it is fixed to
		math::Vec3d offset{0.0, 0.0, 0.0}; // metres, in the mount's frame
		Aim aim;
		PlacementTolerance tolerance;
	};

	// A light specification put into a rig plan, on a mount, shining along its forward axis.
	struct PlacedLight
	{
		core::Uuid id;
		std::string name;
		std::string light; // a light specification of the rig plan's
		core::Uuid mount;
		math::Vec3d offset{0.0, 0.0, 0.0}; // metres, in the mount's frame
		Aim aim;
	};

	// The frames a synthetic capture records (CONTEXT.md, "Capture timeline").
	struct CaptureTimeline
	{
		std::size_t frames = 1;
		media::FrameRate rate; // unspecified is allowed: by-position grouping reads no timestamp
	};

	// The numbers a realisation scales by (CONTEXT.md, "Deviation scale", "Noise scale"), each 0 to 1.
	struct Scales
	{
		double intrinsic = 0;
		double installation = 0;
		bool locked = true; // installation follows intrinsic until a user separates them
		double noise = 0;

		double effectiveInstallation() const { return locked ? intrinsic : installation; }
	};

	// The document a rig is designed in (CONTEXT.md, "Rig plan"). Nominal: what was meant, before any
	// realisation. Self-contained: it holds its own copy of every specification it uses.
	struct RigPlan
	{
		Specifications specifications;
		std::optional<Room> room; // a rig plan may stand in none, among props alone
		std::vector<Plane> planes;
		std::vector<Box> boxes;
		std::vector<Prop> props;
		std::vector<Mount> mounts;
		std::vector<PlacedCamera> cameras;
		std::vector<PlacedLight> lights;
		CaptureTimeline timeline;
		std::uint64_t seed = 1;
		Scales scales;
	};

	// Why a rig plan cannot be realised.
	enum class PlanProblem
	{
		InvalidSpecification, // a specification has a problem; the detail is its own diagnostic
		MissingIdentity,	  // a mount, camera or light with the nil uuid
		DuplicateIdentity,	  // two of them share a uuid
		Unnamed,			  // a camera with no name
		DuplicateName,		  // two cameras share a name
		InvalidName,		  // a camera name that is not a valid file name on every platform
		UnknownSpecification, // a camera or light names a specification the rig plan does not have
		UnknownMount,		  // a camera or light names a mount the rig plan does not have
		InvalidMount,		  // a pole of no height or diameter, or a mount at a position that is not finite
		InvalidPlacement,	  // an offset or an aim that is not finite
		InvalidTolerance,	  // a negative or non-finite tolerance
		InvalidTimeline,	  // a timeline of no frames
		InvalidScale,		  // a scale outside [0, 1]
		InvalidEnvironment,	  // a room, plane or box of no extent, a prop with no mesh or unit, or a material that cannot be one
	};

	struct PlanDiagnostic
	{
		PlanProblem problem;
		std::string detail; // which part and what about it, for a person
	};

	// Every problem with `plan`: its specifications first, then its room, planes, boxes and props, then
	// its mounts, cameras, lights, timeline and scales. Empty when the rig plan can be realised (though a realisation may still refuse a
	// draw; see realise()).
	std::vector<PlanDiagnostic> check(const RigPlan& plan);

	// Whether `name` can be a folder's name on Windows, macOS and Linux alike: not empty, not "." or
	// "..", no control character and none of < > : " / \ | ? *, not ending in a space or a dot, and not
	// a name Windows reserves (CON, PRN, AUX, NUL, COM1-9, LPT1-9, with or without an extension).
	bool validFileName(std::string_view name);
} // namespace lain::rigplan
