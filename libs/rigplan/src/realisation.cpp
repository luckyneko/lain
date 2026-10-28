#include "lain/rigplan/realisation.h"

#include <lain/core/sha256.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <variant>

namespace lain::rigplan
{
	double unitDeviation(std::uint64_t seed, const core::Uuid& owner, std::string_view key)
	{
		// Versioned, so a change to how draws are made is a change of name rather than a silent
		// re-roll of every rig plan ever realised. Each field on its own line: a seed, a canonical uuid
		// and a document key contain no newline.
		std::string text = "lain-rigplan-draw/1\n";
		text += std::to_string(seed);
		text += '\n';
		text += owner.toString();
		text += '\n';
		text += key;
		text += '\n';
		const core::Sha256Digest digest = core::sha256(text);
		std::uint64_t bits = 0;
		for (std::size_t i = 0; i < 8; ++i)
			bits = (bits << 8) | digest.bytes[i];
		// 53 bits make every double in [0, 1) on a 2^-53 grid exactly, the same on every platform.
		return double(bits >> 11) * 0x1.0p-52 - 1.0;
	}

	math::RigidTransformd mountFromPlacement(const math::Vec3d& offset, const Aim& aim)
	{
		// At rest a camera looks along the mount's +Y with its image's down along -Z: the camera frame
		// re-expressed in the plan frame's axes.
		const math::Quatd rest = math::quat_cast(math::basisChange<double>(camera::kCameraFrame, kPlanFrame));
		// Roll about the rest forward axis (+Y), then tilt about +X, then pan about +Z, each about the
		// mount's own axes, so a pan never changes what tilt and roll mean.
		const math::Quatd aimed = math::angleAxis(math::radians(aim.panDegrees), math::Vec3d{0.0, 0.0, 1.0}) *
								  math::angleAxis(math::radians(aim.tiltDegrees), math::Vec3d{1.0, 0.0, 0.0}) *
								  math::angleAxis(math::radians(aim.rollDegrees), math::Vec3d{0.0, 1.0, 0.0});
		return math::RigidTransformd{aimed * rest, offset};
	}

	// Every coefficient times `factor`: a unit with the lens's shape and another strength.
	static camera::Distortion scaled(const camera::Distortion& distortion, double factor)
	{
		return std::visit(
			[&](const auto& model) -> camera::Distortion
			{
				using Model = std::decay_t<decltype(model)>;
				auto coefficients = model.coefficients();
				for (double& c : coefficients)
					c *= factor;
				return Model::fromCoefficients(coefficients);
			},
			distortion);
	}

	// One owner's draws: `deviation(key, tolerance)` is scale x draw x tolerance for that key.
	struct Draws
	{
		std::uint64_t seed;
		core::Uuid owner;
		double scale;

		double deviation(std::string_view key, double tolerance) const
		{
			return scale * unitDeviation(seed, owner, key) * tolerance;
		}
	};

	// A mount's frame moved `shift` from where it is planned and turned `yawShift` degrees further: the
	// one place a mount's pose is made, for a realisation and for the nominal layout. A pole stands on
	// the floor, so it moves only across it.
	static math::RigidTransformd mountPose(const Mount& mount, const math::Vec3d& shift, double yawShift)
	{
		math::Vec3d position{0.0};
		if (const auto* pole = std::get_if<Pole>(&mount.kind))
			position = math::Vec3d{pole->floor.x + shift.x, pole->floor.y + shift.y, 0.0};
		else
		{
			const math::Vec3d& planned = std::get<FixedPoint>(mount.kind).position;
			position = math::Vec3d{planned.x + shift.x, planned.y + shift.y, planned.z + shift.z};
		}
		const double yaw = mount.yawDegrees + yawShift;
		return math::RigidTransformd{math::angleAxis(math::radians(yaw), math::Vec3d{0.0, 0.0, 1.0}), position};
	}

	static math::RigidTransformd realisedMount(const Mount& mount, const Draws& draws)
	{
		const double positionTolerance = mount.tolerance.position.metres();
		const bool pole = std::holds_alternative<Pole>(mount.kind);
		const math::Vec3d shift{draws.deviation("position.x", positionTolerance), draws.deviation("position.y", positionTolerance),
								pole ? 0.0 : draws.deviation("position.z", positionTolerance)};
		return mountPose(mount, shift, draws.deviation("yaw", mount.tolerance.yawDegrees));
	}

