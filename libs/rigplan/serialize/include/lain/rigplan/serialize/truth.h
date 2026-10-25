#pragma once

#include <lain/data/value.h>
#include <lain/rigplan/truth.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// A synthetic capture's ground truth as documents (CONTEXT.md, "Ground truth"): truth/poses.json and
// truth/board.json. Its camera models and its rig are camera documents (lain::camera::serialize), so a
// method's result and its truth are read by one reader. A pose is spelled as the rig document spells a
// transform: {rotation: {w, x, y, z}, translation: {x, y, z}}, metres.
namespace lain::rigplan
{
	// The versions these documents are written at, and the newest this build reads.
	constexpr std::uint32_t kPosesDocumentVersion = 1;
	constexpr std::uint32_t kBoardTruthDocumentVersion = 1;

	// Every camera's plan-frame pose: {version, cameras: [{id, name, planFromCamera}]}.
	data::Value posesToValue(const std::vector<CameraPose>& poses);

	struct PosesRead
	{
		std::optional<std::vector<CameraPose>> poses;
		std::vector<std::string> problems; // empty exactly when `poses` is set
	};

	// Poses from a document, read strictly: a missing or misspelled key, an id that is not a uuid, or a
	// rotation that is not a unit quaternion is a problem named by its path.
	PosesRead posesFromValue(const data::Value& document);

	// Every board at every frame: {version, frames: [{frame, boards: [{id, name, planFromBoard, cameras:
	// [{id, name, inView: [{corner, u, v}]}]}]}]}, frames ascending, each frame's boards in the order
	// given.
	data::Value boardTruthToValue(const std::vector<BoardTruth>& truth);

	struct BoardTruthRead
	{
		std::optional<std::vector<BoardTruth>> truth; // frames ascending
		std::vector<std::string> problems;			  // empty exactly when `truth` is set
	};

	// Board truth from a document, read strictly as posesFromValue reads poses.
	BoardTruthRead boardTruthFromValue(const data::Value& document);
} // namespace lain::rigplan
