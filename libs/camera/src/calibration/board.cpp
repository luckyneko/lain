#include "lain/camera/calibration/board.h"

#include "execution.h"
#include "lain/camera/board/pose.h"
#include "lain/camera/calibration/estimator.h"
#include "lain/camera/projection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>

namespace lain::camera::calibration::board
{
	namespace cb = camera::board;

	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Fewer calibration views than this and no estimate is attempted at all; whether more are enough
	// to trust is the fitness profile's question.
	static constexpr std::size_t kMinimumCalibrationViews = 3;
	// Fewer usable views than this and none are held out: holding one of four out leaves too little
	// to estimate from and too little to validate with.
	static constexpr std::size_t kMinimumToHoldOut = 5;

	static bool usable(const cb::DetectionReport& report)
	{
		return report.status != cb::DetectionStatus::Failed && report.observation.has_value();
	}

	// Positions of the usable detections in canonical frame order: source uri, then ordinal, then
	// position, so the choice of views never depends on how the footage happened to be assembled.
	static std::vector<std::uint32_t> canonicalUsable(const std::vector<cb::DetectionReport>& detections)
	{
		std::vector<std::uint32_t> out;
		for (std::uint32_t i = 0; i < detections.size(); ++i)
		{
			if (usable(detections[i]))
				out.push_back(i);
		}
		std::stable_sort(out.begin(), out.end(),
						 [&](std::uint32_t a, std::uint32_t b)
						 {
							 const media::FrameRef& fa = detections[a].observation->frame;
							 const media::FrameRef& fb = detections[b].observation->frame;
							 if (fa.source.toString() != fb.source.toString())
								 return fa.source.toString() < fb.source.toString();
							 return fa.ordinal < fb.ordinal;
						 });
		return out;
	}

	// The grid cell a pixel falls in, or -1 outside the image.
	static int cellOf(const math::Vec2d& pixel, const ImageGeometry& image)
	{
		const double u = (pixel.x + 0.5) / double(image.width);
		const double v = (pixel.y + 0.5) / double(image.height);
		if (!(u >= 0 && u < 1 && v >= 0 && v < 1))
			return -1;
		return int(v * kCoverageRows) * kCoverageColumns + int(u * kCoverageColumns);
	}

	using CellSet = std::array<bool, kCoverageColumns * kCoverageRows>;

	static CellSet cellsOf(const cb::Observation& view, const ImageGeometry& image)
	{
		CellSet cells{};
		for (const cb::FeatureObservation& f : view.features)
		{
			const int cell = cellOf(f.pixel, image);
			if (cell >= 0)
				cells[std::size_t(cell)] = true;
		}
		return cells;
	}

	// The board's TILT in one view, without intrinsics: the perspective row of the homography from
	// the board plane (in board widths) to the image (in image widths). Its length is roughly the
	// fractional change in depth across the board, so (0, 0) is a square-on view and its direction
	// says which way the board leans. Zero when the homography cannot be fitted.
	static math::Vec2d tiltOf(const cb::Observation& view, const cb::Specification& board, const ImageGeometry& image)
	{
		const cb::PatternParameters& p = board.pattern().parameters();
		const double boardWidth = double(p.squaresX) * board.instance().squareLength.value.metres();
		if (view.features.size() < 4 || boardWidth <= 0 || image.width == 0)
			return {0, 0};

		// h33 = 1: eight unknowns, two equations per corner, solved through the normal equations.
		double normal[8][9] = {};
		for (const cb::FeatureObservation& f : view.features)
		{
			const math::Vec3d b = *board.cornerPosition(f.id);
			const double x = b.x / boardWidth, y = b.y / boardWidth;
			const double u = (f.pixel.x + 0.5) / double(image.width);
			const double v = (f.pixel.y + 0.5) / double(image.width);
			const double rows[2][9] = {{x, y, 1, 0, 0, 0, -u * x, -u * y, u}, {0, 0, 0, x, y, 1, -v * x, -v * y, v}};
			for (const auto& row : rows)
			{
				for (int i = 0; i < 8; ++i)
				{
					for (int j = 0; j < 9; ++j)
						normal[i][j] += row[i] * row[j];
				}
			}
		}
		// Gaussian elimination with partial pivoting.
		for (int col = 0; col < 8; ++col)
		{
			int pivot = col;
			for (int r = col + 1; r < 8; ++r)
			{
				if (std::abs(normal[r][col]) > std::abs(normal[pivot][col]))
					pivot = r;
			}
			if (std::abs(normal[pivot][col]) < 1e-12)
				return {0, 0};
			for (int j = 0; j < 9; ++j)
				std::swap(normal[col][j], normal[pivot][j]);
			for (int r = 0; r < 8; ++r)
			{
				if (r == col)
					continue;
				const double factor = normal[r][col] / normal[col][col];
				for (int j = col; j < 9; ++j)
					normal[r][j] -= factor * normal[col][j];
			}
		}
		return {normal[6][8] / normal[6][6], normal[7][8] / normal[7][7]};
	}

