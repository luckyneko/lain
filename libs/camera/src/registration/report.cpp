#include "lain/camera/registration/report.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <string>

namespace lain::camera::registration
{
	std::optional<math::RigidTransformd> Report::referenceFromCamera(const capture::CameraIdentity& camera) const
	{
		for (const RegisteredCamera& c : cameras)
		{
			if (c.camera == camera)
				return c.referenceFromCamera;
		}
		return std::nullopt;
	}

	std::string Report::toString() const
	{
		if (status != RegistrationStatus::Succeeded)
		{
			if (failures.empty())
				return "Failed";
			return lain::string::format("Failed: {} ({})", meta::enums::name(failures.front().failure),
										failures.front().detail);
		}

		std::string text = lain::string::format("{}: {} cameras relative to {}, {}", meta::enums::name(verdict),
												cameras.size(), reference ? reference->value : std::string("?"),
												scale.scale == Scale::Metric ? "metric" : "arbitrary scale");
		if (const auto* held = std::get_if<HeldOutEvidence>(&heldOut))
			text += lain::string::format(", held out {:.3g} mrad RMS over {} groups", held->rmsAngle * 1000.0, held->groups);
		else
			text += lain::string::format(", no held-out evidence ({})", std::get<Unavailable>(heldOut).reason);
		text += lain::string::format(", {} of {} groups usable", diagnostics.groupsUsable, diagnostics.groupsExamined);
		if (!diagnostics.outliers.empty())
			text += lain::string::format(", {} outliers", diagnostics.outliers.size());
		// What stood between this verdict and a better one, which is the first thing a person with a
		// Rejected or Exploratory report wants to know.
		if (verdict != Verdict::Ready && !fitnessNotes.empty())
			text += lain::string::format("; short of the next verdict: {}", fitnessNotes.front());
		return text;
	}
} // namespace lain::camera::registration
