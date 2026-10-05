#include "fixture.h"

#include <lain/camera/calibration/board.h>
#include <lain/camera/distortion.h>
#include <lain/core/length.h>
#include <lain/core/uri.h>
#include <lain/io/data/load.h>
#include <lain/io/data/save.h>
#include <lain/io/sequence/open.h>
#include <lain/log/log.h>
#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <system_error>
#include <utility>

namespace lain::camera::fixture
{
	namespace fs = std::filesystem;
	namespace cal = calibration;

	FixtureLoad loadFixture(const fs::path& root)
	{
		FixtureLoad result;
		const auto problem = [&result](std::string text)
		{ result.problems.push_back(std::move(text)); };

		const fs::path file = root / kFixtureFile;
		std::error_code ec;
		if (!fs::is_regular_file(file, ec))
		{
			problem(lain::string::format("there is no {} in '{}'", kFixtureFile, root.generic_string()));
			return result;
		}
		const std::optional<data::Value> value = io::data::load(core::Uri::fromPath(file));
		if (!value)
		{
			problem(lain::string::format("'{}' could not be read", file.generic_string()));
			return result;
		}

		// A fixture is typed by a person, so it is read strictly: a misspelled key is named, never
		// replaced by a default that would quietly calibrate something else.
		const data::StrictRead<FixtureDocument> read = data::fromValueStrict<FixtureDocument>(*value);
		for (const std::string& p : read.problems)
			problem(lain::string::format("{}: {}", kFixtureFile, p));
		if (!read.value)
			return result;
		const FixtureDocument& document = *read.value;

		if (document.version != kFixtureVersion)
		{
			problem(lain::string::format("{} is version {}, and this build reads version {}", kFixtureFile,
										 document.version, kFixtureVersion));
			return result;
		}

		const board::SpecificationRead board = board::specificationFrom(document.board);
		for (const std::string& p : board.problems)
			problem("board: " + p);

		std::vector<Session> sessions;
		std::set<std::string> names;
		for (const SessionDocument& s : document.sessions)
		{
			const std::string label = "session '" + s.name + "'";
			if (s.name.empty())
				problem("a session has no name");
			else if (!names.insert(s.name).second)
				problem(label + " is named twice");

			Session session;
			session.name = s.name;
			session.capture = s.capture;

			const fs::path frames = root / s.frames;
			if (s.frames.empty() || !fs::is_directory(frames, ec))
			{
				problem(lain::string::format("{}: frames folder '{}' does not exist", label, s.frames));
				continue;
			}
			std::optional<media::FrameSequence> footage = io::sequence::open(core::Uri::fromPath(frames));
			if (!footage || footage->empty())
			{
				problem(lain::string::format("{}: frames folder '{}' holds no frames", label, s.frames));
				continue;
			}
			const math::Vec2i extent = footage->spec().extent;
			session.image = ImageGeometry{std::uint32_t(extent.x), std::uint32_t(extent.y)};
			session.footage = std::move(*footage);

			// The record describes the stream as the device delivered it. Frames of another size were
			// resized or cropped on the way, and a calibration of them is not a calibration of that
			// stream, nor comparable with the imported model stated for it. An unknown size (0) is
			// not held against the frames.
			const StreamDescription& stream = s.capture.stream;
			if ((stream.width != 0 || stream.height != 0) &&
				(stream.width != session.image.width || stream.height != session.image.height))
			{
				problem(lain::string::format("{}: the frames are {}x{}, and the capture record says the stream was {}x{}",
											 label, session.image.width, session.image.height, stream.width,
											 stream.height));
			}

			if (s.capture.importedModel)
			{
				const ModelResult created = CameraModel::create(s.capture.importedModel->parameters);
				for (const ModelDiagnostic& d : created.diagnostics)
					problem(lain::string::format("{}: imported model: {}", label, d.detail));
				if (created.model && !(created.model->image() == session.image))
				{
					problem(lain::string::format("{}: the imported model is for {}x{}, and the frames are {}x{}", label,
												 created.model->image().width, created.model->image().height,
												 session.image.width, session.image.height));
				}
				session.imported = created.model;
			}
			sessions.push_back(std::move(session));
		}
		if (document.sessions.empty())
			problem("the fixture has no sessions");

		if (!result.problems.empty())
			return result;
		result.fixture = Fixture{document.name, root, *board.specification, document.model, std::move(sessions)};
		return result;
	}

	FixtureDocument templateDocument(const std::string& name)
	{
		FixtureDocument document;
		document.name = name;
		document.model = DistortionModel::BrownConrady5;
		document.board.pattern.dictionary = board::Dictionary::Aruco5x5_100;
		document.board.pattern.squaresX = 9;
		document.board.pattern.squaresY = 6;
		document.board.pattern.markerToSquare = 0.75;
		document.board.instance.identity = "lain-9x6-30mm";
		document.board.instance.squareLength.value = core::Length::from<core::Length::Millimetres>(30);
		document.sessions.push_back({"a", "a", CaptureRecord{}});
		document.sessions.push_back({"b", "b", CaptureRecord{}});
		return document;
	}

