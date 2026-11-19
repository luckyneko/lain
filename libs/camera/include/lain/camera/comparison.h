#pragma once

#include "lain/camera/cameramodel.h"
#include "lain/camera/capture/capturegroup.h"
#include "lain/camera/registration/report.h"

#include <lain/math/types.h>

#include <cstddef>
#include <string>
#include <vector>

// How far a camera model or a registered rig is from an expected one (CONTEXT.md, "Model comparison"
// and "Rig comparison"). Neither knows where either side came from, so each scores a result against a
// synthetic capture's ground truth, a survey or a manufacturer's model alike (ADR-0029). Neither is a
// verdict: whether a difference is small enough is the caller's to say.
namespace lain::camera
{
	enum class ModelComparisonStatus
	{
		Compared,
		DifferentImages, // the two models are of different image geometries
	};

	// Where two models of one image geometry send the same rays: each sampled pixel's ray through the
	// EXPECTED model, projected through the FOUND one, and the distance from where it started. Not
	// symmetric, and stated so: the rays are the expected model's.
	struct ModelComparison
	{
		ModelComparisonStatus status = ModelComparisonStatus::Compared;
		std::string detail; // why, when not compared

		// The pixels sampled: every 16th pixel centre from 0 along each axis, and the last row and
		// column, so the corners, where distortion is strongest, are among them.
		std::size_t samples = 0;

		// Samples that could not be compared: the expected model has no ray there, or the found model
		// cannot project it. Any makes the mean and the worst infinite, since two models that disagree
		// about whether a ray is seen are as far apart as they can be.
		std::size_t unreached = 0;

		double meanPixels = 0;
		double worstPixels = 0;
		math::Vec2d worstAt{0.0, 0.0}; // the expected pixel the worst distance was measured at

		// The larger of the focal lengths' relative differences, max(|fx_found / fx_expected - 1|,
		// |fy_found / fy_expected - 1|). Beside the ray mapping, not part of it: two models can trade
		// focal length against distortion and still project alike.
		double focalDifference = 0;

		bool compared() const { return status == ModelComparisonStatus::Compared; }

		// One line for a person: the mean and worst distance, where the worst is, the focal difference
		// and how many rays; or why there was no comparison.
		std::string toString() const;
	};

	// How far `found` is from `expected`, two models of one image geometry. Refused, as
	// DifferentImages, when their images differ.
	ModelComparison compareModels(const CameraModel& expected, const CameraModel& found);

	enum class RigComparisonStatus
	{
		Compared,
		UnknownReference, // found's reference camera is not in the expected rig
	};

	// One camera both rigs hold.
	struct CameraDifference
	{
		capture::CameraIdentity camera;
		double rotation = 0; // the angle between the two rotations, radians
		double centre = 0;	 // the distance between the two centres, in the expected rig's units
	};

	// How far a registered rig is from an expected one, both relative to the found rig's reference
	// camera. When the expected rig is relative to another of its cameras, it is DERIVED AGAIN for the
	// found one's, never the found rig rebased: rebasing a result would spread its reference camera's
	// own error over every other camera.
	//
	// Both pin the reference to the identity, so the two can differ in rotation and in the scale of
	// their centres and in nothing else. A scale-only fit is the whole alignment: the found rig's centres
	// are scaled onto the expected one's by least squares, s = sum f.e / sum f.f over the cameras both
	// hold, whatever either rig's scale status says. A metric registration's scale error is `scale`.
	struct RigComparison
	{
		RigComparisonStatus status = RigComparisonStatus::Compared;
		std::string detail; // why, when not compared

		capture::CameraIdentity reference; // the found rig's, which both are compared relative to
		bool derivedAgain = false;		   // the expected rig was relative to another camera
		registration::Scale expectedScale = registration::Scale::Arbitrary;
		registration::Scale foundScale = registration::Scale::Arbitrary;
		double scale = 1.0; // what the found rig's centres were multiplied by

		std::vector<CameraDifference> cameras; // both rigs hold them; ascending by identity
		std::vector<capture::CameraIdentity> onlyExpected;
		std::vector<capture::CameraIdentity> onlyFound;

		bool compared() const { return status == RigComparisonStatus::Compared; }

		// The camera whose rotation, or whose centre, is furthest from the expected one's. Derived, not
		// stored, so they cannot disagree with `cameras`; null when nothing was compared.
		const CameraDifference* worstRotation() const;
		const CameraDifference* worstCentre() const;

		// One line for a person: the reference, the scale, the worst rotation and centre and whose they
		// are, and any camera only one rig holds; or why there was no comparison.
		std::string toString() const;
	};

	// How far `found` is from `expected`, cameras paired by identity. The cameras only one rig holds are
	// named rather than refused, so a survey of some of a rig's cameras can still score it. Refused when
	// found's reference is not in `expected`: there is nothing to derive the expected rig again for. So
	// a comparison always shares at least the reference.
	RigComparison compareRigs(const registration::RegisteredRig& expected, const registration::RegisteredRig& found);
} // namespace lain::camera
