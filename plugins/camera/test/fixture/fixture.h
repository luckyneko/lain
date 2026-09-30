#pragma once

#include <lain/camera/board/specification.h>
#include <lain/camera/calibration/report.h>
#include <lain/camera/calibration/request.h>
#include <lain/camera/cameramodel.h>
#include <lain/camera/capturerecord.h>
#include <lain/camera/distortion.h>
#include <lain/camera/serialize/board.h>
#include <lain/camera/serialize/capturerecord.h>
#include <lain/media/framesequence.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// A real-camera calibration fixture (ADR-0016): footage of one known board from one camera, in two or
// more capture SESSIONS (the camera unplugged and re-mounted between them), with a capture record per
// session. On disk it is a folder holding fixture.json and one folder of stills per session.
//
// Nothing here names a device. A fixture's provenance is its capture records, so a second camera is a
// second fixture folder, never a second schema.
namespace lain::camera::fixture
{
	constexpr std::uint32_t kFixtureVersion = 1;
	constexpr const char* kFixtureFile = "fixture.json";

	// fixture.json, as written: {version, name, model, board, sessions}. `model` is the distortion
	// model every session is calibrated with; `frames` is a folder relative to fixture.json.
	struct SessionDocument
	{
		std::string name;
		std::string frames;
		CaptureRecord capture;
	};
	LAIN_SERIALIZE(SessionDocument, name, frames, capture)

	struct FixtureDocument
	{
		std::uint32_t version = kFixtureVersion;
		std::string name;
		DistortionModel model = DistortionModel::BrownConrady5;
		board::SpecificationParameters board;
		std::vector<SessionDocument> sessions;
	};
	LAIN_SERIALIZE(FixtureDocument, version, name, model, board, sessions)

	// A loaded session: its footage opened, and its imported model (if the record has one) made a
	// camera model.
	struct Session
	{
		std::string name;
		media::FrameSequence footage;
		ImageGeometry image;
		CaptureRecord capture;
		std::optional<CameraModel> imported;
	};

	struct Fixture
	{
		std::string name;
		std::filesystem::path root;
		board::Specification board;
		DistortionModel model;
		std::vector<Session> sessions;
	};

	struct FixtureLoad
	{
		std::optional<Fixture> fixture;
		std::vector<std::string> problems; // empty exactly when `fixture` is set
	};

	// The fixture in `root`, read strictly (a typo in fixture.json is a named problem, not a default):
	// the document, the board through specificationFrom, each session's frames through io::sequence,
	// each imported model through CameraModel::create, and each session's frames against the stream
	// size its capture record states. The caller registers the image codecs, the sequence openers and
	// the json codec.
	FixtureLoad loadFixture(const std::filesystem::path& root);

	// A fixture.json to fill in: the board apps/flowview/examples/render-board.json prints (9 x 6
	// squares of 30 mm, markers 0.75 of a square, ARUCO 5x5_100) at its nominal size, Brown-Conrady
	// with five coefficients, and sessions "a" and "b" in folders of those names, with empty capture
	// records. What camera-fixture-tool template writes; the square length is replaced by the
	// measured one, and the records by what is known.
	FixtureDocument templateDocument(const std::string& name);

	// Write `document` as root/fixture.json. False (logged) on a write failure.
	bool writeFixtureDocument(const std::filesystem::path& root, const FixtureDocument& document);

	// --- the harness ---------------------------------------------------------------------------

	struct SessionOutcome
	{
		std::string name;
		calibration::Report report;
	};

	// How far apart two sets of intrinsics are, in the units the fitness profile's stability criteria
	// use, so they compare against the same numbers.
	struct IntrinsicsDifference
	{
		double focal = 0;		   // relative: the larger of |dfx| and |dfy| over the pair's mean focal length
		double principalPoint = 0; // radians: the larger of |dcx| and |dcy| over the pair's mean focal length
	};
	IntrinsicsDifference differenceOf(const Intrinsics& a, const Intrinsics& b);

	// The repeat check has two halves, because each misses what the other sees.
	//
	// PREDICTION: one session's Ready model held (HoldAndValidate) and checked against another
	// session's views, to the angle its own held-out views needed for Ready. It sees a lens that is
	// not the lens (distortion), and it is weak on intrinsics: each view's board pose is recovered with
	// the model fixed, and on a planar board a pose absorbs most of a focal or principal-point error.
	// Measured on synthetic footage: a second camera 3% longer and 12 px off-centre predicted the
	// first's views at 0.27 mrad, against 0.6 for Ready.
	struct CrossValidation
	{
		std::string model; // the session whose model is held
		std::string views; // the session whose views it is checked against
		std::variant<calibration::HeldOutEvidence, calibration::Unavailable> evidence = calibration::Unavailable{};
		double threshold = 0; // radians: the Ready held-out angle the model was held to
		bool agrees = false;
	};

	// AGREEMENT: two Ready sessions' estimates compared directly, against the spread each showed when
	// its own views were resampled: they agree when they differ by no more than kAgreementSigmas
	// combined standard deviations, sqrt(sa^2 + sb^2). Not against the Ready tier's stability bounds
	// themselves: those bound one capture's resamples, and two captures also differ by their poses.
	// Measured on synthetic footage of one camera, two sessions' focal lengths came out 0.544% apart
	// against a 0.5% Ready bound, and 1.5 combined deviations apart. Ready caps each deviation, so the
	// bound here never exceeds about 2.1% in focal length and 8.5 mrad in principal point.
	constexpr double kAgreementSigmas = 3.0;

	struct IntrinsicsAgreement
	{
		std::string first;
		std::string second;
		IntrinsicsDifference difference;
		double focalBound = 0;			// relative
		double principalPointBound = 0; // radians
		bool agrees = false;
	};

	// A capture's imported manufacturer model held and checked against its own session's views, beside
	// what the session estimated. A diagnostic only (ADR-0016): it never passes or fails the fixture.
	struct ManufacturerComparison
	{
		std::string session;
		std::string source;
		std::variant<calibration::HeldOutEvidence, calibration::Unavailable> evidence = calibration::Unavailable{};
		Intrinsics imported;
		std::optional<Intrinsics> estimated;			// the session's own estimate, when it produced one
		std::optional<IntrinsicsDifference> difference; // imported against estimated
	};

	struct FixtureReport
	{
		std::string name;
		std::vector<SessionOutcome> sessions;
		std::vector<CrossValidation> crossValidations;
		std::vector<IntrinsicsAgreement> agreements;
		std::vector<ManufacturerComparison> manufacturer;
		std::vector<std::string> failures; // each criterion that failed, for a person

		bool passed() const { return failures.empty(); }

		// Several lines: the verdict, each session's report, each check, each comparison.
		std::string toString() const;
	};

	// Calibrate every session independently with the fixture's model (and `base` for everything else),
	// then the repeat check:
	//   - at least two sessions must be Ready, since the check is that two calibrations agree;
	//   - every Ready model must predict every other session's views (CrossValidation);
	//   - every two Ready sessions' intrinsics must agree (IntrinsicsAgreement).
	// Each imported model is compared, as a diagnostic that never fails the fixture.
	FixtureReport runFixture(const Fixture& fixture, const calibration::Request& base = {});
} // namespace lain::camera::fixture