	// A camera's optical centre on its mount: the mounting point at `offset`, aimed by `aim`, then the
	// optical centre from it, in the camera frame. The one place a camera's pose is made.
	static math::RigidTransformd cameraPose(const math::RigidTransformd& planFromMount, const math::Vec3d& offset, const Aim& aim,
											const math::Vec3d& opticalCentre)
	{
		return planFromMount * mountFromPlacement(offset, aim) * math::RigidTransformd{math::Quatd{1.0, 0.0, 0.0, 0.0}, opticalCentre};
	}

	RealisationResult realise(const RigPlan& plan)
	{
		RealisationResult result;
		for (const PlanDiagnostic& d : check(plan))
			result.diagnostics.push_back({RealisationProblem::InvalidPlan, d.detail});
		if (!result.diagnostics.empty())
			return result;

		const double intrinsic = plan.scales.intrinsic;
		const double installation = plan.scales.effectiveInstallation();

		Realisation realisation;
		std::unordered_map<core::Uuid, std::size_t> mountIndex;
		for (const Mount& mount : plan.mounts)
		{
			mountIndex.emplace(mount.id, realisation.mounts.size());
			realisation.mounts.push_back({mount.id, realisedMount(mount, Draws{plan.seed, mount.id, installation})});
		}

		for (const PlacedCamera& placed : plan.cameras)
		{
			// check() has established that every name resolves and every nominal model exists.
			const CameraSpecification& spec = *findCamera(plan.specifications, placed.camera);
			const SensorSpecification& sensor = *findSensor(plan.specifications, spec.sensor);
			const LensSpecification& lens = *findLens(plan.specifications, spec.lens);

			const Draws lensDraws{plan.seed, placed.id, intrinsic};
			camera::CameraModelParameters parameters = nominalModel(sensor, lens).model->parameters();
			const double focal = 1.0 + lensDraws.deviation("focalLength", lens.focalLengthTolerance);
			parameters.intrinsics.fx *= focal;
			parameters.intrinsics.fy *= focal;
			const double decentring = double(sensor.principalPointTolerance.nanometres()) / double(sensor.pixelPitch.nanometres());
			parameters.intrinsics.cx += lensDraws.deviation("principalPoint.x", decentring);
			parameters.intrinsics.cy += lensDraws.deviation("principalPoint.y", decentring);
			parameters.distortion =
				scaled(parameters.distortion, 1.0 + lensDraws.deviation("distortion", lens.distortionTolerance));

			camera::ModelResult model = camera::CameraModel::create(parameters);
			if (!model.model)
			{
				std::string why;
				for (const camera::ModelDiagnostic& d : model.diagnostics)
					why += (why.empty() ? "" : "; ") + d.detail;
				result.diagnostics.push_back({RealisationProblem::InvalidRealisedModel,
											  string::format("camera '{}' (specification '{}'): this build's draws make no camera model: {}",
															 placed.name, placed.camera, why)});
				continue; // report every camera that cannot be built, then refuse the whole build
			}

			const Draws placement{plan.seed, placed.id, installation};
			const double offsetTolerance = placed.tolerance.offset.metres();
			const math::Vec3d offset{placed.offset.x + placement.deviation("offset.x", offsetTolerance),
									 placed.offset.y + placement.deviation("offset.y", offsetTolerance),
									 placed.offset.z + placement.deviation("offset.z", offsetTolerance)};
			const Aim aim{placed.aim.panDegrees + placement.deviation("pan", placed.tolerance.panDegrees),
						  placed.aim.tiltDegrees + placement.deviation("tilt", placed.tolerance.tiltDegrees),
						  placed.aim.rollDegrees + placement.deviation("roll", placed.tolerance.rollDegrees)};
			const math::RigidTransformd& planFromMount = realisation.mounts[mountIndex.at(placed.mount)].planFromMount;
			const math::RigidTransformd planFromCamera = cameraPose(planFromMount, offset, aim, sensor.body.opticalCentre);
			realisation.cameras.push_back({placed.id, placed.name, std::move(*model.model), planFromCamera});
		}
		if (!result.diagnostics.empty())
			return result;

		for (const PlacedLight& placed : plan.lights)
		{
			const math::RigidTransformd& planFromMount = realisation.mounts[mountIndex.at(placed.mount)].planFromMount;
			realisation.lights.push_back({placed.id, planFromMount * mountFromPlacement(placed.offset, placed.aim)});
		}

		result.realisation = std::move(realisation);
		return result;
	}

