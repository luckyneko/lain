#include "inspector.h"

#include "../appcontext.h"
#include "../edits.h"
#include "fields.h"

#include <lain/camera/board/pattern.h>
#include <lain/gui/gui.h>
#include <lain/image/colorspace.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/specification.h>
#include <lain/string/format.h>

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace rigplan
{
	using namespace lain;
	namespace rp = lain::rigplan;

	template <typename T>
	static T* byId(std::vector<T>& items, const core::Uuid& id)
	{
		const auto found = std::find_if(items.begin(), items.end(), [&](const T& item)
										{ return item.id == id; });
		return found != items.end() ? &*found : nullptr;
	}

	template <typename T>
	static T* byName(std::vector<T>& items, const std::string& name)
	{
		const auto found = std::find_if(items.begin(), items.end(), [&](const T& item)
										{ return item.name == name; });
		return found != items.end() ? &*found : nullptr;
	}

	template <typename T>
	static std::vector<std::string> namesOf(const std::vector<T>& items)
	{
		std::vector<std::string> names;
		for (const T& item : items)
			names.push_back(item.name);
		return names;
	}

	// The name field: committed when editing ends, through edits.h's rename, so a camera's name stays a
	// folder's and a specification's references follow it. A refusal is said, and the field shows the
	// name the plan still has from the next frame.
	//
	// `self` is what is renamed, never simply what is selected: a board's editor shows while one of its
	// keys is selected, and a key has no name.
	static void name(AppContext& ctx, const Selection& self, const std::string& current)
	{
		std::string text = current;
		gui::InputText("Name", &text);
		if (!gui::IsItemDeactivatedAfterEdit() || text == current)
			return;
		const EditResult result = rename(ctx.document.plan, self, text);
		if (result.changed())
		{
			if (ctx.selection == self)
				ctx.selection = *result.selection; // a specification is selected by its name
			ctx.changed();
		}
		else if (!result.refusal.empty())
			ctx.note(result.refusal, true);
	}

	static void heading(const char* text)
	{
		gui::Spacing();
		gui::SeparatorText(text);
	}

	// A mover's keys: frame, position and aim, a row each. A row's # selects that key, which the
	// Viewport shows (slice 8f) and the gizmo moves; a frame is held between its neighbours', so the
	// keys stay in frame order. Below, a key one frame after the last, where it stands.
	static bool keyTable(AppContext& ctx, const core::Uuid& mover, rp::Path& path)
	{
		bool changed = false;
		const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
		if (gui::BeginTable("keys", 8, flags))
		{
			gui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
			for (const char* column : {"Frame", "X", "Y", "Z", "Pan", "Tilt", "Roll"})
				gui::TableSetupColumn(column);
			gui::TableHeadersRow();
			for (std::size_t k = 0; k < path.keys.size(); ++k)
			{
				rp::PathKey& key = path.keys[k];
				gui::PushID(int(k));
				gui::TableNextRow();
				gui::TableNextColumn();
				const Selection self = Selection::keyOf(mover, k);
				if (gui::Selectable(std::to_string(k).c_str(), ctx.selection == self))
					ctx.selection = self;
				gui::TableNextColumn();
				gui::SetNextItemWidth(-1.0f);
				std::uint64_t frame = key.frame;
				const std::uint64_t lowest = k == 0 ? 0 : path.keys[k - 1].frame + 1;
				const std::uint64_t highest = k + 1 < path.keys.size() ? path.keys[k + 1].frame - 1 : std::uint64_t(1) << 31;
				if (gui::DragScalar("##frame", ImGuiDataType_U64, &frame, 0.2f, &lowest, &highest, "%llu", ImGuiSliderFlags_AlwaysClamp))
				{
					key.frame = std::size_t(frame);
					changed = true;
				}
				for (double* metres : {&key.position.x, &key.position.y, &key.position.z})
				{
					gui::TableNextColumn();
					gui::SetNextItemWidth(-1.0f);
					gui::PushID(metres);
					changed |= gui::DragScalar("##m", ImGuiDataType_Double, metres, 0.005f, nullptr, nullptr, "%.3f");
					gui::PopID();
				}
				for (double* degrees : {&key.aim.panDegrees, &key.aim.tiltDegrees, &key.aim.rollDegrees})
				{
					gui::TableNextColumn();
					gui::SetNextItemWidth(-1.0f);
					gui::PushID(degrees);
					changed |= gui::DragScalar("##deg", ImGuiDataType_Double, degrees, 0.25f, nullptr, nullptr, "%.1f");
					gui::PopID();
				}
				gui::PopID();
			}
			gui::EndTable();
		}
		if (gui::Button("Add key") && !path.keys.empty())
		{
			rp::PathKey next = path.keys.back();
			++next.frame;
			path.keys.push_back(next);
			ctx.selection = Selection::keyOf(mover, path.keys.size() - 1);
			changed = true;
		}
		if (gui::IsItemHovered())
			gui::SetTooltip("A key one frame after the last, where the last one stands. Delete one with Edit > Delete.");
		return changed;
	}

	static bool plan(AppContext& ctx)
	{
		rp::RigPlan& p = ctx.document.plan;
		bool changed = false;
		heading("Timeline");
		changed |= fields::count("Frames", p.timeline.frames, 1);
		changed |= fields::count("Rate numerator", p.timeline.rate.numerator, 0, 1000000);
		changed |= fields::count("Rate denominator", p.timeline.rate.denominator, 1, 1000000);
		if (gui::IsItemHovered())
			gui::SetTooltip("Frames per second as a fraction; a numerator of 0 leaves the rate unspecified");

		heading("Realisation");
		changed |= fields::seed("Seed", p.seed);
		changed |= fields::number("Intrinsic scale", p.scales.intrinsic, 0.005, 0.0, 1.0, "%.3f");
		changed |= gui::Checkbox("Installation follows intrinsic", &p.scales.locked);
		gui::BeginDisabled(p.scales.locked);
		changed |= fields::number("Installation scale", p.scales.installation, 0.005, 0.0, 1.0, "%.3f");
		gui::EndDisabled();
		changed |= fields::number("Noise scale", p.scales.noise, 0.005, 0.0, 1.0, "%.3f");

		heading("Reference");
		std::string current = "(the first camera)";
		for (const rp::PlacedCamera& c : p.cameras)
		{
			if (p.reference && c.id == *p.reference)
				current = c.name;
		}
		if (gui::BeginCombo("Reference camera", current.c_str()))
		{
			if (gui::Selectable("(the first camera)", !p.reference) && p.reference)
				changed |= setReference(p, Selection::nothing()).changed();
			for (const rp::PlacedCamera& c : p.cameras)
			{
				gui::PushID(c.id.toString().c_str());
				const bool selected = p.reference && *p.reference == c.id;
				if (gui::Selectable(c.name.c_str(), selected) && !selected)
					changed |= setReference(p, Selection::object(Selected::Camera, c.id)).changed();
				gui::PopID();
			}
			gui::EndCombo();
		}
		return changed;
	}

	static bool room(rp::Room& room)
	{
		bool changed = false;
		changed |= fields::metres("Centre", room.centre);
		changed |= fields::metres("Size", room.size);
		const std::pair<const char*, rp::RoomSurface*> surfaces[] = {{"Floor", &room.floor}, {"Ceiling", &room.ceiling}, {"North wall", &room.north}, {"South wall", &room.south}, {"East wall", &room.east}, {"West wall", &room.west}};
		for (const auto& [label, surface] : surfaces)
		{
			gui::PushID(label);
			if (gui::CollapsingHeader(label))
			{
				changed |= gui::Checkbox("Present", &surface->present);
				gui::BeginDisabled(!surface->present);
				changed |= fields::material(surface->material);
				gui::EndDisabled();
			}
			gui::PopID();
		}
		return changed;
	}

	static bool mount(AppContext& ctx, rp::Mount& mount)
	{
		bool changed = false;
		name(ctx, Selection::object(Selected::Mount, mount.id), mount.name);
		fields::identity(mount.id);
		if (auto* pole = std::get_if<rp::Pole>(&mount.kind))
		{
			gui::TextDisabled("A pole, standing on the floor");
			changed |= fields::metres("Floor", pole->floor);
			changed |= fields::number("Height", pole->height, 0.01, 0.01, 100.0, "%.3f m");
			changed |= fields::number("Diameter", pole->diameter, 0.001, 0.001, 10.0, "%.3f m");
		}
		else
		{
			gui::TextDisabled("A fixed point, with no geometry of its own");
			changed |= fields::metres("Position", std::get<rp::FixedPoint>(mount.kind).position);
		}
		changed |= fields::degrees("Yaw", mount.yawDegrees);
		heading("Tolerance");
		changed |= fields::millimetres("Position tolerance", mount.tolerance.position);
		changed |= fields::degrees("Yaw tolerance", mount.tolerance.yawDegrees);
		return changed;
	}

	static bool camera(AppContext& ctx, rp::PlacedCamera& camera)
	{
		rp::RigPlan& p = ctx.document.plan;
		bool changed = false;
		name(ctx, Selection::object(Selected::Camera, camera.id), camera.name);
		fields::identity(camera.id);
		changed |= fields::pick("Specification", camera.camera, namesOf(p.specifications.cameras));
		changed |= fields::pickMount("Mount", camera.mount, p);
		heading("Placement, in its mount's frame");
		changed |= fields::metres("Offset", camera.offset);
		changed |= fields::aim(camera.aim);
		heading("Tolerance");
		gui::PushID("tolerance");
		changed |= fields::millimetres("Offset tolerance", camera.tolerance.offset);
		changed |= fields::degrees("Pan tolerance", camera.tolerance.panDegrees);
		changed |= fields::degrees("Tilt tolerance", camera.tolerance.tiltDegrees);
		changed |= fields::degrees("Roll tolerance", camera.tolerance.rollDegrees);
		gui::PopID();
		heading("Reference");
		const bool reference = p.reference ? *p.reference == camera.id : !p.cameras.empty() && p.cameras.front().id == camera.id;
		bool chosen = p.reference && *p.reference == camera.id;
		if (gui::Checkbox("The rig's reference camera", &chosen))
			changed |= setReference(p, chosen ? Selection::object(Selected::Camera, camera.id) : Selection::nothing()).changed();
		if (reference && !chosen)
			gui::TextDisabled("It is, as the plan's first camera");
		return changed;
	}

	static bool light(AppContext& ctx, rp::PlacedLight& light)
	{
		rp::RigPlan& p = ctx.document.plan;
		bool changed = false;
		name(ctx, Selection::object(Selected::Light, light.id), light.name);
		fields::identity(light.id);
		changed |= fields::pick("Specification", light.light, namesOf(p.specifications.lights));
		changed |= fields::pickMount("Mount", light.mount, p);
		heading("Placement, in its mount's frame");
		changed |= fields::metres("Offset", light.offset);
		changed |= fields::aim(light.aim);
		return changed;
	}

	static bool plane(AppContext& ctx, rp::Plane& plane)
	{
		bool changed = false;
		name(ctx, Selection::object(Selected::Plane, plane.id), plane.name);
		fields::identity(plane.id);
		changed |= fields::metres("Centre", plane.centre);
		changed |= fields::aim(plane.aim);
		changed |= fields::metres("Size", plane.size);
		heading("Material");
		changed |= fields::material(plane.material);
		return changed;
	}

	static bool box(AppContext& ctx, rp::Box& box)
	{
		bool changed = false;
		name(ctx, Selection::object(Selected::Box, box.id), box.name);
		fields::identity(box.id);
		changed |= fields::metres("Centre", box.centre);
		changed |= fields::metres("Size", box.size);
		changed |= fields::degrees("Yaw", box.yawDegrees);
		heading("Material");
		changed |= fields::material(box.material);
		return changed;
	}

	static bool prop(AppContext& ctx, rp::Prop& prop)
	{
		bool changed = false;
		name(ctx, Selection::object(Selected::Prop, prop.id), prop.name);
		fields::identity(prop.id);
		changed |= fields::file("Mesh", prop.mesh, "Choose a mesh", fields::meshFilters());
		changed |= gui::Checkbox("A mesh clip", &prop.clip);
		if (gui::IsItemHovered())
			gui::SetTooltip("A numbered run of meshes its Mesh names by a pattern (shot.<frame:04>.obj), played on a loop");
		if (prop.clip)
		{
			changed |= fields::count("Clip rate numerator", prop.clipRate.numerator, 0, 1000000);
			changed |= fields::count("Clip rate denominator", prop.clipRate.denominator, 1, 1000000);
		}
		changed |= fields::choice("Up axis", prop.up);
		changed |= fields::number("Metres per unit", prop.unitMetres, 0.001, 1e-6, 1e6, "%.4f");

		bool moves = prop.path.has_value();
		if (gui::Checkbox("Moves along a path", &moves))
		{
			if (moves)
				prop.path = rp::Path{{rp::PathKey{0, prop.position, rp::Aim{prop.yawDegrees, 0.0, 0.0}}}};
			else
			{
				prop.path.reset();
				ctx.selection = Selection::object(Selected::Prop, prop.id); // no key of it to stay selected
			}
			changed = true;
		}
		if (prop.path)
		{
			heading("Path");
			changed |= keyTable(ctx, prop.id, *prop.path);
		}
		else
		{
			changed |= fields::metres("Position", prop.position);
			changed |= fields::degrees("Yaw", prop.yawDegrees);
		}
		return changed;
	}

	static bool board(AppContext& ctx, rp::BoardMover& board)
	{
		bool changed = false;
		name(ctx, Selection::object(Selected::Board, board.id), board.name);
		fields::identity(board.id);
		heading("Pattern");
		changed |= fields::choice("Dictionary", board.pattern.dictionary);
		changed |= fields::count("Squares across", board.pattern.squaresX, 2, 100);
		changed |= fields::count("Squares down", board.pattern.squaresY, 2, 100);
		changed |= fields::number("Marker to square", board.pattern.markerToSquare, 0.005, 0.05, 0.95, "%.3f");
		changed |= fields::count("First marker id", board.pattern.firstMarkerId, 0, 100000);
		changed |= fields::choice("Layout", board.pattern.layout);
		heading("The printed board");
		changed |= gui::InputText("Printed identity", &board.instance.identity);
		changed |= fields::millimetres("Square", board.instance.squareLength.value);
		changed |= fields::number("Margin", board.margin, 0.001, 0.0, 1.0, "%.3f m");
		changed |= fields::count("Pixels a square", board.pixelsPerSquare, 4, 1000);
		changed |= fields::number("Backing", board.backing, 0.0005, 0.0001, 0.1, "%.4f m");
		heading("Path");
		changed |= keyTable(ctx, board.id, board.path);
		return changed;
	}

	static bool sensor(AppContext& ctx, rp::SensorSpecification& sensor)
	{
		bool changed = false;
		name(ctx, Selection::specificationOf(SpecificationKind::Sensor, sensor.name), sensor.name);
		changed |= fields::count("Width", sensor.image.width, 1, 65536);
		changed |= fields::count("Height", sensor.image.height, 1, 65536);
		changed |= fields::micrometres("Pixel pitch", sensor.pixelPitch);
		changed |= fields::micrometres("Principal point tolerance", sensor.principalPointTolerance);
		heading("Body, in the camera's frame");
		changed |= fields::metres("Size", sensor.body.size, 0.001);
		changed |= fields::metres("Optical centre", sensor.body.opticalCentre, 0.001);
		heading("Sensor model");
		rp::SensorModel& model = sensor.sensorModel;
		changed |= fields::number("Gain", model.gain, 0.01, 1e-6, 1e6, "%.3f");
		changed |= fields::number("Full well", model.fullWellElectrons, 10.0, 1.0, 1e9, "%.0f e-");
		changed |= fields::number("Read noise", model.readNoiseElectrons, 0.05, 0.0, 1e6, "%.2f e-");
		changed |= fields::count("ADC bits", model.adcBits, 1, 16);
		changed |= fields::choice("Channels", model.channels);
		changed |= fields::choice("Format", model.output.format);
		changed |= fields::count("Bits", model.output.bits, 8, 16);
		changed |= fields::choice("Curve", model.output.curve);
		bool quality = model.output.quality.has_value();
		if (gui::Checkbox("Quality", &quality))
		{
			model.output.quality = quality ? std::optional<std::uint8_t>(90) : std::nullopt;
			changed = true;
		}
		if (model.output.quality)
		{
			std::uint32_t q = *model.output.quality;
			gui::SameLine();
			if (fields::count("##quality", q, 1, 100))
			{
				model.output.quality = std::uint8_t(q);
				changed = true;
			}
		}
		for (const std::string& problem : rp::sensorModelProblems(model))
			gui::TextColored(ImVec4(0.9f, 0.7f, 0.25f, 1.0f), "%s", problem.c_str());
		return changed;
	}

	static bool lens(AppContext& ctx, rp::LensSpecification& lens)
	{
		bool changed = false;
		name(ctx, Selection::specificationOf(SpecificationKind::Lens, lens.name), lens.name);
		changed |= fields::millimetres("Focal length", lens.focalLength);
		changed |= fields::percent("Focal length tolerance", lens.focalLengthTolerance, 0.99);
		heading("Distortion");
		changed |= fields::distortion(lens.distortion);
		changed |= fields::percent("Distortion tolerance", lens.distortionTolerance);
		return changed;
	}

	static bool cameraSpecification(AppContext& ctx, rp::CameraSpecification& spec)
	{
		rp::Specifications& s = ctx.document.plan.specifications;
		bool changed = false;
		name(ctx, Selection::specificationOf(SpecificationKind::Camera, spec.name), spec.name);
		changed |= fields::pick("Sensor", spec.sensor, namesOf(s.sensors));
		changed |= fields::pick("Lens", spec.lens, namesOf(s.lenses));
		// What the pair makes, as every camera of it is drawn and captured nominally.
		const rp::SensorSpecification* sensor = rp::findSensor(s, spec.sensor);
		const rp::LensSpecification* lens = rp::findLens(s, spec.lens);
		if (sensor && lens)
		{
			const camera::ModelResult model = rp::nominalModel(*sensor, *lens);
			if (model.model)
				gui::TextDisabled("Nominal model: %s", model.model->toString().c_str());
			for (const camera::ModelDiagnostic& d : model.diagnostics)
				gui::TextColored(ImVec4(0.9f, 0.7f, 0.25f, 1.0f), "%s", d.detail.c_str());
		}
		return changed;
	}

	static bool lightSpecification(AppContext& ctx, rp::LightSpecification& spec)
	{
		bool changed = false;
		name(ctx, Selection::specificationOf(SpecificationKind::Light, spec.name), spec.name);
		changed |= fields::choice("Kind", spec.kind);
		changed |= fields::number("Intensity", spec.intensity, 0.01, 0.0, 1e6, "%.3f");
		changed |= fields::colour("Colour", spec.colour);
		if (spec.kind == rp::LightKind::Spot)
		{
			changed |= fields::number("Inner cone", spec.innerConeDegrees, 0.25, 0.0, 180.0, "%.1f°");
			changed |= fields::number("Outer cone", spec.outerConeDegrees, 0.25, 0.0, 180.0, "%.1f°");
		}
		return changed;
	}

	// The editor `selection` names, or a word that it has none.
	static bool edit(AppContext& ctx)
	{
		rp::RigPlan& p = ctx.document.plan;
		const Selection selection = ctx.selection;
		switch (selection.kind)
		{
			case Selected::Nothing:
				gui::TextDisabled("Select something in the Outliner.");
				return false;
			case Selected::Plan:
				return plan(ctx);
			case Selected::Room:
				return p.room && room(*p.room);
			case Selected::Mount:
				if (rp::Mount* m = byId(p.mounts, selection.id))
					return mount(ctx, *m);
				return false;
			case Selected::Camera:
				if (rp::PlacedCamera* c = byId(p.cameras, selection.id))
					return camera(ctx, *c);
				return false;
			case Selected::Light:
				if (rp::PlacedLight* l = byId(p.lights, selection.id))
					return light(ctx, *l);
				return false;
			case Selected::Plane:
				if (rp::Plane* pl = byId(p.planes, selection.id))
					return plane(ctx, *pl);
				return false;
			case Selected::Box:
				if (rp::Box* b = byId(p.boxes, selection.id))
					return box(ctx, *b);
				return false;
			case Selected::Prop:
			case Selected::Key:
				// A key is edited in its mover's table, with its row marked.
				if (rp::Prop* pr = byId(p.props, selection.id))
					return prop(ctx, *pr);
				[[fallthrough]];
			case Selected::Board:
				if (rp::BoardMover* b = byId(p.boards, selection.id))
					return board(ctx, *b);
				return false;
			case Selected::Specification:
				switch (selection.specification)
				{
					case SpecificationKind::Sensor:
						if (rp::SensorSpecification* s = byName(p.specifications.sensors, selection.name))
							return sensor(ctx, *s);
						return false;
					case SpecificationKind::Lens:
						if (rp::LensSpecification* l = byName(p.specifications.lenses, selection.name))
							return lens(ctx, *l);
						return false;
					case SpecificationKind::Camera:
						if (rp::CameraSpecification* c = byName(p.specifications.cameras, selection.name))
							return cameraSpecification(ctx, *c);
						return false;
					case SpecificationKind::Light:
						if (rp::LightSpecification* l = byName(p.specifications.lights, selection.name))
							return lightSpecification(ctx, *l);
						return false;
				}
				return false;
		}
		return false;
	}

	void InspectorPane::draw(AppContext& ctx)
	{
		if (gui::Begin("Inspector"))
		{
			gui::PushItemWidth(gui::GetContentRegionAvail().x * 0.6f);
			if (edit(ctx))
				ctx.changed();
			gui::PopItemWidth();
		}
		gui::End();
	}
} // namespace rigplan