	// Five tilt buckets: square-on, and leaning towards each of the four sides.
	static int bucketOf(const math::Vec2d& tilt)
	{
		constexpr double kSquareOn = 0.15;
		if (math::length(tilt) < kSquareOn)
			return 0;
		constexpr double kQuarter = 1.57079632679489661923;
		const double angle = std::atan2(tilt.y, tilt.x) + kQuarter / 2; // centre each bucket on an axis
		const int quadrant = int(std::floor(angle / kQuarter));
		return 1 + ((quadrant % 4) + 4) % 4;
	}

	// Sample standard deviation.
	static double deviation(const std::vector<double>& values)
	{
		double mean = 0;
		for (const double v : values)
			mean += v / double(values.size());
		double sum = 0;
		for (const double v : values)
			sum += (v - mean) * (v - mean);
		return std::sqrt(sum / double(values.size() - 1));
	}

	static double average(const std::vector<double>& values)
	{
		double mean = 0;
		for (const double v : values)
			mean += v / double(values.size());
		return mean;
	}

	// The first registered backend that can estimate `model`, or nullptr.
	static std::unique_ptr<Estimator> estimatorFor(DistortionModel model)
	{
		for (const std::string& key : estimatorRegistry().keys())
		{
			std::unique_ptr<Estimator> estimator = estimatorRegistry().create(key);
			if (estimator->canEstimate(model))
				return estimator;
		}
		return nullptr;
	}

	using detail::forEach;

	static std::vector<cb::Observation> observationsAt(const std::vector<cb::DetectionReport>& detections,
													   const std::vector<std::uint32_t>& positions)
	{
		std::vector<cb::Observation> out;
		out.reserve(positions.size());
		for (const std::uint32_t i : positions)
			out.push_back(*detections[i].observation);
		return out;
	}

	// The model checked against views it was not estimated from, through lain's own projection. The
	// pose solver only places each view's board with the model held fixed; every residual is
	// measured here (board::measure).
	static std::variant<HeldOutEvidence, Unavailable> validate(const CameraModel& model, const cb::Specification& board,
															   const std::vector<cb::Observation>& views,
															   Provenance& poseSolver)
	{
		if (views.empty())
			return Unavailable{"no views were held out"};
		if (!cb::canSolvePose())
			return Unavailable{"this build has no board pose solver"};
		HeldOutEvidence evidence;
		evidence.views = std::uint32_t(views.size());
		double angles = 0, pixels = 0;
		for (const cb::Observation& view : views)
		{
			const cb::PoseResult pose = cb::pose(model, board, view);
			if (poseSolver.backend.empty())
				poseSolver = pose.provenance;
			if (!pose.ok())
			{
				++evidence.viewsWithoutPose;
				continue;
			}
			const cb::ViewResidual residual = cb::measure(model, board, view, pose.pose->cameraFromBoard);
			angles += residual.sumSquaredAngle;
			pixels += residual.sumSquaredPixels;
			evidence.worstPixels = std::max(evidence.worstPixels, residual.worstPixels);
			evidence.corners += residual.corners;
		}
		if (evidence.corners == 0)
			return Unavailable{"no held-out corner could be measured: no board pose was recovered"};
		evidence.rmsAngle = std::sqrt(angles / evidence.corners);
		evidence.rmsPixels = std::sqrt(pixels / evidence.corners);
		return evidence;
	}

