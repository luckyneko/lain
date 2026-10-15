#include "lain/camera/feature/sequenceextraction.h"

#include "cells.h"
#include "execution.h"
#include "lain/camera/feature/geometry.h"

#include <lain/core/sha256.h>
#include <lain/string/format.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace lain::camera::feature
{
	using detail::cell;
	using detail::CellPair;
	using detail::Cells;

	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// The checks on the request alone; the first problem, or nothing. The settings every extraction
	// shares are checked in one place (cells.h); a fundamental matrix needs eight inliers.
	static std::optional<std::string> requestProblem(const SequenceExtractionRequest& r)
	{
		if (r.samples < 2)
			return string::format("a sample count of {} gives no frame pair to match", r.samples);
		if (std::optional<std::string> problem = detail::settingsProblem(r, 8, "a fundamental matrix"))
			return problem;
		if (!(std::isfinite(r.threshold) && r.threshold > 0))
			return string::format("an inlier distance of {} px is not positive", r.threshold);
		return std::nullopt;
	}

	// A frame's place in the capture: its timestamp, then its ordinal, then its source.
	static auto captureOrder(const media::FrameRef& frame)
	{
		return std::make_tuple(frame.timestamp, frame.ordinal, frame.source.toString());
	}

	// The text a sequence track's identity is the digest of: a version line, then each observation's
	// camera and its frame's source, length-prefixed so no two different lists spell the same text, its
	// frame's ordinal, and its pixel on the 1/64-pixel grid, as integers so the text does not depend on
	// how a double prints. The fixed rig's text (extraction.cpp) is its own, so neither changes the
	// other's identities, and the version line changes if this does.
	static std::string canonicalText(const Track& track, const std::vector<View>& views)
	{
		std::string text = "lain sequence feature track 1\n";
		for (const SceneObservation& o : track.observations)
		{
			const View& view = views[o.view];
			const std::string& camera = view.camera.value;
			const std::string source = view.frames.front().source.toString();
			text += std::to_string(camera.size()) + ":" + camera + " ";
			text += std::to_string(source.size()) + ":" + source + " " + std::to_string(view.frames.front().ordinal) + " ";
			text += std::to_string(cell(o.pixel.x)) + " " + std::to_string(cell(o.pixel.y)) + "\n";
		}
		return text;
	}

	// What one sampled frame's task produced.
	struct FrameWork
	{
		std::size_t position = 0; // in the footage
		media::FrameRef frame;
		Cells cells;
		FrameExtraction counts;
		Status failure = Status::Ok;
		std::string detail;
		std::optional<Provenance> extractor;
	};

	static void extractFrame(const capture::CameraFootage& camera, FrameWork& work, const SequenceExtractionRequest& request)
	{
		detail::ImageCells found;
		{
			const image::Image image = camera.footage.image(work.position);
			if (!image.valid())
			{
				work.counts.failed = true;
				return;
			}
			found = detail::imageCells(image, request);
		} // the frame is released before its features are kept
		work.extractor = found.provenance;
		if (!found.ok())
		{
			work.failure = found.status;
			work.detail = string::format("camera \"{}\", {}: {}", camera.camera.value, work.frame.toString(), found.detail);
			return;
		}
		work.counts.scale = found.scale;
		work.counts.found = found.found;
		work.counts.orientations = found.orientations;
		work.counts.capped = found.capped;
		work.counts.features = std::uint32_t(found.cells.size());
		work.cells = std::move(found.cells);
	}

	// What one frame pair's task produced.
	struct PairWork
	{
		PairExtraction entry;
		detail::VerifiedMatches verified;
		Status failure = Status::Ok;
		std::string detail;
		std::optional<Provenance> matcher;
		std::optional<Provenance> geometry;
	};

	// One frame pair: pre-screened, matched and verified by a fundamental matrix on source pixels, its
	// threshold turned from the pixels searched into source pixels by the frames' scale. A sequence is
	// homogeneous, so its frames are searched at one scale.
	static void extractPair(const FrameWork& a, const FrameWork& b, PairWork& work, const SequenceExtractionRequest& request)
	{
		work.verified.a = work.entry.a;
		work.verified.b = work.entry.b;
		const detail::MatchedCells matched = detail::matchCells(a.cells, b.cells, work.entry, request, "features");
		work.matcher = matched.matcher;
		if (matched.failure != Status::Ok)
		{
			work.failure = matched.failure;
			work.detail = string::format("{} and {}: {}", a.frame.toString(), b.frame.toString(), matched.detail);
			return;
		}
		if (!matched.verify)
			return;
		const std::vector<CellPair>& matches = matched.matches;

		std::vector<PixelPair> pairs;
		pairs.reserve(matches.size());
		for (const auto& [ca, cb] : matches)
			pairs.push_back({a.cells.keypoint(ca).pixel, b.cells.keypoint(cb).pixel});
		FundamentalRequest verification;
		verification.threshold = request.threshold / a.counts.scale;
		verification.seed = request.seed;
		const FundamentalResult f = fundamental(pairs, verification);
		work.geometry = f.provenance;
		if (!f.ok())
		{
			work.entry.outcome = PairOutcome::Unverified;
			work.entry.detail = f.detail;
			return;
		}
		work.entry.inliers = std::uint32_t(f.inliers.size());
		if (f.inliers.size() < request.minimumPairInliers)
		{
			work.entry.outcome = PairOutcome::Unverified;
			work.entry.detail = string::format("its best fundamental matrix explains {} of {} matches; a pair needs {}",
											   f.inliers.size(), matches.size(), request.minimumPairInliers);
			return;
		}
		work.entry.outcome = PairOutcome::Verified;
		for (const std::uint32_t k : f.inliers)
			work.verified.matches.push_back(matches[k]);
	}

	// --- extractSequenceTracks ------------------------------------------------------------

	SequenceExtractionResult extractSequenceTracks(const capture::CameraFootage& camera, const SequenceExtractionRequest& request)
	{
		const core::Time start = core::Time::now();
		SequenceExtractionResult out;
		out.report.request = request;
		const auto finish = [&](Status status, std::string detail)
		{
			out.status = status;
			out.detail = std::move(detail);
			out.report.elapsed = core::Time::now() - start;
			return std::move(out);
		};

		// 1. Every check that needs no frame.
		if (!canExtract())
			return finish(Status::NoBackend, "this build has no feature extractor (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!canMatch())
			return finish(Status::NoBackend, "this build has no feature matcher (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!canSolveGeometry())
			return finish(Status::NoBackend, "this build has no geometry solver (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (const std::optional<std::string> problem = requestProblem(request))
			return finish(Status::Unsupported, *problem);
		if (const std::optional<std::string> problem = detail::searchProblem(request))
			return finish(Status::Unsupported, *problem);
		const std::size_t n = camera.footage.size();
		if (n < 2)
			return finish(Status::TooFew, string::format("{} frame(s): a track joins two", n));
		if (camera.camera.empty())
			return finish(Status::InvalidInput, "the camera has no identity");

		// 2. The frames in capture order, then the ones to sample: evenly across it, at the centres of
		// equal bins. One frame given twice would be two views of one image.
		std::vector<std::pair<media::FrameRef, std::size_t>> ordered; // (frame, position)
		for (std::size_t p = 0; p < n; ++p)
			ordered.push_back({camera.footage.frame(p), p});
		std::sort(ordered.begin(), ordered.end(), [](const auto& x, const auto& y)
				  { return std::make_tuple(captureOrder(x.first), x.second) < std::make_tuple(captureOrder(y.first), y.second); });
		for (std::size_t i = 1; i < n; ++i)
		{
			if (ordered[i].first == ordered[i - 1].first)
				return finish(Status::InvalidInput, ordered[i].first.toString() + " is given twice");
		}
		const std::size_t k = std::min<std::size_t>(request.samples, n);
		std::vector<FrameWork> frames(k);
		for (std::size_t i = 0; i < k; ++i)
		{
			const auto& [frame, position] = ordered[(2 * i + 1) * n / (2 * k)];
			frames[i].frame = frame;
			frames[i].position = position;
		}

		// 3. Each sampled frame's features, one task per frame.
		detail::forEach(k, request.execution, [&](std::size_t i)
						{ extractFrame(camera, frames[i], request); });
		const math::Vec2i extent = camera.footage.spec().extent;
		const ImageGeometry image{std::uint32_t(std::max(extent.x, 0)), std::uint32_t(std::max(extent.y, 0))};
		for (const FrameWork& f : frames)
		{
			if (f.failure != Status::Ok)
				return finish(f.failure, f.detail);
			if (f.extractor && out.report.extractor.backend.empty())
				out.report.extractor = *f.extractor;
			out.report.frames.push_back(f.counts);
			View view;
			view.camera = camera.camera;
			view.image = image;
			if (!f.counts.failed)
				view.frames.push_back(f.frame);
			out.trackSet.views.push_back(std::move(view));
		}

		// 4. Every frame pair, one task per pair.
		std::vector<PairWork> pairs;
		for (std::uint32_t a = 0; a < k; ++a)
		{
			for (std::uint32_t b = a + 1; b < k; ++b)
			{
				pairs.emplace_back();
				pairs.back().entry.a = a;
				pairs.back().entry.b = b;
			}
		}
		detail::forEach(pairs.size(), request.execution, [&](std::size_t p)
						{ extractPair(frames[pairs[p].entry.a], frames[pairs[p].entry.b], pairs[p], request); });
		for (const PairWork& p : pairs)
		{
			if (p.failure != Status::Ok)
				return finish(p.failure, p.detail);
			if (p.matcher && out.report.matcher.backend.empty())
				out.report.matcher = *p.matcher;
			if (p.geometry && out.report.geometry.backend.empty())
				out.report.geometry = *p.geometry;
			out.report.pairs.push_back(p.entry);
		}

		// 5. The verified matches joined into tracks, serially in canonical pair order.
		std::vector<std::uint32_t> cellsPerView;
		for (const FrameWork& f : frames)
			cellsPerView.push_back(std::uint32_t(f.cells.size()));
		std::vector<detail::VerifiedMatches> verified;
		for (PairWork& p : pairs)
			verified.push_back(std::move(p.verified));
		const detail::JoinedTracks joined = detail::joinTracks(cellsPerView, verified);
		out.report.conflicts = joined.conflicts;
		out.report.conflictObservations = joined.conflictObservations;
		for (const std::vector<CellPair>& members : joined.tracks)
		{
			Track track;
			for (const auto& [v, c] : members)
			{
				// A feature's size is in source pixels whatever the scale searched, so its covariance is
				// too, as a fixed rig observation's is.
				const Keypoint& key = frames[v].cells.keypoint(c);
				const double sigma = request.localisationPerSize * key.size;
				track.observations.push_back({v, key.pixel, 1, std::array<double, 3>{sigma * sigma, 0.0, sigma * sigma}});
			}
			track.identity = core::sha256(canonicalText(track, out.trackSet.views)).toString();
			out.trackSet.tracks.push_back(std::move(track));
		}
		std::sort(out.trackSet.tracks.begin(), out.trackSet.tracks.end(), [](const Track& a, const Track& b)
				  { return a.identity < b.identity; });
		assert(std::adjacent_find(out.trackSet.tracks.begin(), out.trackSet.tracks.end(), [](const Track& a, const Track& b)
								  { return a.identity == b.identity; }) == out.trackSet.tracks.end());
		out.report.tracks = std::uint32_t(out.trackSet.tracks.size());
		return finish(Status::Ok, {});
	}
} // namespace lain::camera::feature
