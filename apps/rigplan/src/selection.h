#pragma once

#include <lain/core/uuid.h>
#include <lain/rigplan/rigplan.h>

#include <cstddef>
#include <string>

namespace rigplan
{
	// What a selection is of.
	enum class Selected
	{
		Nothing,
		Plan, // the plan as a whole: its timeline, seed, scales and reference
		Room,
		Mount,
		Camera,
		Light,
		Plane,
		Box,
		Prop,
		Board,
		Key,		   // one key of a mover's path: a board's, or a prop mover's
		Specification, // one of the plan's own specifications
	};

	// The four kinds of specification a plan holds its own copy of.
	enum class SpecificationKind
	{
		Sensor,
		Lens,
		Camera,
		Light,
	};

	// What the editor has selected, by IDENTITY, never by position in a list: an undo rebuilds the plan,
	// and a selection that named an index would then name whatever came to stand there. So a selection
	// survives an undo, and one whose object the undo removed is recognised as gone (exists()).
	struct Selection
	{
		Selected kind = Selected::Nothing;
		lain::core::Uuid id; // a mount's, camera's, light's, plane's, box's, prop's or board's; a key's mover's
		std::size_t key = 0; // a key's index in its mover's path
		SpecificationKind specification = SpecificationKind::Sensor;
		std::string name; // a specification's: a specification is known by its name

		static Selection nothing() { return {}; }
		static Selection plan() { return of(Selected::Plan); }
		static Selection room() { return of(Selected::Room); }
		// A mount, camera, light, plane, box, prop or board, by its identity.
		static Selection object(Selected kind, const lain::core::Uuid& id)
		{
			Selection s = of(kind);
			s.id = id;
			return s;
		}
		static Selection keyOf(const lain::core::Uuid& mover, std::size_t index)
		{
			Selection s = object(Selected::Key, mover);
			s.key = index;
			return s;
		}
		static Selection specificationOf(SpecificationKind kind, std::string name)
		{
			Selection s = of(Selected::Specification);
			s.specification = kind;
			s.name = std::move(name);
			return s;
		}

		bool operator==(const Selection& rhs) const
		{
			return kind == rhs.kind && id == rhs.id && key == rhs.key && specification == rhs.specification && name == rhs.name;
		}
		bool operator!=(const Selection& rhs) const { return !(*this == rhs); }

	private:
		static Selection of(Selected kind)
		{
			Selection s;
			s.kind = kind;
			return s;
		}
	};

	// Whether `selection` names something `plan` has: Nothing and the plan always do, the room when there
	// is one, a key while its mover has that many keys, a specification while one of its kind has the
	// name.
	bool exists(const lain::rigplan::RigPlan& plan, const Selection& selection);

	// The capture frame the Viewport shows for `selection`: a key's frame, and frame 0 otherwise.
	std::size_t shownFrame(const lain::rigplan::RigPlan& plan, const Selection& selection);

	// The path of the mover `id` names, a board's or a prop mover's, or null.
	const lain::rigplan::Path* pathOf(const lain::rigplan::RigPlan& plan, const lain::core::Uuid& id);
	lain::rigplan::Path* pathOf(lain::rigplan::RigPlan& plan, const lain::core::Uuid& id);
} // namespace rigplan
