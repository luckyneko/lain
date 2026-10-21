#include "lain/camera/registration/report.h"

#include "common.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lain::camera::registration
{
	// What a method alone adds to a report's one line. One overload per alternative of
	// Diagnostics::method, reached through std::visit, so a method without one does not compile.
	static std::string methodSummary(const BoardDiagnostics& board)
	{
		return lain::string::format(", {} of {} groups usable", board.groupsUsable, board.groupsExamined);
	}

	static std::string methodSummary(const TargetlessDiagnostics& targetless)
	{
		return lain::string::format(", {} landmarks from {} usable tracks", targetless.landmarks, targetless.tracksUsable);
	}

	// `camera`'s transform among `cameras`, which a report and a rig each hold.
	static std::optional<math::RigidTransformd> transformOf(const std::vector<RegisteredCamera>& cameras,
															const capture::CameraIdentity& camera)
	{
		for (const RegisteredCamera& c : cameras)
		{
			if (c.camera == camera)
				return c.referenceFromCamera;
		}
		return std::nullopt;
	}

	std::optional<math::RigidTransformd> RegisteredRig::referenceFromCamera(const capture::CameraIdentity& camera) const
	{
		return transformOf(cameras, camera);
	}

	std::string RegisteredRig::toString() const
	{
		const bool metric = scale.scale == Scale::Metric;
		std::string text = lain::string::format("{} cameras relative to {}, {}", cameras.size(), reference.value,
												metric ? "metric" : "arbitrary scale");
		// Where each camera's centre is in the reference's frame, which is its transform's translation,
		// and its rotation from the reference as one angle.
		for (const RegisteredCamera& c : cameras)
		{
			const math::Vec3d& centre = c.referenceFromCamera.translation();
			text += lain::string::format("; {} at ({:.4g}, {:.4g}, {:.4g}){} turned {:.3g} deg", c.camera.value, centre.x,
										 centre.y, centre.z, metric ? " m" : "",
										 math::degrees(detail::rotationBetween(math::RigidTransformd{}, c.referenceFromCamera)));
		}
		return text;
	}

	std::optional<math::RigidTransformd> Report::referenceFromCamera(const capture::CameraIdentity& camera) const
	{
		return transformOf(cameras, camera);
	}

	std::optional<RegisteredRig> Report::rig() const
	{
		if (status != RegistrationStatus::Succeeded || !reference)
			return std::nullopt;
		return RegisteredRig{*reference, scale, cameras};
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
		{
			text += lain::string::format(", held out {:.3g} mrad (median-based; RMS {:.3g}) over {} {}",
										 held->robustAngle * 1000.0, held->rmsAngle * 1000.0, held->units,
										 detail::unitsWord(thresholds.unit));
		}
		else
			text += lain::string::format(", no held-out evidence ({})", std::get<Unavailable>(heldOut).reason);
		text += std::visit([](const auto& method)
						   { return methodSummary(method); }, diagnostics.method);
		if (!diagnostics.outliers.empty())
			text += lain::string::format(", {} outliers", diagnostics.outliers.size());
		// What stood between this verdict and a better one, which is the first thing a person with a
		// Rejected or Exploratory report wants to know.
		if (verdict != Verdict::Ready && !fitnessNotes.empty())
			text += lain::string::format("; short of the next verdict: {}", fitnessNotes.front());
		return text;
	}
} // namespace lain::camera::registration
