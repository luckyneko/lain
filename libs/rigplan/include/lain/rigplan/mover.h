#pragma once

#include "lain/rigplan/aim.h"

#include <lain/camera/board/pattern.h>
#include <lain/camera/board/specification.h>
#include <lain/core/uuid.h>
#include <lain/math/rigidtransform.h>
#include <lain/math/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lain::rigplan
{
	// What moves in a rig plan (CONTEXT.md, "Mover"): a board or a prop, following a path. Cameras and
	// mounts never move, and nothing here has a tolerance: a mover's path is its truth.

	// Where a mover is at one frame of the capture timeline: a position in the plan frame, metres, and
	// an aim. A mover is aimed as a camera is (Aim, with the plan as its mount), so at rest it faces a
	// camera at rest, and a camera with the same aim sees it face on and upright, as a Plane is turned.
	struct PathKey
	{
		std::size_t frame = 0; // a frame of the capture timeline, never a time, so a new rate moves no key
		math::Vec3d position{0.0, 0.0, 0.0};
		Aim aim;
	};

	// Poses keyed at frames, in frame order.
	struct Path
	{
		std::vector<PathKey> keys;
	};

	// Why `path` cannot be followed: no keys, frames that do not strictly increase, or a position or aim
	// that is not finite. One line per fault; empty when it can be.
	std::vector<std::string> pathProblems(const Path& path);

	// The pose `path` holds at `frame`, in camera-frame axes as mountFromPlacement gives them with the
	// plan as the mount: at a key, that key's; between two, linear in position and by slerp in rotation
	// (the shorter way), in proportion to the frames between them; before the first key the first's and
	// after the last the last's. `path` must have a key.
	math::RigidTransformd poseAt(const Path& path, std::size_t frame);

	// A board waved through the rig (CONTEXT.md, "Mover"): a board specification drawn by the board
	// renderer onto a printed sheet, so the board in a capture is the pattern a detector expects.
	struct BoardMover
	{
		core::Uuid id;
		std::string name;
		camera::board::PatternParameters pattern;
		camera::board::Instance instance;	 // its identity, and the printed square's side, which is exact here
		double margin = 0.01;				 // metres of white around the squares, drawn to the nearest pixel
		std::uint32_t pixelsPerSquare = 100; // how finely the board is drawn onto its sheet
		// Metres between the print and the sheet's plain back. The renderer lights both sides of a
		// surface, so without a back a camera behind the board would see its pattern mirrored, and a
		// mirrored marker can read as another marker.
		double backing = 0.003;
		// Where the centre of its squares is, aimed as a camera that sees it face on and upright.
		Path path;
	};

	// Why `board` cannot be shown: its pattern or instance make no board specification, a margin that
	// is negative or not finite, no pixels a square, a backing that is not positive and finite, or a path
	// that cannot be followed (pathProblems). One line per fault; empty when it can be.
	std::vector<std::string> boardProblems(const BoardMover& board);

	// The board's specification, or none when its pattern and instance make none (boardProblems says
	// why).
	std::optional<camera::board::Specification> specificationOf(const BoardMover& board);

	// The board frame (camera::board::Specification: its squares' top-left corner, X right and Y down
	// along its face, Z into it) into the plan frame at `frame`: the path's pose, less half the squares'
	// extent along X and Y. `board` must have a usable pattern and a key.
	math::RigidTransformd planFromBoard(const BoardMover& board, std::size_t frame);
} // namespace lain::rigplan