	// Re-estimate from bootstrap resamples of the calibration views, each from the same `initial` as
	// the estimate itself: stability is a property of the procedure that produced the model, so a
	// seeded estimate is resampled seeded. The draws come from one seeded
	// engine, in order, before anything runs, so they are the same under either execution policy;
	// only the estimates run in parallel. The mapping from an engine draw to an index is a plain
	// modulo, since the standard's distributions are implementation-defined and would draw
	// differently on each platform.
	static std::variant<ResamplingEvidence, Unavailable>
	resample(const std::vector<cb::Observation>& views, const cb::Specification& board, const ImageGeometry& image,
			 const Request& request, const std::optional<CameraModelParameters>& initial, const Estimator& estimator)
	{
		if (request.resamples == 0)
			return Unavailable{"resampling was not requested"};
		if (views.size() < kMinimumCalibrationViews)
			return Unavailable{"too few calibration views to resample"};

		std::mt19937_64 engine(request.seed);
		std::vector<std::vector<cb::Observation>> draws(request.resamples);
		for (std::vector<cb::Observation>& draw : draws)
		{
			for (std::size_t i = 0; i < views.size(); ++i)
				draw.push_back(views[std::size_t(engine() % views.size())]);
		}

		std::vector<std::optional<CameraModelParameters>> estimates(draws.size());
		forEach(draws.size(), request.execution,
				[&](std::size_t r)
				{ estimates[r] = estimator.estimate(image, draws[r], board, request.model, initial).parameters; });

		std::vector<double> fx, fy, cx, cy;
		for (const std::optional<CameraModelParameters>& e : estimates)
		{
			if (!e)
				continue;
			fx.push_back(e->intrinsics.fx);
			fy.push_back(e->intrinsics.fy);
			cx.push_back(e->intrinsics.cx);
			cy.push_back(e->intrinsics.cy);
		}
		if (fx.size() < 2)
			return Unavailable{"fewer than two resamples produced an estimate"};

		ResamplingEvidence evidence;
		evidence.resamples = std::uint32_t(fx.size());
		evidence.focalVariation = std::max(deviation(fx) / average(fx), deviation(fy) / average(fy));
		const double focal = (average(fx) + average(fy)) / 2;
		evidence.principalPointVariation = std::max(deviation(cx), deviation(cy)) / focal;
		return evidence;
	}

	// Whether the evidence meets one tier. `complete` demands every criterion's evidence be present
	// (Ready); otherwise a criterion whose evidence is unavailable is not held against it. Notes say
	// what fell short.
	static bool meets(const FitnessThresholds& t, const char* tier, bool complete, std::uint32_t views, double covered,
					  const std::variant<HeldOutEvidence, Unavailable>& heldOut,
					  const std::variant<ResamplingEvidence, Unavailable>& resampling, std::vector<std::string>& notes)
	{
		bool ok = true;
		const auto note = [&](std::string text)
		{
			notes.push_back(std::string(tier) + ": " + std::move(text));
			ok = false;
		};
		if (views < t.minimumViews)
			note(std::to_string(views) + " views, fewer than " + std::to_string(t.minimumViews));
		if (covered < t.minimumCoverage)
			note("coverage " + std::to_string(covered) + " is below " + std::to_string(t.minimumCoverage));
		if (const auto* e = std::get_if<HeldOutEvidence>(&heldOut))
		{
			if (e->rmsAngle > t.maximumHeldOutAngle)
				note("held-out RMS angle " + std::to_string(e->rmsAngle) + " rad exceeds " + std::to_string(t.maximumHeldOutAngle));
		}
		else if (complete)
			note("no held-out evidence: " + std::get<Unavailable>(heldOut).reason);
		if (const auto* e = std::get_if<ResamplingEvidence>(&resampling))
		{
			if (e->focalVariation > t.maximumFocalVariation)
				note("focal variation " + std::to_string(e->focalVariation) + " exceeds " + std::to_string(t.maximumFocalVariation));
			if (e->principalPointVariation > t.maximumPrincipalPointVariation)
				note("principal point variation " + std::to_string(e->principalPointVariation) + " rad exceeds " +
					 std::to_string(t.maximumPrincipalPointVariation));
		}
		else if (complete)
			note("no resampling evidence: " + std::get<Unavailable>(resampling).reason);
		return ok;
	}

	// --- the public pieces --------------------------------------------------------

	double coverage(const std::vector<const cb::Observation*>& views, const ImageGeometry& image)
	{
		if (image.width == 0 || image.height == 0)
			return 0;
		CellSet covered{};
		for (const cb::Observation* view : views)
		{
			const CellSet cells = cellsOf(*view, image);
			for (std::size_t c = 0; c < cells.size(); ++c)
				covered[c] = covered[c] || cells[c];
		}
		return double(std::count(covered.begin(), covered.end(), true)) / double(covered.size());
	}

