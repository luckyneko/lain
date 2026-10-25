#include "lain/rigplan/serialize/truth.h"

#include <lain/data/data.h>
#include <lain/data/strict.h>
#include <lain/data/versioned.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace lain::rigplan
{
	// The documents' parts with nothing checked, as the rig document's are.
	struct RotationDoc
	{
		double w = 1;
		double x = 0;
		double y = 0;
		double z = 0;
	};
	LAIN_SERIALIZE(RotationDoc, w, x, y, z)

	struct TranslationDoc
	{
		double x = 0;
		double y = 0;
		double z = 0;
	};
	LAIN_SERIALIZE(TranslationDoc, x, y, z)

	struct TransformDoc
	{
		RotationDoc rotation;
		TranslationDoc translation;
	};
	LAIN_SERIALIZE(TransformDoc, rotation, translation)

	struct PoseDoc
	{
		std::string id;
		std::string name;
		TransformDoc planFromCamera;
	};
	LAIN_SERIALIZE(PoseDoc, id, name, planFromCamera)

	struct PosesDoc
	{
		std::vector<PoseDoc> cameras;
	};
	LAIN_SERIALIZE(PosesDoc, cameras)

	struct CornerDoc
	{
		std::uint32_t corner = 0;
		double u = 0;
		double v = 0;
	};
	LAIN_SERIALIZE(CornerDoc, corner, u, v)

	struct CameraCornersDoc
	{
		std::string id;
		std::string name;
		std::vector<CornerDoc> inView;
	};
	LAIN_SERIALIZE(CameraCornersDoc, id, name, inView)

	struct BoardAtFrameDoc
	{
		std::string id;
		std::string name;
		TransformDoc planFromBoard;
		std::vector<CameraCornersDoc> cameras;
	};
	LAIN_SERIALIZE(BoardAtFrameDoc, id, name, planFromBoard, cameras)

	struct FrameDoc
	{
		std::uint64_t frame = 0;
		std::vector<BoardAtFrameDoc> boards;
	};
	LAIN_SERIALIZE(FrameDoc, frame, boards)

	struct BoardTruthDoc
	{
		std::vector<FrameDoc> frames;
	};
	LAIN_SERIALIZE(BoardTruthDoc, frames)

	// How far a rotation's norm may be from 1 before a reader refuses it: the rig document's tolerance.
	static constexpr double kTolerance = 1e-6;

	static TransformDoc docOf(const math::RigidTransformd& transform)
	{
		const math::Quatd& r = transform.rotation();
		const math::Vec3d& t = transform.translation();
		return {{r.w, r.x, r.y, r.z}, {t.x, t.y, t.z}};
	}

	// What reading needs beyond the shape: identities that are uuids and rotations that are unit, each
	// problem named by its path.
	struct Reading
	{
		std::vector<std::string> problems;

		core::Uuid identity(const std::string& text, const std::string& path)
		{
			const std::optional<core::Uuid> id = core::Uuid::parse(text);
			if (!id)
				problems.push_back(string::format("'{}' is '{}', which is not a uuid", path, text));
			return id.value_or(core::Uuid{});
		}

		math::RigidTransformd transform(const TransformDoc& doc, const std::string& path)
		{
			const RotationDoc& r = doc.rotation;
			const double norm = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
			if (!std::isfinite(norm) || std::abs(norm - 1) > kTolerance)
			{
				problems.push_back(string::format("'{}.rotation' is not a unit quaternion: its norm is {}", path, norm));
				return {};
			}
			return math::RigidTransformd{math::Quatd{r.w, r.x, r.y, r.z}, math::Vec3d{doc.translation.x, doc.translation.y, doc.translation.z}};
		}
	};

	// The parts of a `kind` document, its shape read strictly: the version first, then the parts, then
	// what writing the parts back gives compared with the document.
	template <typename Parts>
	static std::optional<Parts> partsOf(const data::Value& document, const char* kind, std::uint32_t newest,
										std::vector<std::string>& problems)
	{
		problems = data::versionProblems(document, kind, newest);
		if (!problems.empty())
			return std::nullopt;
		std::optional<Parts> parts = data::fromValue<Parts>(document);
		if (!parts)
		{
			problems.push_back("the document does not have the expected shape");
			return std::nullopt;
		}
		problems = data::shapeDifferences(document, data::versionedDocument(newest, data::toValue(*parts)));
		if (!problems.empty())
			return std::nullopt;
		return parts;
	}

	data::Value posesToValue(const std::vector<CameraPose>& poses)
	{
		PosesDoc doc;
		for (const CameraPose& pose : poses)
			doc.cameras.push_back({pose.id.toString(), pose.name, docOf(pose.planFromCamera)});
		return data::versionedDocument(kPosesDocumentVersion, data::toValue(doc));
	}

	PosesRead posesFromValue(const data::Value& document)
	{
		PosesRead result;
		const std::optional<PosesDoc> parts = partsOf<PosesDoc>(document, "poses", kPosesDocumentVersion, result.problems);
		if (!parts)
			return result;
		Reading read;
		std::vector<CameraPose> poses;
		for (std::size_t i = 0; i < parts->cameras.size(); ++i)
		{
			const PoseDoc& pose = parts->cameras[i];
			const std::string at = string::format("cameras[{}]", i);
			poses.push_back({read.identity(pose.id, at + ".id"), pose.name, read.transform(pose.planFromCamera, at + ".planFromCamera")});
		}
		result.problems = std::move(read.problems);
		if (result.problems.empty())
			result.poses = std::move(poses);
		return result;
	}

	data::Value boardTruthToValue(const std::vector<BoardTruth>& truth)
	{
		std::map<std::size_t, FrameDoc> frames;
		for (const BoardTruth& board : truth)
		{
			FrameDoc& frame = frames[board.frame];
			frame.frame = board.frame;
			BoardAtFrameDoc doc{board.board.toString(), board.name, docOf(board.planFromBoard), {}};
			for (const CameraCorners& camera : board.cameras)
			{
				CameraCornersDoc corners{camera.camera.toString(), camera.name, {}};
				for (const CornerTruth& corner : camera.inView)
					corners.inView.push_back({corner.corner, corner.pixel.x, corner.pixel.y});
				doc.cameras.push_back(std::move(corners));
			}
			frame.boards.push_back(std::move(doc));
		}
		BoardTruthDoc doc;
		for (auto& [index, frame] : frames)
			doc.frames.push_back(std::move(frame));
		return data::versionedDocument(kBoardTruthDocumentVersion, data::toValue(doc));
	}

	BoardTruthRead boardTruthFromValue(const data::Value& document)
	{
		BoardTruthRead result;
		const std::optional<BoardTruthDoc> parts =
			partsOf<BoardTruthDoc>(document, "board truth", kBoardTruthDocumentVersion, result.problems);
		if (!parts)
			return result;
		Reading read;
		std::vector<BoardTruth> truth;
		for (std::size_t f = 0; f < parts->frames.size(); ++f)
		{
			const FrameDoc& frame = parts->frames[f];
			for (std::size_t b = 0; b < frame.boards.size(); ++b)
			{
				const BoardAtFrameDoc& board = frame.boards[b];
				const std::string at = string::format("frames[{}].boards[{}]", f, b);
				BoardTruth out;
				out.board = read.identity(board.id, at + ".id");
				out.name = board.name;
				out.frame = std::size_t(frame.frame);
				out.planFromBoard = read.transform(board.planFromBoard, at + ".planFromBoard");
				for (std::size_t c = 0; c < board.cameras.size(); ++c)
				{
					const CameraCornersDoc& camera = board.cameras[c];
					CameraCorners corners;
					corners.camera = read.identity(camera.id, string::format("{}.cameras[{}].id", at, c));
					corners.name = camera.name;
					for (const CornerDoc& corner : camera.inView)
						corners.inView.push_back({corner.corner, {corner.u, corner.v}});
					out.cameras.push_back(std::move(corners));
				}
				truth.push_back(std::move(out));
			}
		}
		result.problems = std::move(read.problems);
		if (result.problems.empty())
			result.truth = std::move(truth);
		return result;
	}
} // namespace lain::rigplan
