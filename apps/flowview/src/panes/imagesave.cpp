#include "imagesave.h"

#include "../appcontext.h"

#include <lain/gui/dialogs.h>
#include <lain/gui/enums.h> // enumCombo — the Compression setting, labelled from the enum itself
#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/io/image/save.h> // save + writerRegistry + canEncode
#include <lain/log/log.h>

#include <algorithm>
#include <cstdint>
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
	static void saveImage(const image::Image& img, const std::string& key, const io::image::ImageWriterOptions& options)
	{
		const auto path = gui::saveFile("Save image", {}, {{key, {"*." + key}}});
		if (!path)
			return; // cancelled
		std::filesystem::path out = *path;
		out.replace_extension(key);
		if (io::image::save(lain::core::Uri::fromPath(out), img, options))
			log::info("flowview: saved image to {}", out.string());
		else
			gui::message("Save failed", "Couldn't write the file: " + out.string(), true);
	}

	void renderImageSave(AppContext& ctx, const PinKey& key, const image::Image& img)
	{
		const std::vector<std::string> formats = savableFormats(img);
		if (formats.empty())
		{
			// No registered format can HOLD this image (e.g. RGBA/float with only JPEG); an explicit
			// convert is the fix, not a silent degrade (ADR-0003).
			//
			// It says "hold" rather than "lossless", which it used to: with a quality control now
			// sitting a line below, one word cannot mean both canEncode's question (can this format
			// represent these pixels at all) and isLossy's (will encoding cost fidelity). A JPEG
			// answers yes to the first and yes to the second, so the old wording read as a
			// contradiction the moment the slider appeared.
			gui::TextDisabled("(no format here can hold this image — convert first)");
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
			saveImage(img, sel, ctx.saveOptions[key]);

		// How it encodes, under the gesture rather than in it: the common case is Save... with
		// whatever the codec does by default, and these two are what a caller reaches for when that
		// is not enough.
		io::image::ImageWriterOptions& options = ctx.saveOptions[key];
		gui::SetNextItemWidth(110.0f);
		gui::enumCombo("##compression", options.compression);
		if (gui::IsItemHovered())
			gui::SetTooltip("How hard to squeeze. Costs encode time, never a pixel.");

		// ASKED OF THE SEAM, not derived from the format key here: a quality slider on a lossless
		// format would be a control that does nothing, and which formats those are is the codec's
		// fact to state rather than this pane's to remember.
		if (io::image::isLossy(sel))
		{
			gui::SameLine();
			int quality = options.quality ? static_cast<int>(*options.quality) : 90;
			// While unset, the slider shows what it MEANS rather than a number: the handle has to sit
			// somewhere, and a number under an untouched slider would be this pane claiming to know
			// the codec's default. Moving it is what makes the request explicit.
			const char* display = options.quality ? "quality %d" : "codec default";
			gui::SetNextItemWidth(140.0f);
			if (gui::SliderInt("##quality", &quality, 1, 100, display))
				options.quality = static_cast<std::uint8_t>(quality);
		}
	}
} // namespace flowview
