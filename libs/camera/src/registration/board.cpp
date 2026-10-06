#include "lain/camera/registration/board.h"

#include "common.h"
#include "execution.h"
#include "lain/camera/board/pose.h"
#include "lain/camera/projection.h"
#include "lain/camera/registration/observationgraph.h"
#include "lain/camera/registration/refiner.h"

#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <variant>

namespace lain::camera::registration::board
{
	namespace cb = camera::board;
	using detail::failed;
	using detail::median;
	using detail::root;
	using detail::rotationBetween;

	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Fewer usable capture groups than this and none are held out: holding one of four out leaves
	// too little to register from and too little to validate with.
	static constexpr std::size_t kMinimumToHoldOut = 5;
	// How many of an edge's shared groups propose relative poses, and score them. Evenly spaced in
	// group order, so a rig with thousands of shared groups initialises in time independent of how
	// many, while a single conflicting group is still outvoted by the median of the rest.
	static constexpr std::size_t kInitialisationGroups = 24;

	// One camera's usable view of the board in one capture group, with the board's pose in it.
	struct Posed
	{
		std::uint32_t camera = 0; // in canonical camera order
		std::uint32_t group = 0;  // in canonical group order
		const cb::Observation* view = nullptr;
		std::vector<math::RigidTransformd> poses; // cameraFromBoard: the backend's first, then the plane's other
		std::vector<math::Vec3d> points;		  // board frame, each corner whose pixel could be unprojected
		std::vector<math::Vec3d> rays;			  // camera frame, unit, parallel to `points`
	};

	// Everything the steps after posing read: the posed observations and, per group, which they are.
	struct Evidence
	{
		std::size_t cameras = 0;
		std::vector<Posed> posed;
		std::vector<std::vector<std::uint32_t>> byGroup; // per canonical group: into `posed`, ascending by camera
	};

	// RMS angle, radians, between each observed ray and the ray to its corner posed by
	// `cameraFromBoard`. The rays were unprojected once, so this is the inner loop of initialisation
	// and costs no projection.
	static double angleRms(const Posed& o, const math::RigidTransformd& cameraFromBoard)
	{
		double sum = 0;
		for (std::size_t i = 0; i < o.points.size(); ++i)
		{
			const math::Vec3d p = cameraFromBoard.apply(o.points[i]);
			const double angle = std::atan2(math::length(math::cross(o.rays[i], p)), math::dot(o.rays[i], p));
			sum += angle * angle;
		}
		return o.points.empty() ? std::numeric_limits<double>::infinity() : std::sqrt(sum / double(o.points.size()));
	}

	// The best fit of any of `o`'s poses: a planar board's two poses are both candidates for it.
	static double bestAngleRms(const Posed& o, const math::RigidTransformd& cameraFromOther,
							   const std::vector<math::RigidTransformd>& otherFromBoard)
	{
		double best = std::numeric_limits<double>::infinity();
		for (const math::RigidTransformd& pose : otherFromBoard)
			best = std::min(best, angleRms(o, cameraFromOther * pose));
		return best;
	}

	// The cameras of each group in `groups`, for the observation graph.
	static std::vector<std::vector<std::uint32_t>> camerasOf(const Evidence& evidence, const std::vector<std::uint32_t>& groups)
	{
		std::vector<std::vector<std::uint32_t>> out;
		out.reserve(groups.size());
		for (const std::uint32_t g : groups)
		{
			std::vector<std::uint32_t> cameras;
			for (const std::uint32_t o : evidence.byGroup[g])
				cameras.push_back(evidence.posed[o].camera);
			out.push_back(std::move(cameras));
		}
		return out;
	}

	// aFromB for one edge of the camera graph. Every shared group sampled proposes a relative pose from
	// each pair of its two poses (four, for two planar ambiguities); each proposal is scored by the
	// MEDIAN, over the sampled groups, of how well it transfers each camera's board into the other,
	// either pose of the board allowed. The lowest score wins, ties to the earlier proposal. This is
	// what chooses between a planar board's two poses, and what outvotes a group whose frames do not
	// agree: a wrong proposal transfers well only in the group that made it.
	static std::optional<math::RigidTransformd> relativePose(const Evidence& evidence, std::uint32_t a, std::uint32_t b,
															 const std::vector<std::uint32_t>& shared)
	{
		std::vector<std::pair<const Posed*, const Posed*>> sample;
		const std::size_t count = std::min(shared.size(), kInitialisationGroups);
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::uint32_t g = shared[i * shared.size() / count];
			const Posed* pa = nullptr;
			const Posed* pb = nullptr;
			for (const std::uint32_t o : evidence.byGroup[g])
			{
				if (evidence.posed[o].camera == a)
					pa = &evidence.posed[o];
				else if (evidence.posed[o].camera == b)
					pb = &evidence.posed[o];
			}
			if (pa && pb)
				sample.push_back({pa, pb});
		}