	ViewSplit splitViews(const std::vector<cb::DetectionReport>& detections, const cb::Specification& board,
						 const Request& request)
	{
		ViewSplit split;
		const std::vector<std::uint32_t> ordered = canonicalUsable(detections);

		// Held out: every k-th usable view, starting half a stride in, so the held-out views are
		// spread through the footage the way the calibration views are drawn from it.
		std::vector<bool> heldOut(ordered.size(), false);
		if (request.heldOutFraction > 0 && ordered.size() >= kMinimumToHoldOut)
		{
			const std::size_t stride = std::max<std::size_t>(2, std::size_t(std::lround(1.0 / request.heldOutFraction)));
			for (std::size_t i = stride / 2; i < ordered.size(); i += stride)
			{
				heldOut[i] = true;
				split.heldOut.push_back(ordered[i]);
			}
		}

		// Calibration views, greedily. A view scores the grid cells it newly covers, a bonus when its
		// tilt bucket is not represented yet, and a smaller one for how far its tilt is from every
		// tilt already chosen; strict comparison in canonical order sends ties to the earlier view.
		struct Candidate
		{
			std::uint32_t position;
			CellSet cells;
			math::Vec2d tilt;
			int bucket;
		};
		std::vector<Candidate> candidates;
		for (std::size_t i = 0; i < ordered.size(); ++i)
		{
			if (heldOut[i])
				continue;
			const cb::DetectionReport& report = detections[ordered[i]];
			const ImageGeometry& image = report.observation->image;
			const math::Vec2d tilt = tiltOf(*report.observation, board, image);
			candidates.push_back({ordered[i], cellsOf(*report.observation, image), tilt, bucketOf(tilt)});
		}

		CellSet covered{};
		std::set<int> buckets;
		std::vector<math::Vec2d> tilts;
		std::vector<bool> taken(candidates.size(), false);
		while (split.calibration.size() < request.maximumViews)
		{
			std::size_t best = candidates.size();
			double bestScore = -1;
			for (std::size_t c = 0; c < candidates.size(); ++c)
			{
				if (taken[c])
					continue;
				double score = 0;
				for (std::size_t cell = 0; cell < covered.size(); ++cell)
					score += (candidates[c].cells[cell] && !covered[cell]) ? 1.0 : 0.0;
				if (buckets.count(candidates[c].bucket) == 0)
					score += 4.0;
				double nearest = 1.0;
				for (const math::Vec2d& t : tilts)
					nearest = std::min(nearest, math::length(candidates[c].tilt - t) / 0.3);
				score += 2.0 * nearest;
				if (score > bestScore)
				{
					bestScore = score;
					best = c;
				}
			}
			if (best == candidates.size())
				break;
			taken[best] = true;
			split.calibration.push_back(candidates[best].position);
			for (std::size_t cell = 0; cell < covered.size(); ++cell)
				covered[cell] = covered[cell] || candidates[best].cells[cell];
			buckets.insert(candidates[best].bucket);
			tilts.push_back(candidates[best].tilt);
		}
		return split;
	}

	// --- calibrate ------------------------------------------------------------------

	// A report that has failed, timed from `start`.
	static Report failed(Report report, core::Time start, Failure failure, std::string detail)
	{
		report.status = CalibrationStatus::Failed;
		report.failures.push_back({failure, std::move(detail)});
		report.elapsed = core::Time::now() - start;
		return report;
	}

	// The checks both entry points make before any work: a known profile, a usable imported model,
	// and a backend for the job. Returns the estimator to use (none for a held model, which estimates
	// nothing), or the failed report.
	static std::variant<std::unique_ptr<Estimator>, Report> prepare(Report& report, const ImageGeometry& image,
																	const Request& request, core::Time start)
	{
		report.reproducibility.request = request;
		const std::optional<FitnessProfile> profile = fitnessProfile(request.fitnessProfile);
		if (!profile)
			return failed(report, start, Failure::UnknownFitnessProfile,
						  "no fitness profile is named \"" + request.fitnessProfile + "\"");
		report.thresholds = resolve(*profile, request.overrides);

		const bool hold = request.importedPolicy == ImportedModelPolicy::HoldAndValidate;
		if (hold && !request.imported)
			return failed(report, start, Failure::NoImportedModel, "HoldAndValidate needs an imported model to hold");
		if (request.imported && applicability(*request.imported, image) == Applicability::Incompatible)
			return failed(report, start, Failure::IncompatibleImportedModel,
						  "the imported model is " + std::to_string(request.imported->image().width) + "x" +
							  std::to_string(request.imported->image().height) + " and the footage is " +
							  std::to_string(image.width) + "x" + std::to_string(image.height));

		// A held model needs only board poses, which are all its validation is; an estimated one needs a
		// backend that estimates exactly the requested model, never a similar one.
		if (hold)
		{
			if (!cb::canSolvePose())
				return failed(report, start, Failure::NoPoseSolver,
							  "this build has no board pose solver, which validating a held model needs");
			return std::unique_ptr<Estimator>{};
		}
		std::unique_ptr<Estimator> estimator = estimatorFor(request.model);
		if (!estimator)
			return failed(report, start, Failure::NoEstimator,
						  canEstimate() ? "no registered backend can estimate " + std::string(displayName(request.model))
										: std::string("this build has no calibration estimator"));
		report.reproducibility.estimator = estimator->provenance();
		return estimator;
	}

