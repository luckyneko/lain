#include "parameditors.h"

#include <lain/flow/example/comparenode.h> // Comparison — CompareNode's operator param
#include <lain/flow/portvalue.h>
#include <lain/gui/dialogs.h>
#include <lain/gui/enums.h>
#include <lain/gui/gui.h>
#include <lain/image/color.h>
#include <lain/image/colorspace.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/io/image/load.h>
#include <lain/io/sequence/open.h>
#include <lain/io/video/open.h> // videoExtensions — the one list of what counts as video
#include <lain/media/frameposition.h>
#include <lain/media/framesequence.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace flowview
{
	using namespace lain;

	// The slot's current T, or a default when it's empty (a boundary input not yet set edits from there).
	template <typename T>
	static T currentOr(const flow::PortValue& value, T fallback = T{})
	{
		return value.holds<T>() ? value.get<T>() : fallback;
	}

	// --- built-in editors (named static; each draws + writes, returns edited) -----------

	static bool editInt(const std::string& label, flow::PortValue& value)
	{
		int v = currentOr<int>(value);
		if (gui::DragInt(label.c_str(), &v))
		{
			value.set<int>(v);
			return true;
		}
		return false;
	}

	static bool editFloat(const std::string& label, flow::PortValue& value)
	{
		float v = currentOr<float>(value);
		if (gui::DragFloat(label.c_str(), &v, 0.01f))
		{
			value.set<float>(v);
			return true;
		}
		return false;
	}

	static bool editBool(const std::string& label, flow::PortValue& value)
	{
		bool v = currentOr<bool>(value);
		if (gui::Checkbox(label.c_str(), &v))
		{
			value.set<bool>(v);
			return true;
		}
		return false;
	}

	// A std::string InputText (the imgui_stdlib overload — no fixed edit buffer, so no length cap).
	// Sets `changedNow` and copies the edit into `out` on any per-frame change, so the caller can
	// persist the text live (the edit buffer is local, so an un-persisted keystroke would be lost).
	// Returns true only when editing FINISHED — Enter or focus loss — which is the recompute trigger,
	// so a file-loading value doesn't reopen the file every character.
	static bool textField(const char* label, const std::string& current, std::string& out, bool& changedNow)
	{
		std::string edit = current;
		gui::InputText(label, &edit);
		changedNow = edit != current;
		if (changedNow)
			out = std::move(edit);
		return gui::IsItemDeactivatedAfterEdit();
	}

	static bool editString(const std::string& label, flow::PortValue& value)
	{
		std::string edited;
		bool changed = false;
		const bool committed = textField(label.c_str(), currentOr<std::string>(value), edited, changed);
		if (changed)
			value.set<std::string>(std::move(edited)); // persist live so the text isn't lost
		return committed;							   // recompute only on commit
	}

	// The filters a "pick something to open" dialog offers when the value does not say what it is.
	// The video patterns are built from io::video::videoExtensions() — the seam's own list, so the
	// dialog cannot offer a different set from the opener that will be handed the result — and "All
	// files" is last because a footage uri is legitimately anything an opener claims. Before this the
	// only filter was Images, which hid every .mp4 from the path editor an openSequence node uses.
	static std::vector<gui::FileFilter> openableFilters()
	{
		std::vector<std::string> video;
		for (const std::string& extension : io::video::videoExtensions())
			video.push_back("*." + extension);
		return {{"Images", {"*.png", "*.jpg", "*.jpeg", "*.tif", "*.tiff"}},
				{"Video", std::move(video)},
				{"All files", {"*"}}};
	}

	static bool editPath(const std::string& label, flow::PortValue& value)
	{
		// A text field (type/paste, commit on Enter or focus loss — committing per keystroke would
		// reopen the file each character) beside a Browse… button that opens the native picker.
		// The path TYPE owns this editor, so it slots in without touching any node (ADR-0005).
		std::string edited;
		bool changed = false;
		const bool committed = textField(label.c_str(), currentOr<std::filesystem::path>(value).string(), edited, changed);
		if (changed)
			value.set<std::filesystem::path>(std::filesystem::path(std::move(edited)));

		// Picking commits immediately (the same recompute trigger as finishing a text edit).
		//
		// BOTH pick modes are offered, because a std::filesystem::path is legitimately either: an
		// image to load, or a folder to list. Nothing in the type says which, and the editor is
		// handed only (label, type, value) — so rather than guess, it lets the user say. The image
		// filter on "File..." stays a DEFAULT for the common case; a per-value filter hint is still
		// the refinement ADR-0005 named, and would replace both of these with one button that knows
		// what it is picking.
		bool browsed = false;
		gui::PushID(label.c_str());

		// The dialog wants a starting DIRECTORY, so drop the filename from the current path (passing
		// a file path breaks the macOS backend — it resolves it as a folder).
		const std::filesystem::path current = currentOr<std::filesystem::path>(value);
		const std::filesystem::path startDir = current.has_filename() ? current.parent_path() : current;

		gui::SameLine();
		if (gui::Button("File..."))
		{
			const auto picked = gui::openFile("Open file", startDir, openableFilters());
			if (picked)
			{
				value.set<std::filesystem::path>(*picked);
				browsed = true;
			}
		}

		gui::SameLine();
		if (gui::Button("Folder..."))
		{
			// A folder value has no filename to drop, so it opens at ITSELF when it already names one.
			const std::filesystem::path folderStart = current.empty() ? startDir : current;
			if (const auto picked = gui::selectFolder("Select folder", folderStart))
			{
				value.set<std::filesystem::path>(*picked);
				browsed = true;
			}
		}
		gui::PopID();
		return committed || browsed;
	}

	// An IMAGE is picked, not typed: the editor opens the native file dialog and loads what comes
	// back. It lives here rather than in the Interface pane — where it was an `if (type == image)`
	// branch beside the fall-through to this registry — because ADR-0005's rule is that the TYPE
	// chooses the widget, and a pane deciding it for one type is how a second type (a frame sequence)
	// ends up with no way to be bound at all.
	//
	// A failed load is said out loud rather than silently leaving the old value: the user picked a
	// file and nothing happened otherwise.
	static bool editImage(const std::string& label, flow::PortValue& value)
	{
		gui::PushID(label.c_str());
		bool bound = false;
		if (gui::Button("Bind file..."))
		{
			if (const auto path = gui::openFile("Open image", {}, {{"Images", {"*.png", "*.jpg", "*.jpeg", "*.tif", "*.tiff"}}}))
			{
				if (auto loaded = io::image::load(path->string()))
				{
					value.set<image::Image>(std::move(*loaded));
					bound = true;
				}
				else
				{
					gui::message("Load failed", "Could not load: " + path->string(), true);
				}
			}
		}
		gui::PopID();
		return bound;
	}

	// FOOTAGE is opened, and it is the one editor that offers both pick modes for a reason the type
	// states: io::sequence::open dispatches by what the uri IS — a video file by its extension, a
	// folder or a "shot.####.png" pattern structurally — so a sequence is legitimately either.
	//
	// It shows what is currently bound through FrameSequence::toString ("500 frames · 3840x2160 RGB8
	// BT709 · 24 fps") rather than a path, because a bound sequence is not a path: it may span
	// several sources, and the value on the pin no longer remembers what was typed to get it.
	static bool editFrameSequence(const std::string& label, flow::PortValue& value)
	{
		gui::TextDisabled("%s", value.holds<media::FrameSequence>()
									? value.get<media::FrameSequence>().toString().c_str()
									: "(nothing bound)");

		gui::PushID(label.c_str());
		const auto bind = [&](const std::optional<std::filesystem::path>& picked)
		{
			if (!picked)
				return false;
			if (auto sequence = io::sequence::open(picked->string()))
			{
				value.set<media::FrameSequence>(std::move(*sequence));
				return true;
			}
			// Said out loud: the refusal is a capability or a format question (a build with no video
			// codec answers exactly that), and silence would read as a broken button.
			gui::message("Open failed", "Could not open as footage: " + picked->string(), true);
			return false;
		};

		bool bound = false;
		if (gui::Button("File..."))
			bound = bind(gui::openFile("Open footage", {}, openableFilters()));
		gui::SameLine();
		if (gui::Button("Folder..."))
			bound = bind(gui::selectFolder("Select footage folder", {}));
		gui::PopID();
		return bound;
	}

	// A FRAME POSITION is a plain drag over its ordinal — deliberately not a slider bounded by the
	// sequence's length. An editor is handed only (label, type, value): it has no way to know WHICH
	// sequence this position indexes, and a bound taken from the wrong one is worse than none. That
	// bound is the per-value hint ADR-0005 names as a refinement, and belongs to the driving
	// transport this milestone defers.
	static bool editFramePosition(const std::string& label, flow::PortValue& value)
	{
		const media::FramePosition current = currentOr<media::FramePosition>(value);
		int position = static_cast<int>(current.value);
		if (!gui::DragInt(label.c_str(), &position, 1.0f, 0, 0, "frame %d"))
			return false;
		value.set<media::FramePosition>(media::FramePosition{static_cast<std::size_t>(std::max(0, position))});
		return true;
	}

	static bool editColorRGBf(const std::string& label, flow::PortValue& value)
	{
		const image::ColorRGBf color = currentOr<image::ColorRGBf>(value);
		float rgb[3] = {color.r, color.g, color.b};
		if (gui::ColorEdit3(label.c_str(), rgb))
		{
			value.set<image::ColorRGBf>(image::ColorRGBf(rgb[0], rgb[1], rgb[2]));
			return true;
		}
		return false;
	}

	// --- registry ----------------------------------------------------------------------

	void ParamEditors::add(std::type_index type, Editor editor)
	{
		m_editors[type] = std::move(editor);
	}

	bool ParamEditors::render(const std::string& label, std::type_index type, flow::PortValue& value) const
	{
		const auto it = m_editors.find(type);
		if (it != m_editors.end())
			return it->second(label, value);

		gui::Text("%s (no editor)", label.c_str()); // unregistered type: read-only note
		return false;
	}

	// An enum param edits as a combo over its own enumerators — the ONE editor shape that needs no
	// per-type code, since lain::meta::enums supplies the names. Convert's pixel format and colour
	// space are its first users, and they give gui::enumCombo a live caller again: it has had none
	// since the preview-size dropdown was retired on 2026-07-31, and an API kept alive only to
	// dogfood itself is the thing that retirement deliberately avoided.
	template <typename E>
	static bool editEnum(const std::string& label, flow::PortValue& value)
	{
		E current = value.holds<E>() ? value.get<E>() : E{};
		if (!gui::enumCombo(label.c_str(), current))
			return false;
		value.set<E>(current);
		return true;
	}

	void registerBuiltinParamEditors(ParamEditors& editors)
	{
		editors.add(typeid(int), &editInt);
		editors.add(typeid(float), &editFloat);
		editors.add(typeid(bool), &editBool);
		editors.add(typeid(std::string), &editString);
		editors.add(typeid(std::filesystem::path), &editPath);
		editors.add(typeid(image::ColorRGBf), &editColorRGBf);
		editors.add(typeid(image::Image), &editImage);
		editors.add(typeid(media::FrameSequence), &editFrameSequence);
		editors.add(typeid(media::FramePosition), &editFramePosition);
		editors.add(typeid(image::PixelFormat), &editEnum<image::PixelFormat>);
		editors.add(typeid(image::ColorSpace), &editEnum<image::ColorSpace>);
		// Compare's operator (M11). An example node's enum reaching this list is not a layering
		// slip: flowview OWNS the example scene, and sceneCodecs already names the same type on the
		// serialization side — the two have to agree about which params a document can carry.
		editors.add(typeid(flow::example::Comparison), &editEnum<flow::example::Comparison>);
	}
} // namespace flowview