		std::optional<math::RigidTransformd> best;
		double bestScore = std::numeric_limits<double>::infinity();
		for (const auto& [ownA, ownB] : sample)
		{
			for (const math::RigidTransformd& aFromBoard : ownA->poses)
			{
				for (const math::RigidTransformd& bFromBoard : ownB->poses)
				{
					const math::RigidTransformd aFromB = aFromBoard * bFromBoard.inverse();
					const math::RigidTransformd bFromA = aFromB.inverse();
					std::vector<double> errors;
					errors.reserve(sample.size());
					for (const auto& [pa, pb] : sample)
						errors.push_back((bestAngleRms(*pa, aFromB, pb->poses) + bestAngleRms(*pb, bFromA, pa->poses)) / 2);
					const double score = median(std::move(errors));
					if (score < bestScore)
					{
						bestScore = score;
						best = aFromB;
					}
				}
			}
		}
		return best;
	}

	// Every camera in `root`'s component placed relative to it (cameraFromRoot), through a maximum
	// spanning tree of `graph` weighted by shared groups: the relative pose of each tree edge is
	// estimated from the groups in `groups` that edge rests on. Cameras outside the component stay
	// unset. A failure says which edge could not be estimated.
	struct Initialisation
	{
		std::vector<std::optional<math::RigidTransformd>> cameraFromRoot;
		std::string failure;
	};

	static Initialisation initialise(const Evidence& evidence, const ObservationGraph& graph,
									 const std::vector<std::uint32_t>& groups, std::uint32_t rootCamera)
	{
		const std::size_t n = evidence.cameras;
		Initialisation out;
		out.cameraFromRoot.assign(n, std::nullopt);

		// Kruskal: heaviest edges first, ties in (a, b) order, which graph.edges already is.
		std::vector<std::size_t> order(graph.edges.size());
		std::iota(order.begin(), order.end(), std::size_t{0});
		std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y)
						 { return graph.edges[x].sharedGroups > graph.edges[y].sharedGroups; });
		std::vector<std::uint32_t> parent(n);
		std::iota(parent.begin(), parent.end(), 0u);
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::uint32_t>> tree; // edge -> its shared groups
		for (const std::size_t e : order)
		{
			const GraphEdge& edge = graph.edges[e];
			const std::uint32_t ra = root(parent, edge.a), rb = root(parent, edge.b);
			if (ra == rb)
				continue;
			parent[ra] = rb;
			tree[{edge.a, edge.b}];
		}

		for (const std::uint32_t g : groups)
		{
			const std::vector<std::uint32_t>& members = evidence.byGroup[g];
			for (std::size_t i = 0; i < members.size(); ++i)
			{
				for (std::size_t j = i + 1; j < members.size(); ++j)
				{
					const auto found = tree.find({evidence.posed[members[i]].camera, evidence.posed[members[j]].camera});
					if (found != tree.end())
						found->second.push_back(g);
				}
			}
		}

		std::vector<std::vector<std::pair<std::uint32_t, math::RigidTransformd>>> adjacent(n); // (other, selfFromOther)
		for (const auto& [edge, shared] : tree)
		{
			const std::optional<math::RigidTransformd> aFromB = relativePose(evidence, edge.first, edge.second, shared);
			if (!aFromB)
			{
				out.failure = lain::string::format("no relative pose between cameras {} and {} could be estimated",
												   edge.first, edge.second);
				continue;
			}
			adjacent[edge.first].push_back({edge.second, *aFromB});
			adjacent[edge.second].push_back({edge.first, aFromB->inverse()});
		}

		out.cameraFromRoot[rootCamera] = math::RigidTransformd{};
		std::vector<std::uint32_t> queue{rootCamera};
		for (std::size_t q = 0; q < queue.size(); ++q)
		{
			const std::uint32_t self = queue[q];
			for (const auto& [other, selfFromOther] : adjacent[self])
			{
				if (out.cameraFromRoot[other])
					continue;
				out.cameraFromRoot[other] = selfFromOther.inverse() * *out.cameraFromRoot[self];
				queue.push_back(other);
			}
		}
		return out;
	}

	// referenceFromBoard for group `g`: proposed by each member's poses through its camera, and chosen
	// by the MEDIAN fit across the members, so a member whose frame disagrees with the rest is
	// outvoted. `exclude`, when set, takes no part: a held-out member is predicted, not fitted.
	static math::RigidTransformd seedBody(const Evidence& evidence, std::uint32_t g,
										  const std::vector<math::RigidTransformd>& cameraFromReference,
										  std::optional<std::uint32_t> exclude = std::nullopt)
	{
		math::RigidTransformd best;
		double bestScore = std::numeric_limits<double>::infinity();
		for (const std::uint32_t proposer : evidence.byGroup[g])
		{
			if (exclude && *exclude == proposer)
				continue;
			const Posed& p = evidence.posed[proposer];
			for (const math::RigidTransformd& cameraFromBoard : p.poses)
			{
				const math::RigidTransformd referenceFromBoard = cameraFromReference[p.camera].inverse() * cameraFromBoard;
				std::vector<double> fits;
				for (const std::uint32_t member : evidence.byGroup[g])
				{
					if (exclude && *exclude == member)
						continue;
					const Posed& m = evidence.posed[member];
					fits.push_back(angleRms(m, cameraFromReference[m.camera] * referenceFromBoard));
				}
				const double score = median(std::move(fits));
				if (score < bestScore)
				{
					bestScore = score;
					best = referenceFromBoard;
				}
			}
		}
		return best;
	}

	// The refinement problem over `groups`, one body per entry (a resample may name a group twice).
	static Problem problemOf(const Evidence& evidence, const std::vector<RigCamera>& cameras,
							 const std::vector<std::uint32_t>& groups, const std::vector<math::RigidTransformd>& cameraFromReference,
							 std::uint32_t reference, const cb::Specification& board, const Request& request)
	{
		Problem problem;
		for (const RigCamera& c : cameras)
			problem.models.push_back(c.model);
		problem.cameraFromReference = cameraFromReference;
		problem.reference = reference;
		problem.noise = request.noise;
		problem.loss = request.loss;
		problem.maximumIterations = request.maximumIterations;
		for (std::size_t body = 0; body < groups.size(); ++body)
		{
			problem.referenceFromBody.push_back(seedBody(evidence, groups[body], cameraFromReference));
			for (const std::uint32_t o : evidence.byGroup[groups[body]])
			{
				const Posed& p = evidence.posed[o];
				for (const cb::FeatureObservation& f : p.view->features)
				{
					const std::optional<math::Vec3d> corner = board.cornerPosition(f.id);
					if (corner)
						problem.observations.push_back({p.camera, std::uint32_t(body), *corner, f.pixel, f.covariance});
				}
			}
		}
		return problem;
	}

	// Initialise and refine over `groups`: the whole registration short of its evidence, which is what
	// a bootstrap resample repeats. Unset when the groups do not connect every camera or the refinement
	// gives no usable solution.
	static std::optional<std::vector<math::RigidTransformd>> registerGroups(const Evidence& evidence,
																			const std::vector<RigCamera>& cameras,
																			const std::vector<std::uint32_t>& groups,
																			std::uint32_t reference,
																			const cb::Specification& board,
																			const Request& request)
	{
		const ObservationGraph graph = observationGraph(std::uint32_t(cameras.size()), camerasOf(evidence, groups));
		if (graph.components.size() != 1)
			return std::nullopt;
		const Initialisation init = initialise(evidence, graph, groups, reference);
		std::vector<math::RigidTransformd> start;
		for (const std::optional<math::RigidTransformd>& t : init.cameraFromRoot)
		{
			if (!t)
				return std::nullopt;
			start.push_back(*t);
		}
		const Solution solution = refine(problemOf(evidence, cameras, groups, start, reference, board, request));
		if (!solution.usable() || solution.cameraFromReference.size() != cameras.size())
			return std::nullopt;
		return solution.cameraFromReference;
	}

	static bool usable(const cb::DetectionReport& report)
	{
		return report.status != cb::DetectionStatus::Failed && report.observation.has_value();
	}

	// A registration dataset in canonical order, with each member's camera resolved to an index.
	struct Dataset
	{
		std::vector<RigCamera> cameras;					  // canonical: ascending by identity
		std::vector<const GroupObservations*> groups;	  // canonical: ascending by group identity
		std::vector<std::vector<std::uint32_t>> memberOf; // per group, per member: the camera index
		std::vector<Applicability> applicability;		  // per camera
	};

	// The checks that need no detection: a known profile, a well-formed dataset, a known reference,
	// models that apply, and the backends. On failure the report is returned failed.
	static std::optional<Report> prepare(Report& report, Dataset& data, const std::vector<RigCamera>& cameras,
										 const std::vector<GroupObservations>& groups,
										 const std::vector<std::optional<ImageGeometry>>& geometry, const Request& request,
										 core::Time start)
	{
		report.reproducibility.request = request;
		const std::optional<FitnessProfile> profile = fitnessProfile(request.fitnessProfile);
		if (!profile)
			return failed(report, start, Failure::UnknownFitnessProfile,
						  "no fitness profile is named \"" + request.fitnessProfile + "\"");
		report.thresholds = resolve(*profile, request.overrides);

		// Canonical camera order, so nothing downstream depends on how the cameras were listed.
		std::vector<std::size_t> cameraOrder(cameras.size());
		std::iota(cameraOrder.begin(), cameraOrder.end(), std::size_t{0});
		std::stable_sort(cameraOrder.begin(), cameraOrder.end(), [&](std::size_t a, std::size_t b)
						 { return cameras[a].camera < cameras[b].camera; });
		for (const std::size_t i : cameraOrder)
		{
			if (cameras[i].camera.empty())
				return failed(report, start, Failure::InvalidDataset, "a camera has no identity");
			if (!data.cameras.empty() && data.cameras.back().camera == cameras[i].camera)
				return failed(report, start, Failure::InvalidDataset,
							  "two cameras have the identity \"" + cameras[i].camera.value + "\"");
			data.cameras.push_back(cameras[i]);
		}
		if (data.cameras.size() < 2)
			return failed(report, start, Failure::TooFewCameras,
						  lain::string::format("{} cameras; a registration needs at least two", data.cameras.size()));
		const auto indexOf = [&](const capture::CameraIdentity& camera) -> std::optional<std::uint32_t>
		{
			const auto found = std::lower_bound(data.cameras.begin(), data.cameras.end(), camera,
												[](const RigCamera& c, const capture::CameraIdentity& id)
												{ return c.camera < id; });
			if (found == data.cameras.end() || found->camera != camera)
				return std::nullopt;
			return std::uint32_t(found - data.cameras.begin());
		};
		if (request.reference && !indexOf(*request.reference))
			return failed(report, start, Failure::UnknownReference,
						  "the requested reference \"" + request.reference->value + "\" is not a camera of the dataset");

		// Canonical group order, then each member's camera.
		for (const GroupObservations& g : groups)
			data.groups.push_back(&g);
		std::stable_sort(data.groups.begin(), data.groups.end(), [](const GroupObservations* a, const GroupObservations* b)
						 { return a->group.identity() < b->group.identity(); });
		for (std::size_t g = 0; g < data.groups.size(); ++g)
		{
			const GroupObservations& group = *data.groups[g];
			if (g > 0 && group.group.identity() == data.groups[g - 1]->group.identity())
				return failed(report, start, Failure::InvalidDataset,
							  "capture group " + group.group.identity().substr(0, 12) + " is given twice");
			if (group.detections.size() != group.group.members().size())
				return failed(report, start, Failure::InvalidDataset,
							  lain::string::format("capture group {} has {} members and {} detections",
												   group.group.identity().substr(0, 12), group.group.members().size(),
												   group.detections.size()));
			std::vector<std::uint32_t> members;
			for (const capture::CaptureMember& m : group.group.members())
			{
				const std::optional<std::uint32_t> camera = indexOf(m.camera);
				if (!camera)
					return failed(report, start, Failure::InvalidDataset,
								  "capture group " + group.group.identity().substr(0, 12) + " names camera \"" +
									  m.camera.value + "\", which the dataset does not have");
				members.push_back(*camera);
			}
			data.memberOf.push_back(std::move(members));
		}

		// Each model against the geometry its footage is measured in.
		for (std::size_t c = 0; c < data.cameras.size(); ++c)
		{
			const RigCamera& camera = data.cameras[c];
			const std::optional<ImageGeometry>& image = geometry[cameraOrder[c]];
			const Applicability applies = applicability(camera.model, image ? *image : camera.model.image());
			data.applicability.push_back(applies);
			if (applies == Applicability::Incompatible)
				return failed(report, start, Failure::IncompatibleModel,
							  lain::string::format("camera \"{}\"'s model is {}x{} and its footage is {}x{}", camera.camera.value,
												   camera.model.image().width, camera.model.image().height, image->width,
												   image->height));
			if (applies == Applicability::Unknown && request.unknownApplicability == ApplicabilityPolicy::Refuse)
				return failed(report, start, Failure::UnknownApplicability,
							  "camera \"" + camera.camera.value +
								  "\"'s model is of unknown applicability to its footage, and the request refuses that");
		}

		if (!cb::canSolvePose())
			return failed(report, start, Failure::NoPoseSolver,
						  "this build has no board pose solver (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!canRefine())
			return failed(report, start, Failure::NoRefiner,
						  "this build has no registration refiner (configure with -DLAIN_CAMERA_CERES=ON)");
		return std::nullopt;
	}

	// The geometry each camera's detections are measured in, or the reason they disagree.
	static std::variant<std::vector<std::optional<ImageGeometry>>, std::string>
	detectionGeometry(const std::vector<RigCamera>& cameras, const std::vector<GroupObservations>& groups)
	{
		std::vector<std::optional<ImageGeometry>> out(cameras.size());
		for (const GroupObservations& g : groups)
		{
			for (std::size_t m = 0; m < g.group.members().size() && m < g.detections.size(); ++m)
			{
				if (!g.detections[m].observation)
					continue;
				for (std::size_t c = 0; c < cameras.size(); ++c)
				{
					if (cameras[c].camera != g.group.members()[m].camera)
						continue;
					const ImageGeometry& image = g.detections[m].observation->image;
					if (out[c] && (out[c]->width != image.width || out[c]->height != image.height))
						return "camera \"" + cameras[c].camera.value + "\"'s detections are measured in two image sizes";
					out[c] = image;
				}
			}
		}
		return out;
	}

	// Everything after detection. `report` arrives prepared; `data` is the canonical dataset.
	static Report analyse(Report report, const Dataset& data, const cb::Specification& board, const Request& request,
						  core::Time start)
	{
		const std::size_t cameraCount = data.cameras.size();
		const std::size_t groupCount = data.groups.size();
		Diagnostics& diagnostics = report.diagnostics;
		Reproducibility& reproducibility = report.reproducibility;
		BoardRecord& method = std::get<BoardRecord>(reproducibility.method);
		diagnostics.groupsExamined = std::uint32_t(groupCount);

		// The record: every source, every detection, in canonical order.
		std::set<std::string> sources;
		reproducibility.frames = 0;
		for (std::size_t g = 0; g < groupCount; ++g)
		{
			const GroupObservations& group = *data.groups[g];
			GroupDetections record;
			record.group = group.group.identity();
			for (std::size_t m = 0; m < group.detections.size(); ++m)
			{
				sources.insert(group.group.members()[m].frame.source.toString());
				++reproducibility.frames;
				record.members.push_back({group.group.members()[m].camera, group.detections[m]});
				if (method.detector.backend.empty())
					method.detector = group.detections[m].provenance;
				if (!method.detection)
					method.detection = group.detections[m].request;
			}
			diagnostics.detections.push_back(std::move(record));
		}
		reproducibility.sources.assign(sources.begin(), sources.end());

		// 1. A pose for every usable detection, one per task.
		struct Slot
		{
			std::uint32_t group;
			std::uint32_t member;
		};
		std::vector<Slot> slots;
		for (std::uint32_t g = 0; g < groupCount; ++g)
		{
			for (std::uint32_t m = 0; m < data.groups[g]->detections.size(); ++m)
			{
				if (usable(data.groups[g]->detections[m]))
					slots.push_back({g, m});
			}
		}
		diagnostics.observations = std::uint32_t(slots.size());
		std::vector<std::optional<Posed>> posedSlots(slots.size());
		std::vector<Provenance> solverOf(slots.size());
		detail::forEach(slots.size(), request.execution,
						[&](std::size_t i)
						{
							const Slot slot = slots[i];
							const std::uint32_t camera = data.memberOf[slot.group][slot.member];
							const cb::Observation& view = *data.groups[slot.group]->detections[slot.member].observation;
							const CameraModel& model = data.cameras[camera].model;
							const cb::PoseResult result = cb::pose(model, board, view);
							solverOf[i] = result.provenance;
							if (!result.ok())
								return;
							Posed p;
							p.camera = camera;
							p.group = slot.group;
							p.view = &view;
							p.poses.push_back(result.pose->cameraFromBoard);
							if (result.alternative)
								p.poses.push_back(result.alternative->cameraFromBoard);
							for (const cb::FeatureObservation& f : view.features)
							{
								const std::optional<math::Vec3d> corner = board.cornerPosition(f.id);
								const Unprojection<double> ray = unproject(model, f.pixel.x, f.pixel.y);
								if (!corner || !ray.ok())
									continue;
								p.points.push_back(*corner);
								p.rays.push_back(math::normalize(math::Vec3d{ray.x, ray.y, ray.z}));
							}
							posedSlots[i] = std::move(p);
						});

		Evidence evidence;
		evidence.cameras = cameraCount;
		evidence.byGroup.assign(groupCount, {});
		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			if (method.poseSolver.backend.empty())
				method.poseSolver = solverOf[i];
			if (!posedSlots[i])
			{
				++diagnostics.observationsWithoutPose;
				continue;
			}
			evidence.byGroup[posedSlots[i]->group].push_back(std::uint32_t(evidence.posed.size()));
			evidence.posed.push_back(std::move(*posedSlots[i]));
		}
		// byGroup ascends by camera because a group's members do and slots were made in member order.

		// 2. Usable groups, and which of them are held out.
		std::vector<std::uint32_t> usableGroups;
		for (std::uint32_t g = 0; g < groupCount; ++g)
		{
			if (evidence.byGroup[g].size() >= 2)
				usableGroups.push_back(g);
		}
		diagnostics.groupsUsable = std::uint32_t(usableGroups.size());
		if (usableGroups.empty())
			return failed(std::move(report), start, Failure::TooFewCameras,
						  "no capture group has the board posed in two or more cameras");

		// Every k-th usable group held out, unless that would split the graph (detail::holdOut).
		std::vector<bool> heldOut(groupCount, false);
		const std::vector<bool> held = detail::holdOut(cameraCount, camerasOf(evidence, usableGroups),
													   request.heldOutFraction, kMinimumToHoldOut);
		for (std::size_t i = 0; i < usableGroups.size(); ++i)
		{
			if (!held[i])
				continue;
			heldOut[usableGroups[i]] = true;
			diagnostics.heldOutGroups.push_back(data.groups[usableGroups[i]]->group.identity());
		}
		std::vector<std::uint32_t> fitting, validating;
		for (const std::uint32_t g : usableGroups)
			(heldOut[g] ? validating : fitting).push_back(g);

		// 3. The camera observation graph over the fitting groups.
		const ObservationGraph graph = observationGraph(std::uint32_t(cameraCount), camerasOf(evidence, fitting));
		const auto edgeReport = [&](const GraphEdge& e)
		{ return GraphEdgeReport{data.cameras[e.a].camera, data.cameras[e.b].camera, e.sharedGroups, e.bridge}; };
		for (const GraphEdge& e : graph.edges)
			diagnostics.edges.push_back(edgeReport(e));
		for (const GraphEdge& e : weakBridges(graph, report.thresholds.ready.minimumBridgeGroups))
			diagnostics.weakBridges.push_back(edgeReport(e));
		for (const std::vector<std::uint32_t>& component : graph.components)
		{
			std::vector<capture::CameraIdentity> ids;
			for (const std::uint32_t c : component)
				ids.push_back(data.cameras[c].camera);
			diagnostics.components.push_back(std::move(ids));
		}
		for (std::size_t c = 0; c < cameraCount; ++c)
		{
			CameraEvidence camera;
			camera.camera = data.cameras[c].camera;
			camera.applicability = data.applicability[c];
			camera.groups = graph.groupsPerCamera[c];
			diagnostics.cameras.push_back(std::move(camera));
		}

		if (graph.components.size() > 1)
		{
			// Each component on its own, as a diagnostic: never a registration.
			for (const std::vector<std::uint32_t>& component : graph.components)
			{
				ComponentEstimate estimate;
				estimate.reference = data.cameras[component.front()].camera;
				const Initialisation init = initialise(evidence, graph, fitting, component.front());
				for (const std::uint32_t c : component)
				{
					if (init.cameraFromRoot[c])
						estimate.cameras.push_back({data.cameras[c].camera, init.cameraFromRoot[c]->inverse()});
				}
				diagnostics.componentEstimates.push_back(std::move(estimate));
			}
			std::string detail = lain::string::format("the cameras fall into {} groups that never saw the board together:",
													  graph.components.size());
			for (const std::vector<capture::CameraIdentity>& ids : diagnostics.components)
			{
				detail += " {";
				for (std::size_t i = 0; i < ids.size(); ++i)
					detail += (i ? ", " : "") + ids[i].value;
				detail += "}";
			}
			return failed(std::move(report), start, Failure::Disconnected, detail);
		}

		// 4. The reference: requested, or the camera in the most shared groups, ties to the lower identity.
		std::optional<std::uint32_t> requested;
		if (request.reference)
		{
			for (std::uint32_t c = 0; c < cameraCount; ++c)
			{
				if (data.cameras[c].camera == *request.reference)
					requested = c;
			}
			report.referenceRequested = true;
		}
		const std::uint32_t reference = detail::chooseReference(graph.groupsPerCamera, requested);
		report.reference = data.cameras[reference].camera;

		// 5. Initialisation.
		const Initialisation init = initialise(evidence, graph, fitting, reference);
		std::vector<math::RigidTransformd> initial;
		for (std::size_t c = 0; c < cameraCount; ++c)
		{
			if (!init.cameraFromRoot[c])
				return failed(std::move(report), start, Failure::InitialisationFailed,
							  init.failure.empty() ? "camera \"" + data.cameras[c].camera.value + "\" could not be placed"
												   : init.failure);
			initial.push_back(*init.cameraFromRoot[c]);
		}

		// 6. The global refinement.
		const Problem problem = problemOf(evidence, data.cameras, fitting, initial, reference, board, request);
		const Solution solution = refine(problem);
		reproducibility.refiner = solution.provenance;
		RefinementSummary summary;
		summary.status = solution.status;
		summary.detail = solution.detail;
		summary.iterations = solution.iterations;
		summary.initialCost = solution.initialCost;
		summary.finalCost = solution.finalCost;
		summary.corners = std::uint32_t(problem.observations.size());
		summary.bodies = std::uint32_t(problem.referenceFromBody.size());
		summary.elapsed = solution.elapsed;
		summary.noise = request.noise;
		summary.loss = request.loss;
		diagnostics.refinement = summary;
		if (solution.status == RefinementStatus::ResourceExhausted)
			return failed(std::move(report), start, Failure::ResourceExhausted, solution.detail);
		if (!solution.usable())
			return failed(std::move(report), start, Failure::RefinementFailed,
						  solution.detail.empty() ? "the refinement produced no usable solution" : solution.detail);
		if (solution.cameraFromReference.size() != cameraCount || solution.referenceFromBody.size() != fitting.size())
			return failed(std::move(report), start, Failure::RefinementFailed,
						  "the refiner returned a solution of the wrong shape");
		const std::vector<math::RigidTransformd>& cameraFromReference = solution.cameraFromReference;

		// Residuals, outliers, flips and depth, per fitted observation.
		std::vector<double> depths;
		math::Vec3d centroid{0.0};
		const std::uint32_t cornerCount = board.pattern().cornerCount();
		for (std::uint32_t id = 0; id < cornerCount; ++id)
			centroid += *board.cornerPosition(id) / double(cornerCount);
		std::vector<double> sumPixels(cameraCount, 0), sumAngles(cameraCount, 0);
		std::uint32_t outliers = 0, fitted = 0;
		for (std::size_t body = 0; body < fitting.size(); ++body)
		{
			const std::uint32_t g = fitting[body];
			for (const std::uint32_t o : evidence.byGroup[g])
			{
				const Posed& p = evidence.posed[o];
				const CameraModel& model = data.cameras[p.camera].model;
				const math::RigidTransformd cameraFromBoard = cameraFromReference[p.camera] * solution.referenceFromBody[body];
				depths.push_back(cameraFromBoard.apply(centroid).z);
				++fitted;

				double whitened = 0;
				std::uint32_t corners = 0;
				bool unprojectable = false;
				for (const cb::FeatureObservation& f : p.view->features)
				{
					const std::optional<math::Vec3d> corner = board.cornerPosition(f.id);
					if (!corner)
						continue;
					const math::Vec3d point = cameraFromBoard.apply(*corner);
					const Projection<double> predicted = project(model, point.x, point.y, point.z);
					if (!predicted.ok())
					{
						unprojectable = true;
						continue;
					}
					const math::Vec2d r = math::Vec2d{predicted.u, predicted.v} - f.pixel;
					if (f.covariance)
					{
						// Whitened by the Cholesky factor of the covariance: L^-1 r.
						const auto& c = *f.covariance;
						const double l11 = std::sqrt(c[0]);
						const double l21 = c[1] / l11;
						const double l22 = std::sqrt(std::max(c[2] - l21 * l21, 0.0));
						const double w1 = r.x / l11;
						const double w2 = (r.y - l21 * w1) / l22;
						whitened += w1 * w1 + w2 * w2;
					}
					else
						whitened += math::dot(r, r) / (request.noise.pixelSigma * request.noise.pixelSigma);
					sumPixels[p.camera] += math::dot(r, r);
					++corners;
				}
				for (std::size_t i = 0; i < p.points.size(); ++i)
				{
					const math::Vec3d q = cameraFromBoard.apply(p.points[i]);
					const double angle = std::atan2(math::length(math::cross(p.rays[i], q)), math::dot(p.rays[i], q));
					sumAngles[p.camera] += angle * angle;
				}
				CameraEvidence& camera = diagnostics.cameras[p.camera];
				camera.corners += corners;
				const double rmsWhitened =
					unprojectable || corners == 0 ? std::numeric_limits<double>::infinity() : std::sqrt(whitened / corners);
				if (rmsWhitened > request.loss.scale)
				{
					++outliers;
					++camera.outliers;
					diagnostics.outliers.push_back({data.groups[g]->group.identity(), camera.camera, rmsWhitened});
				}

				// A planar board's second pose, chosen: the result agrees with it rather than the first.
				if (p.poses.size() == 2 && rotationBetween(cameraFromBoard, p.poses[1]) < rotationBetween(cameraFromBoard, p.poses[0]))
					diagnostics.flips.push_back({data.groups[g]->group.identity(), camera.camera});
			}
		}
		for (std::size_t c = 0; c < cameraCount; ++c)
		{
			CameraEvidence& camera = diagnostics.cameras[c];
			if (camera.corners > 0)
			{
				camera.rmsPixels = std::sqrt(sumPixels[c] / camera.corners);
				camera.rmsAngle = std::sqrt(sumAngles[c] / camera.corners);
			}
		}
		diagnostics.medianDepth = median(depths);

		report.status = RegistrationStatus::Succeeded;
		for (std::size_t c = 0; c < cameraCount; ++c)
			report.cameras.push_back({data.cameras[c].camera, cameraFromReference[c].inverse()});
		report.cameras[reference].referenceFromCamera = math::RigidTransformd{};
		report.scale.scale = Scale::Metric;
		report.scale.evidence = lain::string::format("the board's measured square, {:.6g} mm ({})",
													 board.instance().squareLength.value.as<core::Length::Millimetres>(),
													 board.instance().identity);

		// 7. Held-out groups, member by member: the board placed from the other members with every
		// camera held at its registered transform, and the member's corners predicted from that.
		if (validating.empty())
			report.heldOut = Unavailable{"no capture groups were held out"};
		else
		{
			struct Prediction
			{
				std::uint32_t camera = 0;
				bool predicted = false;
				cb::ViewResidual residual;
			};
			std::vector<std::pair<std::uint32_t, std::uint32_t>> targets; // (group, posed index)
			for (const std::uint32_t g : validating)
			{
				for (const std::uint32_t o : evidence.byGroup[g])
					targets.push_back({g, o});
			}
			std::vector<Prediction> predictions(targets.size());
			detail::forEach(targets.size(), request.execution,
							[&](std::size_t i)
							{
								const auto [g, member] = targets[i];
								const Posed& target = evidence.posed[member];
								predictions[i].camera = target.camera;
								Problem single;
								for (const RigCamera& c : data.cameras)
									single.models.push_back(c.model);
								single.cameraFromReference = cameraFromReference;
								single.reference = reference;
								single.freeCameras = false;
								single.noise = request.noise;
								single.loss = request.loss;
								single.maximumIterations = request.maximumIterations;
								single.referenceFromBody.push_back(seedBody(evidence, g, cameraFromReference, member));
								for (const std::uint32_t o : evidence.byGroup[g])
								{
									if (o == member)
										continue;
									const Posed& p = evidence.posed[o];
									for (const cb::FeatureObservation& f : p.view->features)
									{
										const std::optional<math::Vec3d> corner = board.cornerPosition(f.id);
										if (corner)
											single.observations.push_back({p.camera, 0, *corner, f.pixel, f.covariance});
									}
								}
								const Solution placed = refine(single);
								if (!placed.usable() || placed.referenceFromBody.size() != 1)
									return;
								predictions[i].predicted = true;
								predictions[i].residual =
									cb::measure(data.cameras[target.camera].model, board, *target.view,
												cameraFromReference[target.camera] * placed.referenceFromBody[0]);
							});

			HeldOutEvidence held;
			held.groups = std::uint32_t(validating.size());
			std::vector<double> cameraAngles(cameraCount, 0);
			std::vector<std::uint32_t> cameraCorners(cameraCount, 0);
			double angles = 0, pixels = 0;
			for (const Prediction& p : predictions)
			{
				if (!p.predicted || p.residual.corners == 0)
				{
					++held.unpredicted;
					continue;
				}
				++held.predictions;
				held.corners += p.residual.corners;
				angles += p.residual.sumSquaredAngle;
				pixels += p.residual.sumSquaredPixels;
				held.worstPixels = std::max(held.worstPixels, p.residual.worstPixels);
				cameraAngles[p.camera] += p.residual.sumSquaredAngle;
				cameraCorners[p.camera] += p.residual.corners;
			}
			if (held.corners == 0)
				report.heldOut = Unavailable{"no held-out member could be predicted from the others"};
			else
			{
				held.rmsAngle = std::sqrt(angles / held.corners);
				held.rmsPixels = std::sqrt(pixels / held.corners);
				for (std::size_t c = 0; c < cameraCount; ++c)
				{
					if (cameraCorners[c] > 0)
						held.perCamera.push_back({data.cameras[c].camera, std::sqrt(cameraAngles[c] / cameraCorners[c])});
				}
				report.heldOut = held;
			}
		}

		// 8. Stability: registrations of bootstrap resamples of the fitting groups. The draws are made
		// before anything runs (detail::bootstrapDraws), so only the registrations run in parallel.
		if (request.resamples == 0)
			report.resampling = Unavailable{"resampling was not requested"};
		else if (fitting.size() < 2)
			report.resampling = Unavailable{"too few capture groups to resample"};
		else
		{
			const std::vector<std::vector<std::uint32_t>> draws = detail::bootstrapDraws(fitting, request.resamples, request.seed);
			std::vector<std::optional<std::vector<math::RigidTransformd>>> registrations(draws.size());
			detail::forEach(draws.size(), request.execution,
							[&](std::size_t r)
							{ registrations[r] = registerGroups(evidence, data.cameras, draws[r], reference, board, request); });

			std::vector<double> rotation(cameraCount, 0), translation(cameraCount, 0);
			std::uint32_t produced = 0;
			for (const auto& registration : registrations)
			{
				if (!registration)
					continue;
				++produced;
				for (std::size_t c = 0; c < cameraCount; ++c)
				{
					const math::RigidTransformd resampled = (*registration)[c].inverse();
					const math::RigidTransformd& result = report.cameras[c].referenceFromCamera;
					const double angle = rotationBetween(resampled, result);
					const math::Vec3d moved = resampled.translation() - result.translation();
					rotation[c] += angle * angle;
					translation[c] += math::dot(moved, moved);
				}
			}
			if (produced < 2)
				report.resampling = Unavailable{"fewer than two resamples produced a registration"};
			else
			{
				ResamplingEvidence evidenceOut;
				evidenceOut.resamples = produced;
				for (std::size_t c = 0; c < cameraCount; ++c)
				{
					const double r = std::sqrt(rotation[c] / produced), t = std::sqrt(translation[c] / produced);
					if (r > evidenceOut.rotationVariation)
					{
						evidenceOut.rotationVariation = r;
						evidenceOut.worstCamera = data.cameras[c].camera;
					}
					evidenceOut.translationVariationMetres = std::max(evidenceOut.translationVariationMetres, t);
				}
				evidenceOut.translationVariation = diagnostics.medianDepth > 0
													   ? evidenceOut.translationVariationMetres / diagnostics.medianDepth
													   : std::numeric_limits<double>::infinity();
				if (evidenceOut.worstCamera.empty())
					evidenceOut.worstCamera = data.cameras[reference].camera;
				report.resampling = evidenceOut;
			}
		}

		// 9. The verdict.
		detail::judge(report, graph, outliers, fitted);

		report.elapsed = core::Time::now() - start;
		return report;
	}

	// --- registerCameras ---------------------------------------------------------------

	Report registerCameras(const std::vector<RigCamera>& cameras, const std::vector<GroupObservations>& groups,
						   const cb::Specification& board, const Request& request)
	{
		const core::Time start = core::Time::now();
		Report report;
		report.reproducibility.request = request;
		auto geometry = detectionGeometry(cameras, groups);
		if (const std::string* problem = std::get_if<std::string>(&geometry))
			return failed(std::move(report), start, Failure::InvalidDataset, *problem);
		Dataset data;
		if (std::optional<Report> failure = prepare(report, data, cameras, groups,
													std::get<std::vector<std::optional<ImageGeometry>>>(geometry), request, start))
			return std::move(*failure);
		return analyse(std::move(report), data, board, request, start);
	}

	Report registerCameras(const std::vector<RigFootage>& cameras, const std::vector<capture::CaptureGroup>& groups,
						   const cb::Specification& board, const cb::DetectionRequest& detection, const Request& request)
	{
		const core::Time start = core::Time::now();
		Report report;
		report.reproducibility.request = request;
		std::get<BoardRecord>(report.reproducibility.method).detection = detection;

		std::vector<RigCamera> rig;
		std::vector<std::optional<ImageGeometry>> geometry;
		for (const RigFootage& c : cameras)
		{
			rig.push_back({c.camera, c.model});
			const math::Vec2i extent = c.footage.spec().extent;
			geometry.push_back(ImageGeometry{std::uint32_t(std::max(extent.x, 0)), std::uint32_t(std::max(extent.y, 0))});
		}

		// Every check that needs no frame comes first, so a request that cannot succeed decodes nothing.
		if (!cb::canDetect())
			return failed(std::move(report), start, Failure::NoDetector,
						  "this build has no board detector (configure with -DLAIN_CAMERA_OPENCV=ON)");
		// Each member's frame, found in its camera's footage through an index of that footage built
		// once. Placeholder detections stand in until the frames are decoded, so prepare() can check
		// the dataset's shape first.
		using FrameKey = std::pair<std::string, std::size_t>; // source uri, ordinal: a frame's identity
		std::map<capture::CameraIdentity, std::pair<const media::FrameSequence*, std::map<FrameKey, std::size_t>>> footageOf;
		for (const RigFootage& c : cameras)
		{
			auto& [footage, positions] = footageOf[c.camera];
			footage = &c.footage;
			for (std::size_t i = 0; i < c.footage.size(); ++i)
			{
				const media::FrameRef frame = c.footage.frame(i);
				positions.emplace(FrameKey{frame.source.toString(), frame.ordinal}, i);
			}
		}
		std::vector<GroupObservations> observed;
		std::vector<std::vector<std::pair<const media::FrameSequence*, std::size_t>>> frames;
		for (const capture::CaptureGroup& group : groups)
		{
			observed.push_back({group, std::vector<cb::DetectionReport>(group.members().size())});
			std::vector<std::pair<const media::FrameSequence*, std::size_t>> located;
			for (const capture::CaptureMember& m : group.members())
			{
				const auto camera = footageOf.find(m.camera);
				if (camera == footageOf.end())
				{
					located.push_back({nullptr, 0}); // prepare() names the unknown camera
					continue;
				}
				const auto position = camera->second.second.find(FrameKey{m.frame.source.toString(), m.frame.ordinal});
				if (position == camera->second.second.end())
					return failed(std::move(report), start, Failure::InvalidDataset,
								  "camera \"" + m.camera.value + "\"'s footage has no " + m.frame.toString());
				located.push_back({camera->second.first, position->second});
			}
			frames.push_back(std::move(located));
		}
		Dataset data;
		if (std::optional<Report> failure = prepare(report, data, rig, observed, geometry, request, start))
			return std::move(*failure);

		// Detect in every member's frame, one decoded frame per task.
		struct Slot
		{
			std::size_t group;
			std::size_t member;
		};
		std::vector<Slot> slots;
		for (std::size_t g = 0; g < groups.size(); ++g)
		{
			for (std::size_t m = 0; m < groups[g].members().size(); ++m)
				slots.push_back({g, m});
		}
		detail::forEach(slots.size(), request.execution,
						[&](std::size_t i)
						{
							const auto [g, m] = slots[i];
							const auto [footage, position] = frames[g][m];
							const image::Image frame = footage->image(position);
							observed[g].detections[m] =
								cb::detect(frame, groups[g].members()[m].frame, board, detection);
						});
		return analyse(std::move(report), data, board, request, start);
	}
} // namespace lain::camera::registration::board