	// Everything after detection: which views do what, the estimate, its validation, its stability and
	// its verdict. `report` arrives prepared, with its detections in place.
	static Report analyse(Report report, const ImageGeometry& image, const cb::Specification& board,
						  const Request& request, const Estimator* estimator, core::Time start)
	{
		Diagnostics& diagnostics = report.diagnostics;
		diagnostics.framesExamined = std::uint32_t(diagnostics.detections.size());
		diagnostics.framesUsable = 0;
		for (const cb::DetectionReport& d : diagnostics.detections)
		{
			if (usable(d))
				++diagnostics.framesUsable;
			if (report.reproducibility.detector.backend.empty())
				report.reproducibility.detector = d.provenance;
		}

		// Which views do what. A held model has nothing to estimate, so every usable view validates it.
		const bool hold = request.importedPolicy == ImportedModelPolicy::HoldAndValidate;
		ViewSplit split;
		if (hold)
			split.heldOut = canonicalUsable(diagnostics.detections);
		else
			split = splitViews(diagnostics.detections, board, request);
		diagnostics.selected = split.calibration;
		diagnostics.heldOut = split.heldOut;
		diagnostics.viewsSelected = std::uint32_t(split.calibration.size());
		diagnostics.viewsHeldOut = std::uint32_t(split.heldOut.size());
		const std::vector<cb::Observation> calibrationViews = observationsAt(diagnostics.detections, split.calibration);
		const std::vector<cb::Observation> heldOutViews = observationsAt(diagnostics.detections, split.heldOut);
		{
			std::vector<const cb::Observation*> supporting;
			for (const cb::Observation& v : hold ? heldOutViews : calibrationViews)
				supporting.push_back(&v);
			diagnostics.coverage = coverage(supporting, image);
		}

		std::optional<CameraModelParameters> initial;
		if (hold)
		{
			report.model = request.imported;
		}
		else
		{
			if (calibrationViews.size() < kMinimumCalibrationViews)
				return failed(std::move(report), start, Failure::TooFewViews,
							  std::to_string(calibrationViews.size()) + " usable calibration views of " +
								  std::to_string(diagnostics.framesExamined) + " frames; at least " +
								  std::to_string(kMinimumCalibrationViews) + " are needed");

			// The Initial policy seeds the estimate. Every parameter only when the imported model is
			// the requested model; otherwise the pinhole part alone, with the distortion starting
			// neutral, since coefficients never carry across models on their shape (ADR-0016).
			if (request.imported && request.importedPolicy == ImportedModelPolicy::Initial)
			{
				initial = CameraModelParameters{image, request.imported->intrinsics(), neutral(request.model)};
				report.seededFields = {"fx", "fy", "cx", "cy"};
				if (modelOf(request.imported->distortion()) == request.model)
				{
					initial->distortion = request.imported->distortion();
					for (const std::string_view name : coefficientNames(request.model))
						report.seededFields.emplace_back(name);
				}
			}

			const Estimate estimate = estimator->estimate(image, calibrationViews, board, request.model, initial);
			if (!estimate.parameters)
				return failed(std::move(report), start, Failure::EstimationFailed,
							  estimate.failure.empty() ? "the estimator returned no model" : estimate.failure);
			ModelResult created = CameraModel::create(*estimate.parameters);
			if (!created.model)
			{
				for (const ModelDiagnostic& d : created.diagnostics)
					report.failures.push_back({Failure::InvalidModel, d.detail});
				return failed(std::move(report), start, Failure::InvalidModel, "the estimate is not a valid camera model");
			}
			report.model = std::move(created.model);
			diagnostics.fitRmsPixels = estimate.rmsPixels;
			report.uncertainty = estimate.uncertainty;
		}
		report.status = CalibrationStatus::Succeeded;

		report.heldOut = validate(*report.model, board, heldOutViews, report.reproducibility.poseSolver);
		report.resampling = hold ? std::variant<ResamplingEvidence, Unavailable>{Unavailable{"the model was held, not estimated"}}
								 : resample(calibrationViews, board, image, request, initial, *estimator);

		// The verdict. A held model is judged on the views that validated it.
		const std::uint32_t supportingViews = hold ? diagnostics.viewsHeldOut : diagnostics.viewsSelected;
		std::vector<std::string> readyNotes, exploratoryNotes;
		if (meets(report.thresholds.ready, "Ready", true, supportingViews, diagnostics.coverage, report.heldOut,
				  report.resampling, readyNotes))
			report.verdict = Verdict::Ready;
		else if (meets(report.thresholds.exploratory, "Exploratory", false, supportingViews, diagnostics.coverage,
					   report.heldOut, report.resampling, exploratoryNotes))
			report.verdict = Verdict::Exploratory;
		else
			report.verdict = Verdict::Rejected;
		report.fitnessNotes = readyNotes;
		report.fitnessNotes.insert(report.fitnessNotes.end(), exploratoryNotes.begin(), exploratoryNotes.end());

		report.elapsed = core::Time::now() - start;
		return report;
	}

