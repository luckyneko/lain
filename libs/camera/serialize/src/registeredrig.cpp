#include "lain/camera/serialize/registeredrig.h"

#include "versioned.h"

#include <lain/data/data.h>
#include <lain/data/strict.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace lain::camera::registration
{
	// Enums reflect as their names ("Metric", "Arbitrary").
	LAIN_SERIALIZE(ScaleStatus, scale, evidence)

	// A rig document's parts with nothing checked, as the document holds them: what is read before
	// registeredRigFromValue judges it, as CameraModelParameters is for a camera model. Private, since a
	// caller has RegisteredRig: a camera identity is its string here, and a transform its two parts.
	struct RigRotation
	{
		double w = 1;
		double x = 0;
		double y = 0;
		double z = 0;
	};
	LAIN_SERIALIZE(RigRotation, w, x, y, z)

	struct RigTranslation
	{
		double x = 0;
		double y = 0;
		double z = 0;
	};
	LAIN_SERIALIZE(RigTranslation, x, y, z)

	struct RigTransform
	{
		RigRotation rotation;
		RigTranslation translation;
	};
	LAIN_SERIALIZE(RigTransform, rotation, translation)

	struct RigDocumentCamera
	{
		std::string camera;
		RigTransform referenceFromCamera;
	};
	LAIN_SERIALIZE(RigDocumentCamera, camera, referenceFromCamera)

	struct RigDocument
	{
		std::string reference;
		ScaleStatus scale;
		std::vector<RigDocumentCamera> cameras;
	};
	LAIN_SERIALIZE(RigDocument, reference, scale, cameras)

	// How far a rotation's norm may be from 1, and the reference's transform from the identity, before a
	// reader refuses it: RigidTransform::fromMatrix's tolerance.
	static constexpr double kTolerance = 1e-6;

	// {version, reference, scale, cameras}.
	static data::Value documentOf(const RigDocument& parts)
	{
		return versionedDocument(kRegisteredRigDocumentVersion, data::toValue(parts));
	}

	data::Value registeredRigToValue(const RegisteredRig& rig)
	{
		RigDocument parts{rig.reference.value, rig.scale, {}};
		for (const RegisteredCamera& camera : rig.cameras)
		{
			const math::Quatd& r = camera.referenceFromCamera.rotation();
			const math::Vec3d& t = camera.referenceFromCamera.translation();
			parts.cameras.push_back({camera.camera.value, {{r.w, r.x, r.y, r.z}, {t.x, t.y, t.z}}});
		}
		return documentOf(parts);
	}

	RegisteredRigRead registeredRigFromValue(const data::Value& document)
	{
		RegisteredRigRead result;
		result.problems = versionProblems(document, "rig", kRegisteredRigDocumentVersion);
		if (!result.problems.empty())
			return result;

		const std::optional<RigDocument> parts = data::fromValue<RigDocument>(document);
		if (!parts)
		{
			result.problems.push_back("the document is not a rig");
			return result;
		}
		// Not fromValueStrict: the version is a document field outside the parts, so the comparison is
		// against the whole document written back.
		result.problems = data::shapeDifferences(document, documentOf(*parts));
		if (!result.problems.empty())
			return result;

		// What makes the parts a rig, each problem named by its path as a shape problem is.
		if (parts->cameras.empty())
			result.problems.push_back("'cameras' is empty: a rig has a camera at least");
		bool hasReference = false;
		for (std::size_t i = 0; i < parts->cameras.size(); ++i)
		{
			const RigDocumentCamera& camera = parts->cameras[i];
			const std::string at = lain::string::format("cameras[{}]", i);
			if (camera.camera.empty())
				result.problems.push_back(lain::string::format("'{}.camera' is empty", at));
			for (std::size_t j = 0; j < i; ++j)
			{
				if (parts->cameras[j].camera == camera.camera)
					result.problems.push_back(
						lain::string::format("'{}.camera' is '{}', as 'cameras[{}].camera' is", at, camera.camera, j));
			}

			const RigRotation& r = camera.referenceFromCamera.rotation;
			const double norm = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
			if (std::abs(norm - 1) > kTolerance)
				result.problems.push_back(lain::string::format(
					"'{}.referenceFromCamera.rotation' is not a unit quaternion: its norm is {}", at, norm));

			if (camera.camera == parts->reference)
			{
				hasReference = true;
				const RigTranslation& t = camera.referenceFromCamera.translation;
				const bool identity = std::abs(r.x) <= kTolerance && std::abs(r.y) <= kTolerance &&
									  std::abs(r.z) <= kTolerance && std::abs(t.x) <= kTolerance &&
									  std::abs(t.y) <= kTolerance && std::abs(t.z) <= kTolerance;
				if (!identity)
					result.problems.push_back(lain::string::format(
						"'{}.referenceFromCamera' is not the identity, and '{}' is the reference", at, camera.camera));
			}
		}
		if (!hasReference)
			result.problems.push_back(
				lain::string::format("'reference' is '{}', which is not one of the rig's cameras", parts->reference));
		if (!result.problems.empty())
			return result;

		RegisteredRig rig{{parts->reference}, parts->scale, {}};
		for (const RigDocumentCamera& camera : parts->cameras)
		{
			const RigRotation& r = camera.referenceFromCamera.rotation;
			const RigTranslation& t = camera.referenceFromCamera.translation;
			rig.cameras.push_back({{camera.camera},
								   math::RigidTransformd{math::Quatd{r.w, r.x, r.y, r.z}, math::Vec3d{t.x, t.y, t.z}}});
		}
		std::sort(rig.cameras.begin(), rig.cameras.end(), [](const RegisteredCamera& a, const RegisteredCamera& b)
				  { return a.camera < b.camera; });
		result.rig = std::move(rig);
		return result;
	}
} // namespace lain::camera::registration
