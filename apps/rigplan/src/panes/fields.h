#pragma once

#include <lain/camera/distortion.h>
#include <lain/core/length.h>
#include <lain/core/uri.h>
#include <lain/core/uuid.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/enums.h>
#include <lain/image/color.h>
#include <lain/math/types.h>
#include <lain/rigplan/rigplan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rigplan::fields
{
	// The Inspector's widgets, each an ImGui field over one value of a rig plan in the unit a person
	// thinks in: positions and sizes in metres, angles in degrees, what a datasheet quotes in its own
	// unit (a focal length in millimetres, a pixel pitch in micrometres). Each returns true on the frame
	// it changed the value. Main thread, inside a window.

	bool metres(const char* label, double& value, double speed = 0.01);
	bool metres(const char* label, lain::math::Vec2d& value, double speed = 0.01);
	bool metres(const char* label, lain::math::Vec3d& value, double speed = 0.01);
	bool degrees(const char* label, double& value);
	bool aim(lain::rigplan::Aim& aim); // pan, tilt and roll, in that order, as aim.h turns them

	// A core::Length in millimetres or micrometres: exact in the plan, rounded to the nanometre.
	bool millimetres(const char* label, lain::core::Length& value);
	bool micrometres(const char* label, lain::core::Length& value);

	// A fraction shown as a percentage, 0 to `maximum`.
	bool percent(const char* label, double& fraction, double maximum = 1.0);
	bool number(const char* label, double& value, double speed, double minimum, double maximum, const char* format);
	bool count(const char* label, std::uint32_t& value, std::uint32_t minimum, std::uint32_t maximum);
	bool count(const char* label, std::size_t& value, std::size_t minimum);
	bool seed(const char* label, std::uint64_t& value);
	bool colour(const char* label, lain::image::ColorRGBf& value);

	template <typename E>
	bool choice(const char* label, E& value)
	{
		return lain::gui::enumCombo(label, value);
	}

	// One of `names`, by name; a name not among them is shown as it is, so a plan naming a specification
	// it lacks shows what it names.
	bool pick(const char* label, std::string& value, const std::vector<std::string>& names);

	// The mount `value` names, picked from the plan's by name.
	bool pickMount(const char* label, lain::core::Uuid& value, const lain::rigplan::RigPlan& plan);

	// A file's uri: its text, which commits when editing ends, and a button that picks one through the
	// native dialog. The uri is absolute in memory (ADR-0027).
	bool file(const char* label, lain::core::Uri& value, const std::string& title, const std::vector<lain::gui::FileFilter>& filters);

	// The dialog filters for a picture (every format io::image reads) and a mesh (every format io::mesh
	// reads), each with "All files" last.
	std::vector<lain::gui::FileFilter> pictureFilters();
	std::vector<lain::gui::FileFilter> meshFilters();

	// A surface's material: a flat colour, a picture repeated every tile, or a seeded procedural texture,
	// with MTL's specular.
	bool material(lain::rigplan::Material& material);

	// A lens's distortion: its model, then each coefficient by its name.
	bool distortion(lain::camera::Distortion& value);

	// An identity, shown and never edited: a draw is keyed by it (ADR-0029).
	void identity(const lain::core::Uuid& id);
} // namespace rigplan::fields