	Aim aimOf(const math::Quatd& rotation)
	{
		// mountFromPlacement's rotation is Rz(pan) Rx(tilt) Ry(roll) times the rest turn, so undoing the
		// rest leaves A = Rz(pan) Rx(tilt) Ry(roll), whose entries (row, column) are
		//   A(2,1) = sin(tilt),
		//   A(0,1) = -sin(pan) cos(tilt),  A(1,1) = cos(pan) cos(tilt),
		//   A(2,0) = -cos(tilt) sin(roll), A(2,2) = cos(tilt) cos(roll).
		const math::Quatd rest = math::quat_cast(math::basisChange<double>(camera::kCameraFrame, kPlanFrame));
		const math::Mat3d a = math::mat3_cast(rotation * math::conjugate(rest)); // GLM: a[column][row]
		const double sinTilt = std::clamp(a[1][2], -1.0, 1.0);
		const double cosTilt = std::sqrt(std::max(0.0, 1.0 - sinTilt * sinTilt));
		Aim aim;
		aim.tiltDegrees = math::degrees(std::asin(sinTilt));
		if (cosTilt > 1e-9)
		{
			aim.panDegrees = math::degrees(std::atan2(-a[1][0], a[1][1]));
			aim.rollDegrees = math::degrees(std::atan2(-a[0][2], a[2][2]));
		}
		else
		{
			// Looking straight up or down: A(0,0) = cos(pan +- roll) and A(1,0) = sin(pan +- roll), so with no
			// roll the pan is that angle.
			aim.panDegrees = math::degrees(std::atan2(a[0][1], a[0][0]));
		}
		return aim;
	}

	NominalLayout nominalLayout(const RigPlan& plan)
	{
		const auto finite = [](const math::Vec3d& v)
		{ return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
		const auto finiteAim = [](const Aim& a)
		{ return std::isfinite(a.panDegrees) && std::isfinite(a.tiltDegrees) && std::isfinite(a.rollDegrees); };

		NominalLayout layout;
		std::unordered_map<core::Uuid, std::optional<math::RigidTransformd>> mounts;
		for (const Mount& mount : plan.mounts)
		{
			const bool pole = std::holds_alternative<Pole>(mount.kind);
			const bool placed = pole ? std::isfinite(std::get<Pole>(mount.kind).floor.x) && std::isfinite(std::get<Pole>(mount.kind).floor.y)
									 : finite(std::get<FixedPoint>(mount.kind).position);
			std::optional<math::RigidTransformd> pose;
			if (placed && std::isfinite(mount.yawDegrees))
				pose = mountPose(mount, math::Vec3d{0.0}, 0.0);
			mounts.emplace(mount.id, pose); // the first of two sharing an identity, as check() reports
			layout.mounts.push_back({mount.id, pose});
		}
		const auto mountOf = [&](const core::Uuid& id) -> std::optional<math::RigidTransformd>
		{
			const auto found = mounts.find(id);
			return found != mounts.end() ? found->second : std::nullopt;
		};

		for (const PlacedCamera& placed : plan.cameras)
		{
			NominalLayout::Camera camera{placed.id, placed.name, std::nullopt, std::nullopt};
			const CameraSpecification* specification = findCamera(plan.specifications, placed.camera);
			const SensorSpecification* sensor = specification ? findSensor(plan.specifications, specification->sensor) : nullptr;
			const LensSpecification* lens = specification ? findLens(plan.specifications, specification->lens) : nullptr;
			const std::optional<math::RigidTransformd> planFromMount = mountOf(placed.mount);
			if (planFromMount && finite(placed.offset) && finiteAim(placed.aim))
			{
				const math::Vec3d opticalCentre = sensor ? sensor->body.opticalCentre : math::Vec3d{0.0};
				camera.planFromCamera = cameraPose(*planFromMount, placed.offset, placed.aim, opticalCentre);
			}
			if (sensor && lens)
				camera.model = nominalModel(*sensor, *lens).model;
			layout.cameras.push_back(std::move(camera));
		}

		for (const PlacedLight& placed : plan.lights)
		{
			std::optional<math::RigidTransformd> pose;
			const std::optional<math::RigidTransformd> planFromMount = mountOf(placed.mount);
			if (planFromMount && finite(placed.offset) && finiteAim(placed.aim))
				pose = *planFromMount * mountFromPlacement(placed.offset, placed.aim);
			layout.lights.push_back({placed.id, pose});
		}
		return layout;
	}
} // namespace lain::rigplan