	bool writeFixtureDocument(const fs::path& root, const FixtureDocument& document)
	{
		const fs::path file = root / kFixtureFile;
		if (!io::data::save(core::Uri::fromPath(file), data::toValue(document)))
		{
			log::error("could not write '{}'", file.generic_string());
			return false;
		}
		return true;
	}

	namespace
	{
		// `model` held exactly and checked against every usable view in `detections`. No resampling:
		// a held model has nothing to resample.
		cal::Report hold(const CameraModel& model, const std::vector<board::DetectionReport>& detections,
						 const ImageGeometry& image, const board::Specification& board, const cal::Request& request)
		{
			cal::Request held = request;
			held.imported = model;
			held.importedPolicy = cal::ImportedModelPolicy::HoldAndValidate;
			held.resamples = 0;
			return cal::board::calibrate(detections, image, board, held);
		}

		std::string describe(const std::variant<cal::HeldOutEvidence, cal::Unavailable>& evidence)
		{
			if (const auto* e = std::get_if<cal::HeldOutEvidence>(&evidence))
				return lain::string::format("{:.3g} mrad ({:.3g} px) RMS over {} views", e->rmsAngle * 1e3,
											e->rmsPixels, e->views);
			return "unavailable (" + std::get<cal::Unavailable>(evidence).reason + ")";
		}

		std::string describe(const Intrinsics& k)
		{
			return lain::string::format("fx {:.2f}, fy {:.2f}, cx {:.2f}, cy {:.2f}", k.fx, k.fy, k.cx, k.cy);
		}

		std::string describe(const IntrinsicsDifference& d)
		{
			return lain::string::format("focal {:.3g}% apart, principal point {:.3g} mrad apart", d.focal * 100,
										d.principalPoint * 1e3);
		}

		std::string describeBounds(double focal, double principalPoint)
		{
			return lain::string::format("{:.3g}% and {:.3g} mrad", focal * 100, principalPoint * 1e3);
		}
	} // namespace

	IntrinsicsDifference differenceOf(const Intrinsics& a, const Intrinsics& b)
	{
		const double focal = (a.fx + a.fy + b.fx + b.fy) / 4;
		IntrinsicsDifference d;
		d.focal = std::max(std::abs(a.fx - b.fx) / ((a.fx + b.fx) / 2), std::abs(a.fy - b.fy) / ((a.fy + b.fy) / 2));
		d.principalPoint = std::max(std::abs(a.cx - b.cx), std::abs(a.cy - b.cy)) / focal;
		return d;
	}

