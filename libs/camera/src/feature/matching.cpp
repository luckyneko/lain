#include "lain/camera/feature/matching.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <cmath>
#include <memory>
#include <utility>

namespace lain::camera::feature
{
	// A nearest() answer against its contract: one entry per searched feature, two distinct neighbours
	// in range, nearest first, at finite non-negative distances. The first breach, or nothing.
	static std::optional<std::string> breach(const std::vector<std::array<Neighbour, 2>>& nearest, std::size_t searched,
											 std::size_t among)
	{
		if (nearest.size() != searched)
			return string::format("{} answers for {} features", nearest.size(), searched);
		for (std::size_t i = 0; i < nearest.size(); ++i)
		{
			const std::array<Neighbour, 2>& n = nearest[i];
			if (n[0].index >= among || n[1].index >= among || n[0].index == n[1].index)
				return string::format("feature {}'s neighbours are not two of the {} features searched", i, among);
			if (!std::isfinite(n[0].distance) || !std::isfinite(n[1].distance) || n[0].distance < 0 ||
				n[0].distance > n[1].distance)
				return string::format("feature {}'s distances are not finite, non-negative and nearest first", i);
		}
		return std::nullopt;
	}

	core::Factory<Matcher>& matcherRegistry()
	{
		static core::Factory<Matcher> registry;
		return registry;
	}

	bool canMatch()
	{
		return !matcherRegistry().keys().empty();
	}

	MatchResult match(const Features& a, const Features& b, const MatchRequest& request)
	{
		const core::Time start = core::Time::now();
		MatchResult out;
		const auto finish = [&](Status status, std::string detail)
		{
			out.status = status;
			out.detail = std::move(detail);
			out.elapsed = core::Time::now() - start;
			return std::move(out);
		};

		const std::vector<std::string> backends = matcherRegistry().keys();
		if (backends.empty())
			return finish(Status::NoBackend, "this build has no feature matcher (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (a.kind != b.kind || a.descriptorBytes != b.descriptorBytes)
		{
			return finish(Status::Unsupported,
						  string::format("'{}' features of {} bytes and '{}' features of {} bytes cannot be matched", a.kind,
										 a.descriptorBytes, b.kind, b.descriptorBytes));
		}
		if (!(request.ratio > 0 && request.ratio <= 1))
			return finish(Status::Unsupported, string::format("a ratio of {} is outside (0, 1]", request.ratio));

		// The first matcher, in registry order, that reads this kind and runs this search.
		std::unique_ptr<Matcher> matcher;
		bool anyAccepts = false;
		for (const std::string& key : backends)
		{
			std::unique_ptr<Matcher> candidate = matcherRegistry().create(key);
			if (!candidate->accepts(a.kind))
				continue;
			anyAccepts = true;
			if (candidate->supports(request.search))
			{
				matcher = std::move(candidate);
				break;
			}
		}
		if (!anyAccepts)
			return finish(Status::Unsupported, string::format("no matcher accepts '{}' features", a.kind));
		if (!matcher)
		{
			return finish(Status::Unsupported, string::format("no matcher of '{}' features supports {} search", a.kind,
															  meta::enums::name(request.search)));
		}
		out.provenance = matcher->provenance();
		if (a.size() < 2 || b.size() < 2)
		{
			return finish(Status::TooFew, string::format("{} and {} features: the ratio test needs a second nearest "
														 "neighbour on each side",
														 a.size(), b.size()));
		}

		const std::vector<std::array<Neighbour, 2>> forward = matcher->nearest(a, b, request.search);
		const std::vector<std::array<Neighbour, 2>> reverse = matcher->nearest(b, a, request.search);
		std::optional<std::string> wrong = breach(forward, a.size(), b.size());
		if (!wrong)
			wrong = breach(reverse, b.size(), a.size());
		if (wrong)
		{
			return finish(Status::BackendMisbehaved,
						  string::format("the {} matcher's answer is malformed: {}", out.provenance.backend, *wrong));
		}

		// Each the other's nearest (mutual), and each clearly nearer than its own second nearest (the
		// ratio test, both ways), so the pairs do not depend on which image came first.
		out.counts.candidates = std::uint32_t(a.size());
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			const std::array<Neighbour, 2>& there = forward[i];
			const std::array<Neighbour, 2>& back = reverse[there[0].index];
			if (back[0].index != i)
			{
				++out.counts.notMutual;
				continue;
			}
			if (!(there[0].distance < request.ratio * there[1].distance) ||
				!(back[0].distance < request.ratio * back[1].distance))
			{
				++out.counts.ambiguous;
				continue;
			}
			out.matches.push_back({std::uint32_t(i), there[0].index, there[0].distance});
		}
		return finish(Status::Ok, {});
	}
} // namespace lain::camera::feature
