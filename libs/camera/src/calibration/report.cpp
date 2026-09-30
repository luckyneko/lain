#include "lain/camera/calibration/report.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <string>

namespace lain::camera::calibration
{
	std::string Report::toString() const
	{
		if (!model)
		{
			if (failures.empty())
				return "Failed";
			return lain::string::format("Failed: {} ({})", meta::enums::name(failures.front().failure),
										failures.front().detail);
		}

		const ImageGeometry& image = model->image();
		std::string text = lain::string::format("{}: {} at {}x{}", meta::enums::name(verdict),
												displayName(model->distortion()), image.width, image.height);
		if (const auto* held = std::get_if<HeldOutEvidence>(&heldOut))
			text += lain::string::format(", held out {:.3g} px RMS over {} views", held->rmsPixels, held->views);
		else
			text += lain::string::format(", no held-out evidence ({})", std::get<Unavailable>(heldOut).reason);
		text += lain::string::format(", {} views covering {:.0f}% of the image, {} of {} frames usable",
									 diagnostics.viewsSelected, diagnostics.coverage * 100.0, diagnostics.framesUsable,
									 diagnostics.framesExamined);
		// What stood between this verdict and a better one, which is the first thing a person with a
		// Rejected or Exploratory report wants to know.
		if (verdict != Verdict::Ready && !fitnessNotes.empty())
			text += lain::string::format("; short of the next verdict: {}", fitnessNotes.front());
		return text;
	}
} // namespace lain::camera::calibration
