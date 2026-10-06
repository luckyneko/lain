#pragma once

#include "lain/camera/feature/features.h"
#include "lain/camera/feature/status.h"
#include "lain/camera/provenance.h"

#include <lain/core/factory.h>
#include <lain/core/time.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Matching two images' features (CONTEXT.md, "Feature match"). A matcher backend proposes each
// feature's two nearest neighbours in the other image; lain applies the ratio and mutual tests
// (ADR-0016, "Producers propose; lain decides").
namespace lain::camera::feature
{
	// How a matcher searches (ADR-0016, amended 2026-10-06). Exact is brute force, and the same on
	// every run; Approximate is faster and reproducible only within a tolerance.
	enum class MatchSearch
	{
		Exact,
		Approximate,
	};

	// One feature of the other image, and how far its descriptor is from the one searched for.
	struct Neighbour
	{
		std::uint32_t index = 0;
		double distance = 0;
	};

	// A matcher backend (ADR-0004's service shape). It declares the descriptor kinds it reads and
	// the searches it can run, as an estimator declares the models it can estimate.
	class Matcher
	{
	public:
		virtual ~Matcher() = default;

		virtual Provenance provenance() const = 0;
		virtual bool accepts(std::string_view kind) const = 0;
		virtual bool supports(MatchSearch search) const = 0;

		// For each feature of `a`, its two nearest features in `b`, nearest first. Called only with
		// at least two features in each, of a kind this matcher accepts, by a search it supports.
		virtual std::vector<std::array<Neighbour, 2>> nearest(const Features& a, const Features& b,
															  MatchSearch search) const = 0;
	};

	// The process-wide matcher registry, keyed by backend name ("opencv").
	core::Factory<Matcher>& matcherRegistry();

	// Whether this build can match features at all: whether any backend registered.
	bool canMatch();

	struct MatchRequest
	{
		MatchSearch search = MatchSearch::Exact;
		// A feature's nearest neighbour must be nearer than this fraction of its second nearest
		// (Lowe's ratio test), in (0, 1].
		double ratio = 0.8;
	};

	// A pair of features that passed both tests, and the distance between their descriptors.
	struct Match
	{
		std::uint32_t a = 0;
		std::uint32_t b = 0;
		double distance = 0;
	};

	// What became of each feature of `a`: matched, or rejected for exactly one reason.
	struct MatchCounts
	{
		std::uint32_t candidates = 0; // features of a examined
		std::uint32_t notMutual = 0;  // its nearest in b has a different nearest in a
		std::uint32_t ambiguous = 0;  // mutual, but the ratio test failed in one direction or both
	};

	struct MatchResult
	{
		Status status = Status::NoSolution;
		std::string detail;			// why there are no matches, when the status is not Ok
		std::vector<Match> matches; // ascending by a
		MatchCounts counts;
		Provenance provenance;
		core::Time elapsed;

		bool ok() const { return status == Status::Ok; }
	};

	// The features of `a` and `b` that match. A pair is kept when each is the other's nearest
	// neighbour and each passes the ratio test against its own second nearest, so match(a, b) is
	// match(b, a) with the sides swapped, and an exact tie is never a match.
	//
	// Answered by the first registered matcher (in registry order) that accepts the kind and supports
	// the search; Unsupported when none does, NoBackend when none is registered at all.
	MatchResult match(const Features& a, const Features& b, const MatchRequest& request = {});
} // namespace lain::camera::feature
