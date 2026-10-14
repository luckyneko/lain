#include "reconstruction.h"

#include "lain/camera/feature/triangulation.h"
#include "lain/camera/projection.h"
#include "median.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace lain::camera::reconstruction
{
	using detail::median;

	// Where a triangulation becomes a point at infinity: the facades' default inlier angle, whatever
	// angle the request poses with.
	static double minimumParallax()
	{
		return feature::GeometryRequest{}.angle;
	}

	std::vector<std::vector<std::uint32_t>> viewsOf(const Evidence& evidence, const Units& units)
	{
		std::vector<std::vector<std::uint32_t>> out;
		out.reserve(units.size());
		for (const std::uint32_t u : units)
		{
			std::vector<std::uint32_t> views;
			for (const Seen& s : evidence.tracks[u].seen)
				views.push_back(s.view);
			out.push_back(std::move(views));
		}
		return out;
	}

	std::vector<std::vector<std::uint32_t>> unitsOf(std::size_t views, const std::vector<std::vector<std::uint32_t>>& units)
	{
		std::vector<std::vector<std::uint32_t>> out(views);
		for (std::uint32_t u = 0; u < units.size(); ++u)
		{
			for (const std::uint32_t v : units[u])
				out[v].push_back(u);
		}
		return out;
	}

	// --- placeability --------------------------------------------------------------

	bool Closure::covers(const std::vector<std::uint32_t>& views) const
	{
		return std::all_of(views.begin(), views.end(), [this](std::uint32_t v)
						   { return bool(placed[v]); });
	}

	Closure closure(const std::vector<std::vector<std::uint32_t>>& units, const std::vector<std::vector<std::uint32_t>>& ofView,
					std::uint32_t a, std::uint32_t b)
	{
		const std::size_t views = ofView.size();
		Closure out;
		out.a = a;
		out.b = b;
		out.placed.assign(views, false);
		out.rank.assign(views, std::numeric_limits<std::uint32_t>::max());
		out.support.assign(views, 0);
		std::vector<std::uint32_t> inPlaced(units.size(), 0), count(views, 0);
		std::uint32_t next = 0;
		const auto place = [&](std::uint32_t v)
		{
			out.placed[v] = true;
			out.rank[v] = next++;
			for (const std::uint32_t u : ofView[v])
			{
				if (++inPlaced[u] != 2)
					continue;
				for (const std::uint32_t other : units[u])
				{
					if (!out.placed[other])
						++count[other];
				}
			}
		};
		for (const std::uint32_t u : ofView[a])
		{
			if (std::find(units[u].begin(), units[u].end(), b) != units[u].end())
				++out.shared;
		}
		place(a);
		place(b);
		for (;;)
		{
			std::optional<std::uint32_t> best;
			for (std::uint32_t v = 0; v < views; ++v)
			{
				if (!out.placed[v] && count[v] >= kPlaceLandmarks && (!best || count[v] > count[*best]))
					best = v;
			}
			if (!best)
				break;
			out.support[*best] = count[*best];
			place(*best);
		}
		return out;
	}

	Candidates candidates(const ObservationGraph& graph, const std::vector<std::vector<std::uint32_t>>& units,
						  const std::vector<std::vector<std::uint32_t>>& ofView, const std::vector<std::uint32_t>& required,
						  std::size_t limit)
	{
		std::vector<bool> isRequired(ofView.size(), false);
		for (const std::uint32_t v : required)
			isRequired[v] = true;
		std::vector<const GraphEdge*> order;
		for (const GraphEdge& e : graph.edges)
		{
			if (e.shared >= kSeedShared && isRequired[e.a] && isRequired[e.b])
				order.push_back(&e);
		}
		std::stable_sort(order.begin(), order.end(), [](const GraphEdge* x, const GraphEdge* y)
						 { return x->shared > y->shared; });

		Candidates out;
		std::vector<std::vector<bool>> shortOf;
		for (const GraphEdge* e : order)
		{
			const bool inside = std::any_of(shortOf.begin(), shortOf.end(), [e](const std::vector<bool>& placed)
											{ return placed[e->a] && placed[e->b]; });
			if (inside)
				continue;
			Closure c = closure(units, ofView, e->a, e->b);
			if (!out.top)
				out.top = c;
			if (c.covers(required))
			{
				out.covering.push_back(std::move(c));
				if (out.covering.size() == limit)
					break;
			}
			else
				shortOf.push_back(c.placed);
		}
		return out;
	}

	// --- initialisation ------------------------------------------------------------

	std::optional<Seed> chooseSeed(const Evidence& evidence, const Units& units, const std::vector<Closure>& covering,
								   const feature::GeometryRequest& request, Provenance& geometry)
	{
		std::optional<Seed> best, widest;
		for (const Closure& candidate : covering)
		{
			std::vector<feature::RayPair> pairs;
			for (const std::uint32_t u : units)
			{
				const Seen* inA = nullptr;
				const Seen* inB = nullptr;
				for (const Seen& s : evidence.tracks[u].seen)
				{
					if (s.view == candidate.a)
						inA = &s;
					else if (s.view == candidate.b)
						inB = &s;
				}
				if (inA && inB)
					pairs.push_back({inA->ray, inB->ray});
			}
			const feature::GeometryResult result = feature::relativePose(pairs, request);
			if (geometry.backend.empty())
				geometry = result.provenance;
			if (!result.ok())
				continue;
			Seed seed;
			seed.a = candidate.a;
			seed.b = candidate.b;
			seed.bFromA = *result.pose;
			seed.inliers = std::uint32_t(result.inliers.size());
			const math::RigidTransformd aFromB = seed.bFromA.inverse();
			std::vector<double> angles;
			for (const std::uint32_t i : result.inliers)
			{
				const feature::Triangulation t = feature::triangulate(
					{{math::Vec3d{0.0}, pairs[i].a}, {aFromB.translation(), aFromB.rotate(pairs[i].b)}}, minimumParallax());
				if (t.ok())
					angles.push_back(t.angle);
			}
			seed.angle = angles.empty() ? 0.0 : median(std::move(angles));
			if (seed.angle >= kSeedAngle && (!best || seed.inliers > best->inliers))
				best = seed;
			if (!widest || seed.angle > widest->angle)
				widest = seed;
		}
		if (best)
			return best;
		if (widest)
			widest->belowFloor = true;
		return widest;
	}

	Placement place(const Evidence& evidence, const Units& units, const Seed& seed, const feature::GeometryRequest& request)
	{
		const std::size_t n = evidence.models.size();
		Placement out;
		out.viewFromWorld.assign(n, std::nullopt);
		out.landmarks.assign(units.size(), std::nullopt);
		std::vector<std::vector<std::uint32_t>> entriesOf(n);
		for (std::uint32_t e = 0; e < units.size(); ++e)
		{
			for (const Seen& s : evidence.tracks[units[e]].seen)
				entriesOf[s.view].push_back(e);
		}
		std::vector<std::uint32_t> landmarksSeen(n, 0); // per unplaced view

		const auto triangulateEntry = [&](std::uint32_t e)
		{
			if (out.landmarks[e])
				return;
			std::vector<feature::Ray> rays;
			for (const Seen& s : evidence.tracks[units[e]].seen)
			{
				if (!out.viewFromWorld[s.view])
					continue;
				const math::RigidTransformd worldFromView = out.viewFromWorld[s.view]->inverse();
				rays.push_back({worldFromView.translation(), worldFromView.rotate(s.ray)});
			}
			if (rays.size() < 2)
				return;
			const feature::Triangulation t = feature::triangulate(rays, minimumParallax());
			if (!t.ok())
				return;
			out.landmarks[e] = t.point;
			for (const Seen& s : evidence.tracks[units[e]].seen)
			{
				if (!out.viewFromWorld[s.view])
					++landmarksSeen[s.view];
			}
		};
		const auto setView = [&](std::uint32_t v, const math::RigidTransformd& viewFromWorld)
		{
			out.viewFromWorld[v] = viewFromWorld;
			out.order.push_back(v);
			for (const std::uint32_t e : entriesOf[v])
				triangulateEntry(e);
		};
		setView(seed.a, math::RigidTransformd{});
		setView(seed.b, seed.bFromA);

		for (;;)
		{
			std::vector<std::uint32_t> waiting;
			for (std::uint32_t v = 0; v < n; ++v)
			{
				if (!out.viewFromWorld[v] && landmarksSeen[v] >= kPlaceLandmarks)
					waiting.push_back(v);
			}
			std::stable_sort(waiting.begin(), waiting.end(), [&](std::uint32_t x, std::uint32_t y)
							 { return landmarksSeen[x] > landmarksSeen[y]; });
			bool placedOne = false;
			for (const std::uint32_t v : waiting)
			{
				std::vector<feature::PointRay> pointRays;
				for (const std::uint32_t e : entriesOf[v])
				{
					if (!out.landmarks[e])
						continue;
					for (const Seen& s : evidence.tracks[units[e]].seen)
					{
						if (s.view == v)
							pointRays.push_back({*out.landmarks[e], s.ray});
					}
				}
				const feature::GeometryResult result = feature::absolutePose(pointRays, request);
				if (!result.ok())
					continue;
				setView(v, *result.pose);
				placedOne = true;
				break;
			}
			if (!placedOne)
				break;
		}
		return out;
	}

	Start startFrom(const Evidence& evidence, const Units& units, const Placement& placement, std::uint32_t reference)
	{
		const std::size_t n = evidence.models.size();
		Start out;
		const math::RigidTransformd& referenceFromWorld = *placement.viewFromWorld[reference];
		const math::RigidTransformd worldFromReference = referenceFromWorld.inverse();
		for (std::size_t v = 0; v < n; ++v)
			out.viewFromReference.push_back(*placement.viewFromWorld[v] * worldFromReference);
		out.viewFromReference[reference] = math::RigidTransformd{};
		out.landmarks.assign(units.size(), std::nullopt);
		out.kept.resize(units.size());
		std::vector<std::uint32_t> seen(n, 0);
		for (std::size_t e = 0; e < units.size(); ++e)
		{
			const std::vector<Seen>& members = evidence.tracks[units[e]].seen;
			out.kept[e].assign(members.size(), false);
			if (!placement.landmarks[e])
				continue;
			const math::Vec3d point = referenceFromWorld.apply(*placement.landmarks[e]);
			std::uint32_t kept = 0;
			for (std::size_t m = 0; m < members.size(); ++m)
			{
				const math::Vec3d q = out.viewFromReference[members[m].view].apply(point);
				if (project(evidence.models[members[m].view], q.x, q.y, q.z).ok())
				{
					out.kept[e][m] = true;
					++kept;
				}
				else
					++out.behind;
			}
			if (kept < 2)
			{
				out.kept[e].assign(members.size(), false);
				continue;
			}
			out.landmarks[e] = point;
			for (std::size_t m = 0; m < members.size(); ++m)
			{
				if (out.kept[e][m])
					++seen[members[m].view];
			}
		}
		for (std::uint32_t v = 0; v < n; ++v)
		{
			if (seen[v] < kPlaceLandmarks)
				out.starved.push_back(v);
		}
		return out;
	}
} // namespace lain::camera::reconstruction