	FixtureReport runFixture(const Fixture& fixture, const cal::Request& base)
	{
		FixtureReport out;
		out.name = fixture.name;

		// Every session is estimated independently: an imported model is compared below, and never
		// seeds the estimate it is compared with.
		cal::Request request = base;
		request.model = fixture.model;
		request.imported.reset();
		request.importedPolicy = cal::ImportedModelPolicy::Ignore;

		for (const Session& session : fixture.sessions)
			out.sessions.push_back({session.name, cal::board::calibrate(session.footage, fixture.board, request)});

		const auto ready = [](const SessionOutcome& s)
		{ return s.report.verdict == cal::Verdict::Ready && s.report.model.has_value(); };
		const std::size_t readyCount = std::size_t(std::count_if(out.sessions.begin(), out.sessions.end(), ready));
		if (readyCount < 2)
		{
			std::string verdicts;
			for (const SessionOutcome& s : out.sessions)
				verdicts += lain::string::format("{}'{}' {}", verdicts.empty() ? "" : ", ", s.name,
												 meta::enums::name(s.report.verdict));
			out.failures.push_back(lain::string::format(
				"the repeat check needs two Ready sessions, and this fixture has {} of {}{}", readyCount,
				out.sessions.size(), verdicts.empty() ? std::string{} : " (" + verdicts + ")"));
		}

		// Prediction: a Ready model held and validated on each other session's views, Ready or not.
		for (std::size_t a = 0; a < out.sessions.size(); ++a)
		{
			if (!ready(out.sessions[a]))
				continue;
			const cal::Report& own = out.sessions[a].report;
			for (std::size_t b = 0; b < out.sessions.size(); ++b)
			{
				if (a == b)
					continue;
				CrossValidation check;
				check.model = out.sessions[a].name;
				check.views = out.sessions[b].name;
				check.threshold = own.thresholds.ready.maximumHeldOutAngle;
				check.evidence = hold(*own.model, out.sessions[b].report.diagnostics.detections,
									  fixture.sessions[b].image, fixture.board, request)
									 .heldOut;
				const auto* e = std::get_if<cal::HeldOutEvidence>(&check.evidence);
				check.agrees = e != nullptr && e->rmsAngle <= check.threshold;
				if (!check.agrees)
					out.failures.push_back(lain::string::format(
						"session '{}''s model does not predict session '{}''s views: {}, against {:.3g} mrad",
						check.model, check.views, describe(check.evidence), check.threshold * 1e3));
				out.crossValidations.push_back(std::move(check));
			}
		}

		// Agreement: every two Ready estimates, against their combined resampled spread. Ready needs
		// resampling evidence, so a Ready session always has it.
		for (std::size_t a = 0; a < out.sessions.size(); ++a)
		{
			for (std::size_t b = a + 1; b < out.sessions.size(); ++b)
			{
				if (!ready(out.sessions[a]) || !ready(out.sessions[b]))
					continue;
				const auto& sa = std::get<cal::ResamplingEvidence>(out.sessions[a].report.resampling);
				const auto& sb = std::get<cal::ResamplingEvidence>(out.sessions[b].report.resampling);
				IntrinsicsAgreement agreement;
				agreement.first = out.sessions[a].name;
				agreement.second = out.sessions[b].name;
				agreement.difference = differenceOf(out.sessions[a].report.model->intrinsics(),
													out.sessions[b].report.model->intrinsics());
				agreement.focalBound = kAgreementSigmas * std::hypot(sa.focalVariation, sb.focalVariation);
				agreement.principalPointBound =
					kAgreementSigmas * std::hypot(sa.principalPointVariation, sb.principalPointVariation);
				agreement.agrees = agreement.difference.focal <= agreement.focalBound &&
								   agreement.difference.principalPoint <= agreement.principalPointBound;
				if (!agreement.agrees)
					out.failures.push_back(lain::string::format(
						"sessions '{}' and '{}' do not agree on the camera: {}, against {}", agreement.first,
						agreement.second, describe(agreement.difference),
						describeBounds(agreement.focalBound, agreement.principalPointBound)));
				out.agreements.push_back(std::move(agreement));
			}
		}

		// A manufacturer's model beside the estimate. Reported, never gating (ADR-0016): the point of
		// a fixture is lain's calibration, and a factory model is one more measurement to compare.
		for (std::size_t i = 0; i < fixture.sessions.size(); ++i)
		{
			const Session& session = fixture.sessions[i];
			if (!session.imported)
				continue;
			ManufacturerComparison comparison;
			comparison.session = session.name;
			comparison.source = session.capture.importedModel ? session.capture.importedModel->source : std::string{};
			comparison.imported = session.imported->intrinsics();
			comparison.evidence = hold(*session.imported, out.sessions[i].report.diagnostics.detections,
									   session.image, fixture.board, request)
									  .heldOut;
			if (out.sessions[i].report.model)
			{
				comparison.estimated = out.sessions[i].report.model->intrinsics();
				comparison.difference = differenceOf(comparison.imported, *comparison.estimated);
			}
			out.manufacturer.push_back(std::move(comparison));
		}
		return out;
	}

	std::string FixtureReport::toString() const
	{
		std::string text = lain::string::format("fixture '{}': {}\n", name, passed() ? "PASSED" : "FAILED");
		for (const std::string& f : failures)
			text += "  failed: " + f + "\n";
		for (const SessionOutcome& s : sessions)
			text += lain::string::format("  session '{}': {}\n", s.name, s.report.toString());
		for (const CrossValidation& c : crossValidations)
			text += lain::string::format("  '{}' model on '{}' views: {}, Ready needs {:.3g} mrad: {}\n", c.model,
										 c.views, describe(c.evidence), c.threshold * 1e3,
										 c.agrees ? "agrees" : "DISAGREES");
		for (const IntrinsicsAgreement& a : agreements)
			text += lain::string::format("  '{}' and '{}' estimates: {}, within {} ({:g} deviations): {}\n", a.first, a.second,
										 describe(a.difference), describeBounds(a.focalBound, a.principalPointBound),
										 kAgreementSigmas, a.agrees ? "agree" : "DISAGREE");
		for (const ManufacturerComparison& m : manufacturer)
		{
			text += lain::string::format("  session '{}' imported model ({}): {}\n", m.session, m.source,
										 describe(m.evidence));
			text += "    imported:  " + describe(m.imported) + "\n";
			if (m.estimated)
				text += "    estimated: " + describe(*m.estimated) + "\n";
			if (m.difference)
				text += "    " + describe(*m.difference) + " (a diagnostic, never a failure)\n";
		}
		return text;
	}
} // namespace lain::camera::fixture
