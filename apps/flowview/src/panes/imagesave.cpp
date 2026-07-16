#include "imagesave.h"

#include "../appcontext.h"

#include <lain/gui/dialogs.h>
#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/io/image/save.h> // save + writerRegistry + canEncode
#include <lain/log/log.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace flowview
{
	using namespace lain;

	// The registered write formats that can encode `img` WITHOUT loss — the writer keys
	// (alphabetical) filtered by io::image::canEncode. This is the format dropdown's contents, so a
	// format that would degrade the image (JPEG + alpha, any + float) simply isn't offered.
	static std::vector<std::string> savableFormats(const image::Image& img)
	{
		std::vector<std::string> out;
		for (const std::string& key : io::image::writerRegistry().keys())
			if (io::image::canEncode(key, img))
				out.push_back(key);
		return out;
	}

	// Write `img` as `key` (a savable format from the dropdown): pick where + name via the native
	// dialog, force the chosen format's extension (the dropdown is authoritative), and save. img is
	// read synchronously while the (blocking) dialog holds the main thread, so the port ref stays
	// valid. Success logs the path; a write failure raises an alert.
	static void saveImage(const image::Image& img, const std::string& key)
	{
		const auto path = gui::saveFile("Save image", {}, {{key, {"*." + key}}});
		if (!path)
			return; // cancelled
		std::filesystem::path out = *path;
		out.replace_extension(key);
		if (io::image::save(out.string(), img))
			log::info("flowview: saved image to {}", out.string());
		else
			gui::message("Save failed", "Couldn't write the file: " + out.string(), true);
	}

	void renderImageSave(AppContext& ctx, const PinKey& key, const image::Image& img)
	{
		const std::vector<std::string> formats = savableFormats(img);
		if (formats.empty())
		{
			// Nothing can store this image losslessly (e.g. RGBA/float with only JPEG); an
			// explicit convert is the fix, not a silent degrade (ADR-0003).
			gui::TextDisabled("(no lossless format — convert first)");
			return;
		}
		// The chosen format for this pin, defaulting to (and falling back to) the first savable
		// when unset or no longer offered.
		std::string& sel = ctx.saveFormat[key];
		if (std::find(formats.begin(), formats.end(), sel) == formats.end())
			sel = formats.front();
		gui::SetNextItemWidth(80.0f);
		if (gui::BeginCombo("##fmt", sel.c_str()))
		{
			for (const std::string& f : formats)
				if (gui::Selectable(f.c_str(), f == sel))
					sel = f;
			gui::EndCombo();
		}
		gui::SameLine();
		if (gui::Button("Save..."))
			saveImage(img, sel);
	}
} // namespace flowview
