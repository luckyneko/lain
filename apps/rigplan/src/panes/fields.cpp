#include "fields.h"

#include <lain/gui/gui.h>
#include <lain/io/image/load.h> // readerRegistry: what a picture can be
#include <lain/io/mesh/load.h>	// readerRegistry: what a mesh can be

#include <cstddef>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace rigplan::fields
{
	using namespace lain;
	namespace rp = lain::rigplan;

	bool metres(const char* label, double& value, double speed)
	{
		return gui::DragScalar(label, ImGuiDataType_Double, &value, float(speed), nullptr, nullptr, "%.3f m");
	}

	bool metres(const char* label, math::Vec2d& value, double speed)
	{
		return gui::DragScalarN(label, ImGuiDataType_Double, &value.x, 2, float(speed), nullptr, nullptr, "%.3f m");
	}

	bool metres(const char* label, math::Vec3d& value, double speed)
	{
		return gui::DragScalarN(label, ImGuiDataType_Double, &value.x, 3, float(speed), nullptr, nullptr, "%.3f m");
	}

	bool degrees(const char* label, double& value)
	{
		return gui::DragScalar(label, ImGuiDataType_Double, &value, 0.25f, nullptr, nullptr, "%.2f°");
	}

	bool aim(rp::Aim& aim)
	{
		bool changed = degrees("Pan", aim.panDegrees);
		changed |= degrees("Tilt", aim.tiltDegrees);
		changed |= degrees("Roll", aim.rollDegrees);
		return changed;
	}

	// `value` in the unit `Unit`, edited as a double and kept to the nanometre.
	template <typename Unit>
	static bool length(const char* label, core::Length& value, float speed, const char* format)
	{
		double shown = value.as<Unit>();
		if (!gui::DragScalar(label, ImGuiDataType_Double, &shown, speed, nullptr, nullptr, format))
			return false;
		value = core::Length::from<Unit>(shown);
		return true;
	}

	bool millimetres(const char* label, core::Length& value)
	{
		return length<core::Length::Millimetres>(label, value, 0.01f, "%.3f mm");
	}

	bool micrometres(const char* label, core::Length& value)
	{
		return length<core::Length::Micrometres>(label, value, 0.01f, "%.3f um");
	}

	bool percent(const char* label, double& fraction, double maximum)
	{
		double shown = fraction * 100.0;
		const double lowest = 0.0;
		const double highest = maximum * 100.0;
		if (!gui::DragScalar(label, ImGuiDataType_Double, &shown, 0.05f, &lowest, &highest, "%.2f %%", ImGuiSliderFlags_AlwaysClamp))
			return false;
		fraction = shown / 100.0;
		return true;
	}

	bool number(const char* label, double& value, double speed, double minimum, double maximum, const char* format)
	{
		return gui::DragScalar(label, ImGuiDataType_Double, &value, float(speed), &minimum, &maximum, format, ImGuiSliderFlags_AlwaysClamp);
	}

	bool count(const char* label, std::uint32_t& value, std::uint32_t minimum, std::uint32_t maximum)
	{
		return gui::DragScalar(label, ImGuiDataType_U32, &value, 0.1f, &minimum, &maximum, "%u", ImGuiSliderFlags_AlwaysClamp);
	}

	bool count(const char* label, std::size_t& value, std::size_t minimum)
	{
		std::uint64_t shown = value;
		const std::uint64_t lowest = minimum;
		const std::uint64_t highest = std::numeric_limits<std::uint32_t>::max();
		if (!gui::DragScalar(label, ImGuiDataType_U64, &shown, 0.1f, &lowest, &highest, "%llu", ImGuiSliderFlags_AlwaysClamp))
			return false;
		value = std::size_t(shown);
		return true;
	}

	bool seed(const char* label, std::uint64_t& value)
	{
		return gui::InputScalar(label, ImGuiDataType_U64, &value);
	}

	bool colour(const char* label, image::ColorRGBf& value)
	{
		return gui::ColorEdit3(label, &value.r, ImGuiColorEditFlags_Float);
	}

	bool pick(const char* label, std::string& value, const std::vector<std::string>& names)
	{
		bool changed = false;
		if (gui::BeginCombo(label, value.c_str()))
		{
			for (const std::string& name : names)
			{
				const bool selected = name == value;
				if (gui::Selectable(name.c_str(), selected) && !selected)
				{
					value = name;
					changed = true;
				}
				if (selected)
					gui::SetItemDefaultFocus();
			}
			gui::EndCombo();
		}
		return changed;
	}

	bool pickMount(const char* label, core::Uuid& value, const rp::RigPlan& plan)
	{
		std::string current = "(none)";
		for (const rp::Mount& mount : plan.mounts)
		{
			if (mount.id == value)
				current = mount.name;
		}
		bool changed = false;
		if (gui::BeginCombo(label, current.c_str()))
		{
			for (const rp::Mount& mount : plan.mounts)
			{
				gui::PushID(mount.id.toString().c_str()); // two mounts may share a name
				const bool selected = mount.id == value;
				if (gui::Selectable(mount.name.c_str(), selected) && !selected)
				{
					value = mount.id;
					changed = true;
				}
				gui::PopID();
			}
			gui::EndCombo();
		}
		return changed;
	}

	bool file(const char* label, core::Uri& value, const std::string& title, const std::vector<gui::FileFilter>& filters)
	{
		gui::PushID(label);
		bool changed = false;
		// Committed when editing ends, so a half-typed path is never read as the file.
		std::string text = value.toString();
		gui::SetNextItemWidth(gui::GetContentRegionAvail().x * 0.65f);
		gui::InputText("##uri", &text);
		if (gui::IsItemDeactivatedAfterEdit() && text != value.toString())
		{
			value = core::Uri{text};
			changed = true;
		}
		gui::SameLine();
		if (gui::Button("Pick..."))
		{
			if (const auto picked = gui::openFile(title, gui::lastDirectory(), filters))
			{
				value = core::Uri::fromPath(*picked);
				changed = true;
			}
		}
		gui::SameLine();
		gui::TextUnformatted(label);
		gui::PopID();
		return changed;
	}

	template <typename Factory>
	static std::vector<gui::FileFilter> filtersOf(const char* name, const Factory& registry)
	{
		std::vector<std::string> patterns;
		for (const std::string& key : registry.keys())
			patterns.push_back("*." + key);
		return {{name, std::move(patterns)}, {"All files", {"*"}}};
	}

	std::vector<gui::FileFilter> pictureFilters()
	{
		return filtersOf("Pictures", io::image::readerRegistry());
	}

	std::vector<gui::FileFilter> meshFilters()
	{
		return filtersOf("Meshes", io::mesh::readerRegistry());
	}

	bool material(rp::Material& material)
	{
		bool changed = false;
		static const char* const kKinds[] = {"Colour", "Picture", "Procedural"};
		int kind = int(material.surface.index());
		if (gui::Combo("Surface", &kind, kKinds, 3) && kind != int(material.surface.index()))
		{
			if (kind == 0)
				material.surface = rp::SolidColour{};
			else if (kind == 1)
				material.surface = rp::ImageTexture{};
			else
				material.surface = image::ProceduralTexture{};
			changed = true;
		}
		if (auto* solid = std::get_if<rp::SolidColour>(&material.surface))
			changed |= colour("Colour", solid->colour);
		else if (auto* picture = std::get_if<rp::ImageTexture>(&material.surface))
		{
			changed |= file("Picture", picture->image, "Choose a picture", pictureFilters());
			changed |= metres("Tile", picture->tile);
		}
		else if (auto* procedural = std::get_if<image::ProceduralTexture>(&material.surface))
		{
			changed |= seed("Seed", procedural->seed);
			changed |= number("Cell", procedural->cell, 0.001, 0.001, 10.0, "%.3f m");
		}
		changed |= colour("Specular", material.specular);
		changed |= number("Shininess", material.shininess, 0.5, 0.0, 10000.0, "%.1f");
		return changed;
	}

	// The model combo: every alternative of camera::Distortion, by its display name. A new model starts
	// with every coefficient zero, which is the model's own idea of no distortion.
	template <std::size_t... I>
	static bool distortionModel(camera::Distortion& value, std::index_sequence<I...>)
	{
		bool changed = false;
		const std::string current(std::visit([](const auto& model)
											 { return std::decay_t<decltype(model)>::kDisplayName; }, value));
		if (gui::BeginCombo("Model", current.c_str()))
		{
			const auto option = [&](auto model)
			{
				using Model = decltype(model);
				const bool selected = std::holds_alternative<Model>(value);
				if (gui::Selectable(std::string(Model::kDisplayName).c_str(), selected) && !selected)
				{
					value = Model::fromCoefficients(typename Model::template Coefficients<double>{});
					changed = true;
				}
			};
			(option(std::variant_alternative_t<I, camera::Distortion>{}), ...);
			gui::EndCombo();
		}
		return changed;
	}

	bool distortion(camera::Distortion& value)
	{
		bool changed = distortionModel(value, std::make_index_sequence<std::variant_size_v<camera::Distortion>>{});
		changed |= std::visit(
			[&](auto& model)
			{
				using Model = std::decay_t<decltype(model)>;
				auto coefficients = model.coefficients();
				bool edited = false;
				for (std::size_t i = 0; i < coefficients.size(); ++i)
				{
					const std::string name(Model::kCoefficientNames[i]);
					edited |= gui::DragScalar(name.c_str(), ImGuiDataType_Double, &coefficients[i], 0.0005f, nullptr, nullptr, "%.6f");
				}
				if (edited)
					model = Model::fromCoefficients(coefficients);
				return edited;
			},
			value);
		return changed;
	}

	void identity(const core::Uuid& id)
	{
		gui::TextDisabled("%s", id.toString().c_str());
		if (gui::IsItemHovered())
			gui::SetTooltip("Its identity: what its draws are keyed by, so a copy draws as another unit (ADR-0029)");
	}
} // namespace rigplan::fields
