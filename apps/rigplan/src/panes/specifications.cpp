#include "specifications.h"

#include <lain/gui/color.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/gui.h>
#include <lain/string/format.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rigplan
{
	using namespace lain;
	using lain::string::format;
	namespace fs = std::filesystem;
	namespace rp = lain::rigplan;

	static constexpr const char* kPicker = "Copy In from Catalogue";
	static constexpr const char* kClash = "Already in the Catalogue";
	static const image::ColorRGBA8 kError{230, 90, 80, 255}; // the Issues pane's

	// The kinds in the order the picker offers them, a camera specification first, since it brings its
	// sensor and lens with it: the order arrivals() names them in.
	static constexpr std::array<SpecificationKind, 4> kKinds{SpecificationKind::Camera, SpecificationKind::Sensor, SpecificationKind::Lens,
															 SpecificationKind::Light};

	// A kind's heading, as the Outliner's groups name them.
	static const char* heading(SpecificationKind kind)
	{
		switch (kind)
		{
			case SpecificationKind::Sensor:
				return "Sensors";
			case SpecificationKind::Lens:
				return "Lenses";
			case SpecificationKind::Camera:
				return "Cameras";
			case SpecificationKind::Light:
				return "Lights";
		}
		return "Specifications";
	}

	template <typename T>
	static std::vector<std::string> namesOf(const std::vector<T>& items)
	{
		std::vector<std::string> names;
		for (const T& item : items)
			names.push_back(item.name);
		return names;
	}

	// The names of `specifications`' specifications of `kind`, in their order.
	static std::vector<std::string> names(const rp::Specifications& specifications, SpecificationKind kind)
	{
		switch (kind)
		{
			case SpecificationKind::Sensor:
				return namesOf(specifications.sensors);
			case SpecificationKind::Lens:
				return namesOf(specifications.lenses);
			case SpecificationKind::Camera:
				return namesOf(specifications.cameras);
			case SpecificationKind::Light:
				return namesOf(specifications.lights);
		}
		return {};
	}

	// "a", "a and b", "a, b and c".
	static std::string sentence(const std::vector<std::string>& parts)
	{
		std::string out;
		for (std::size_t i = 0; i < parts.size(); ++i)
		{
			if (i > 0)
				out += i + 1 == parts.size() ? " and " : ", ";
			out += parts[i];
		}
		return out;
	}

	// What a catalogue is, for the native panels; "All files" beside it, as Open... has.
	static std::vector<gui::FileFilter> catalogueFilters()
	{
		return {{"Specification catalogue", {"*.json"}}, {"All files", {"*"}}};
	}

	// How the picker lists one specification: a camera specification with what it is made of.
	static std::string listed(const rp::Specifications& catalogue, SpecificationKind kind, const std::string& name)
	{
		if (kind == SpecificationKind::Camera)
		{
			if (const rp::CameraSpecification* camera = rp::findCamera(catalogue, name))
				return format("{}   (sensor {}, lens {})", name, camera->sensor, camera->lens);
		}
		return name;
	}

	// What copying the picked specification in would come to, worked out on a copy of the plan, so the
	// picker says so before anything is copied and offers no copy that would be refused or change nothing.
	struct CopyPreview
	{
		bool canCopy = false;
		bool refused = false;
		std::string text;
	};

	static CopyPreview previewOf(const rp::RigPlan& plan, const CopyingIn& copying)
	{
		if (copying.name.empty())
			return {};
		rp::RigPlan trial = plan;
		const EditResult result = copyIn(trial, copying.catalogue, copying.kind, copying.name);
		if (!result.refusal.empty())
			return {false, true, result.refusal};
		if (!result.changed())
			return {false, false, "This plan has it already."};
		return {true, false, format("Brings {}.", sentence(arrivals(plan.specifications, trial.specifications)))};
	}

	// What a save out came to, said: saved, a clash held for the question, cancelled, or refused.
	static void report(AppContext& ctx, const fs::path& file, const rp::Specifications& specifications, SpecificationKind kind,
					   const std::string& name, SaveOutResult result)
	{
		switch (result.outcome)
		{
			case SaveOutResult::Outcome::Saved:
				ctx.note(format("Saved {} '{}' to {}{}", kindName(kind), name, file.filename().string(),
								result.savedAs == name ? std::string() : format(" as '{}'", result.savedAs)));
				return;
			case SaveOutResult::Outcome::Clash:
				ctx.savingOut = SavingOut{file, specifications, kind, name, std::move(result.clashes), true};
				return;
			case SaveOutResult::Outcome::Cancelled:
				ctx.note(format("Nothing was saved to {}", file.filename().string()));
				return;
			case SaveOutResult::Outcome::Refused:
				ctx.note(result.refusal, true);
				return;
		}
	}

	bool SpecificationsMenu::beginCopyIn(AppContext& ctx, const fs::path& file)
	{
		CatalogueFileRead read = readCatalogue(file);
		if (!read.specifications)
		{
			ctx.note(read.problems.empty() ? format("{} does not read as a catalogue", file.filename().string()) : read.problems.front(), true);
			return false;
		}
		CopyingIn copying;
		copying.file = file;
		copying.catalogue = std::move(*read.specifications);
		for (SpecificationKind kind : kKinds)
		{
			const std::vector<std::string> those = names(copying.catalogue, kind);
			if (!those.empty())
			{
				copying.kind = kind;
				copying.name = those.front();
				break;
			}
		}
		ctx.copyingIn = std::move(copying);
		return true;
	}

	void SpecificationsMenu::copyPicked(AppContext& ctx)
	{
		if (!ctx.copyingIn)
			return;
		const CopyingIn copying = std::move(*ctx.copyingIn);
		ctx.copyingIn.reset();
		if (copying.name.empty())
			return;
		const rp::Specifications before = ctx.document.plan.specifications;
		const EditResult result = copyIn(ctx.document.plan, copying.catalogue, copying.kind, copying.name);
		ctx.apply(result);
		if (result.changed())
			ctx.note(format("Copied in {} from {}", sentence(arrivals(before, ctx.document.plan.specifications)), copying.file.filename().string()));
		else if (result.refusal.empty())
		{
			// Everything it brings is the plan's already, under the same name: that one is what was asked for.
			ctx.selection = Selection::specificationOf(copying.kind, copying.name);
			ctx.note(format("This plan has {} '{}' already", kindName(copying.kind), copying.name));
		}
	}

	void SpecificationsMenu::saveSelected(AppContext& ctx, const fs::path& file, CatalogueTarget target)
	{
		const Selection selected = ctx.selection;
		if (selected.kind != Selected::Specification || !exists(ctx.document.plan, selected))
		{
			ctx.note("Select a specification in the Outliner to save it", true);
			return;
		}
		fs::path path = file;
		if (target == CatalogueTarget::New)
		{
			path.replace_extension("json"); // the codec is chosen by the extension
			// The save panel asked whether to replace a file there, and a catalogue is never replaced: one
			// there is added to through Save to Catalogue..., and anything else there is left alone.
			std::error_code ec;
			if (fs::exists(path, ec))
			{
				ctx.note(format("{} is already there, so nothing was written: Save to Catalogue... adds to a catalogue that is there",
								path.filename().string()),
						 true);
				return;
			}
		}
		const rp::Specifications& specifications = ctx.document.plan.specifications;
		report(ctx, path, specifications, selected.specification, selected.name,
			   saveCatalogue(path, specifications, selected.specification, selected.name, std::nullopt));
	}

	void SpecificationsMenu::answerClash(AppContext& ctx, ClashAnswer answer)
	{
		if (!ctx.savingOut)
			return;
		const SavingOut saving = std::move(*ctx.savingOut);
		ctx.savingOut.reset();
		report(ctx, saving.file, saving.specifications, saving.kind, saving.name,
			   saveCatalogue(saving.file, saving.specifications, saving.kind, saving.name, answer));
	}

	void SpecificationsMenu::draw(AppContext& ctx)
	{
		if (!gui::BeginMenu("Specifications"))
			return;
		if (gui::MenuItem("Copy In from Catalogue..."))
		{
			if (const auto picked = gui::openFile("Copy in from which catalogue?", {}, catalogueFilters()))
				beginCopyIn(ctx, *picked);
		}
		gui::Separator();
		// The items name what they save; their ids are fixed, since their labels follow the selection.
		const Selection& selected = ctx.selection;
		const bool canSave = selected.kind == Selected::Specification && exists(ctx.document.plan, selected);
		const std::string what = canSave ? format(" {} '{}'", kindName(selected.specification), selected.name) : std::string();
		const auto explain = [&]
		{
			if (!canSave && gui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				gui::SetTooltip("Select a specification in the Outliner to save it");
		};
		if (gui::MenuItem(format("Save{} to Catalogue...###save", what).c_str(), nullptr, false, canSave))
		{
			if (const auto picked = gui::openFile("Save to which catalogue?", {}, catalogueFilters()))
				saveSelected(ctx, *picked, CatalogueTarget::Existing);
		}
		explain();
		if (gui::MenuItem(format("Save{} to New Catalogue...###saveNew", what).c_str(), nullptr, false, canSave))
		{
			if (const auto picked = gui::saveFile("Save to a new catalogue", {}, {{"json", {"*.json"}}}))
				saveSelected(ctx, *picked, CatalogueTarget::New);
		}
		explain();
		gui::EndMenu();
	}

	// The picker: the kinds, the catalogue's specifications of the one chosen, what copying the one picked
	// would bring, and Copy In (or a double click) and Cancel.
	static void drawPicker(SpecificationsMenu& menu, AppContext& ctx)
	{
		if (ctx.copyingIn && ctx.copyingIn->ask)
		{
			gui::OpenPopup(kPicker);
			ctx.copyingIn->ask = false;
		}
		if (!gui::BeginPopupModal(kPicker, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			// Closed without an answer, by a question opened over it: let it go, so it is not left waiting.
			if (ctx.copyingIn && !ctx.copyingIn->ask && !gui::IsPopupOpen(kPicker))
				ctx.copyingIn.reset();
			return;
		}
		if (!ctx.copyingIn)
		{
			gui::CloseCurrentPopup();
			gui::EndPopup();
			return;
		}
		CopyingIn& copying = *ctx.copyingIn;
		const float width = 24.0f * gui::GetFontSize();

		gui::Text("From %s", copying.file.filename().string().c_str());
		if (gui::IsItemHovered())
			gui::SetTooltip("%s", copying.file.string().c_str());
		for (std::size_t k = 0; k < kKinds.size(); ++k)
		{
			const SpecificationKind kind = kKinds[k];
			const std::vector<std::string> those = names(copying.catalogue, kind);
			if (k > 0)
				gui::SameLine();
			if (gui::RadioButton(format("{} ({})", heading(kind), those.size()).c_str(), copying.kind == kind) && copying.kind != kind)
			{
				copying.kind = kind;
				copying.name = those.empty() ? std::string() : those.front();
			}
		}

		bool copyNow = false;
		const std::vector<std::string> those = names(copying.catalogue, copying.kind);
		if (those.empty())
			gui::TextDisabled("This catalogue has none of these.");
		else if (gui::BeginListBox("##specifications", {width, 8.0f * gui::GetTextLineHeightWithSpacing()}))
		{
			for (std::size_t i = 0; i < those.size(); ++i)
			{
				gui::PushID(int(i));
				if (gui::Selectable(listed(copying.catalogue, copying.kind, those[i]).c_str(), those[i] == copying.name,
									ImGuiSelectableFlags_AllowDoubleClick))
				{
					copying.name = those[i];
					copyNow = gui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				}
				gui::PopID();
			}
			gui::EndListBox();
		}

		const CopyPreview preview = previewOf(ctx.document.plan, copying);
		gui::PushTextWrapPos(gui::GetCursorPosX() + width);
		if (preview.refused)
		{
			gui::PushStyleColor(ImGuiCol_Text, gui::packColor(kError));
			gui::TextUnformatted(preview.text.c_str());
			gui::PopStyleColor();
		}
		else if (!preview.text.empty())
			gui::TextUnformatted(preview.text.c_str());
		gui::PopTextWrapPos();

		gui::BeginDisabled(!preview.canCopy);
		const bool copy = gui::Button("Copy In") || (copyNow && preview.canCopy);
		gui::EndDisabled();
		gui::SameLine();
		const bool cancel = gui::Button("Cancel") || gui::IsKeyPressed(ImGuiKey_Escape, false);
		if (copy)
		{
			gui::CloseCurrentPopup();
			menu.copyPicked(ctx); // last: it lets go of what `copying` refers to
		}
		else if (cancel)
		{
			gui::CloseCurrentPopup();
			ctx.copyingIn.reset();
		}
		gui::EndPopup();
	}

	// The clash question: what the catalogue has differently, and Replace, Keep both and Cancel.
	static void drawClash(SpecificationsMenu& menu, AppContext& ctx)
	{
		if (ctx.savingOut && ctx.savingOut->ask)
		{
			gui::OpenPopup(kClash);
			ctx.savingOut->ask = false;
		}
		if (!gui::BeginPopupModal(kClash, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (ctx.savingOut && !ctx.savingOut->ask && !gui::IsPopupOpen(kClash))
				ctx.savingOut.reset();
			return;
		}
		if (!ctx.savingOut)
		{
			gui::CloseCurrentPopup();
			gui::EndPopup();
			return;
		}
		const SavingOut& saving = *ctx.savingOut;
		gui::PushTextWrapPos(gui::GetCursorPosX() + 26.0f * gui::GetFontSize());
		gui::TextUnformatted(format("{} has a different {}.", saving.file.filename().string(), sentence(saving.clashes)).c_str());
		gui::Spacing();
		gui::TextUnformatted("Replace overwrites the catalogue's. Keep both saves this plan's under a numbered name, the references "
							 "to it following. Cancel saves nothing.");
		gui::PopTextWrapPos();
		gui::Spacing();
		std::optional<ClashAnswer> answer;
		if (gui::Button("Replace"))
			answer = ClashAnswer::Replace;
		gui::SameLine();
		if (gui::Button("Keep both"))
			answer = ClashAnswer::KeepBoth;
		gui::SameLine();
		if (gui::Button("Cancel") || gui::IsKeyPressed(ImGuiKey_Escape, false))
			answer = ClashAnswer::Cancel;
		if (answer)
		{
			gui::CloseCurrentPopup();
			menu.answerClash(ctx, *answer); // last: it lets go of what `saving` refers to
		}
		gui::EndPopup();
	}

	void SpecificationsMenu::drawModals(AppContext& ctx)
	{
		drawPicker(*this, ctx);
		drawClash(*this, ctx);
	}
} // namespace rigplan