	Report calibrate(const std::vector<cb::DetectionReport>& detections, const ImageGeometry& image,
					 const cb::Specification& board, const Request& request)
	{
		const core::Time start = core::Time::now();
		Report report;
		std::variant<std::unique_ptr<Estimator>, Report> prepared = prepare(report, image, request, start);
		if (Report* failure = std::get_if<Report>(&prepared))
			return std::move(*failure);

		report.reproducibility.frames = std::uint32_t(detections.size());
		for (const cb::DetectionReport& d : detections)
		{
			if (!d.observation)
				continue;
			const std::string source = d.observation->frame.source.toString();
			if (std::find(report.reproducibility.sources.begin(), report.reproducibility.sources.end(), source) ==
				report.reproducibility.sources.end())
				report.reproducibility.sources.push_back(source);
		}
		report.diagnostics.detections = detections;
		return analyse(std::move(report), image, board, request, std::get<std::unique_ptr<Estimator>>(prepared).get(), start);
	}

	Report calibrate(const media::FrameSequence& footage, const cb::Specification& board, const Request& request)
	{
		const core::Time start = core::Time::now();
		Report report;
		report.reproducibility.request = request;
		report.reproducibility.frames = std::uint32_t(footage.size());
		for (std::size_t i = 0; i < footage.size(); ++i)
		{
			const std::string source = footage.frame(i).source.toString();
			if (std::find(report.reproducibility.sources.begin(), report.reproducibility.sources.end(), source) ==
				report.reproducibility.sources.end())
				report.reproducibility.sources.push_back(source);
		}

		if (footage.size() == 0)
			return failed(std::move(report), start, Failure::NoFootage, "the frame sequence is empty");
		const math::Vec2i extent = footage.spec().extent;
		const ImageGeometry image{std::uint32_t(std::max(extent.x, 0)), std::uint32_t(std::max(extent.y, 0))};

		// Every check that needs no frame comes first, so a request that cannot succeed decodes nothing.
		if (!cb::canDetect())
			return failed(std::move(report), start, Failure::NoDetector,
						  "this build has no board detector (configure with -DLAIN_CAMERA_OPENCV=ON)");
		std::variant<std::unique_ptr<Estimator>, Report> prepared = prepare(report, image, request, start);
		if (Report* failure = std::get_if<Report>(&prepared))
			return std::move(*failure);

		// Detect in every frame, one decoded frame per task.
		std::vector<cb::DetectionReport>& detections = report.diagnostics.detections;
		detections.resize(footage.size());
		forEach(footage.size(), request.execution,
				[&](std::size_t i)
				{
					const image::Image frame = footage.image(i);
					detections[i] = cb::detect(frame, footage.frame(i), board, request.detection);
				});
		return analyse(std::move(report), image, board, request, std::get<std::unique_ptr<Estimator>>(prepared).get(), start);
	}
} // namespace lain::camera::calibration::board
