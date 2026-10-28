#pragma once

#include "selection.h"

#include <lain/core/uri.h>
#include <lain/core/uuid.h>
#include <lain/rigplan/rigplan.h>

#include <optional>
#include <string>

namespace rigplan
{
	// The edits the editor makes to a rig plan besides moving things: add, delete, duplicate, rename and
	// set the reference. Each changes the plan whole, or says why not and changes nothing, so one edit is
	// at most one step of history and a refusal never leaves half an edit behind.

	struct EditResult
	{
		std::optional<Selection> selection; // set when the plan changed: what is selected now
		std::string refusal;				// set when it was refused: why, for a person

		bool changed() const { return selection.has_value(); }

		static EditResult done(Selection selection)
		{
			EditResult result;
			result.selection = std::move(selection);
			return result;
		}
		static EditResult refused(std::string why)
		{
			EditResult result;
			result.refusal = std::move(why);
			return result;
		}
		// Asked for nothing that would change the plan (a rename to the name it has): neither a change nor
		// a refusal.
		static EditResult nothing() { return {}; }
	};

	// Something new, each with a fresh identity and a name no other of its kind has, standing in the
	// middle of the room (of the plan frame's origin without one), and selected:
	//   * a pole 2.5 m high, or a fixed point 2 m up;
	//   * a camera or a light on the mount `on` names (a mount, or a camera or light whose mount it is),
	//     with the plan's first camera or light specification: on a pole 1.5 m up and 12.5 cm out from
	//     its axis, so it is clear of the pole, and on a fixed point at the point. Refused with no mount,
	//     or no specification to place;
	//   * a plane 1 x 1 m, standing 1 m up and facing -Y; a 1 m box on the floor;
	//   * a static prop showing `mesh`, read Y-up in metres;
	//   * a board (M9's: 7 x 5 squares of 24 mm, ArUco 5x5_100) with one key, 1.25 m up and facing -Y;
	//   * the default room, refused when the plan has one.
	EditResult addPole(lain::rigplan::RigPlan& plan);
	EditResult addFixedPoint(lain::rigplan::RigPlan& plan);
	EditResult addCamera(lain::rigplan::RigPlan& plan, const Selection& on);
	EditResult addLight(lain::rigplan::RigPlan& plan, const Selection& on);
	EditResult addPlane(lain::rigplan::RigPlan& plan);
	EditResult addBox(lain::rigplan::RigPlan& plan);
	EditResult addProp(lain::rigplan::RigPlan& plan, lain::core::Uri mesh);
	EditResult addBoard(lain::rigplan::RigPlan& plan);
	EditResult addRoom(lain::rigplan::RigPlan& plan);

	// Deletes what `selection` names, leaving nothing selected.
	//   * A mount takes the cameras and lights on it with it: one edit, so one step of history.
	//   * A camera that was the reference leaves the plan with none (its first camera, then).
	//   * A key is refused when it is its mover's last: a path needs one, so delete the mover instead.
	//   * A specification is refused while anything names it, saying what.
	//   * The plan, and nothing, are not things to delete.
	EditResult remove(lain::rigplan::RigPlan& plan, const Selection& selection);

	// Copies what `selection` names, and selects the copy. The copy is a new unit (ADR-0029): a fresh
	// identity, so its draws are its own, and a name no other of its kind has ("cam1 2", from a trailing
	// number replaced rather than stacked). A mount is copied with the cameras and lights on it, each a
	// new unit too; a board's printed identity is made unique as its name is, since two printed boards
	// are two boards. A key is copied one frame on, refused where that frame has a key. The room and the
	// plan are refused: a plan has one of each.
	EditResult duplicate(lain::rigplan::RigPlan& plan, const Selection& selection);

	// Gives what `selection` names the name `name`, by check()'s rules: a camera's must be a valid file
	// name on every platform (validFileName) and unique among cameras ignoring case, since it is the
	// folder its footage is written to; a board's unique among boards; a specification's unique among
	// its kind, and every reference to it follows, as one edit. No name may be empty. Refused otherwise,
	// changing nothing. A rename to the name it has is nothing().
	EditResult rename(lain::rigplan::RigPlan& plan, const Selection& selection, const std::string& name);

	// Makes the camera `camera` names the one a capture's rig is relative to, or, given Nothing, leaves
	// the plan to its first camera. Refused for anything else.
	EditResult setReference(lain::rigplan::RigPlan& plan, const Selection& camera);
} // namespace rigplan
