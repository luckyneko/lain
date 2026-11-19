#pragma once

#include "selection.h"

#include <lain/gui/gizmo.h>
#include <lain/math/types.h>
#include <lain/render/view.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>

#include <array>
#include <cstddef>
#include <optional>

namespace rigplan
{
	// The bridge between a selection and ImGuizmo: where the gizmo stands for it, what it may move, the
	// manipulated matrix written back into the plan, and the matrices ImGuizmo draws with. The gizmo never
	// offers a component the plan cannot hold:
	//
	//   kind              translate                          rotate
	//   pole              X and Y, on the floor              yaw
	//   fixed point       X, Y and Z                         yaw
	//   camera, light     its offset, in its mount's frame   its aim
	//   plane             its centre                         its aim
	//   box, static prop  its centre / position              yaw
	//   key               its position                       its aim
	//   board, prop mover its key at the frame               its key's aim
	//   room              its centre, X and Y                none
	//
	// A mover is moved by its keys, so selected itself its gizmo is the key at the frame shown: that key's,
	// where the path has one there, and otherwise one standing where the path puts it, which the first
	// change of a drag makes (setKey). So dragging a mover keys it at the frame shown: making the key moves
	// no frame of its path, and moving it reshapes the path only between the keys either side. Anything
	// else offers no gizmo.

	// Whether the gizmo is moving or turning what is selected (W and E).
	enum class GizmoMode
	{
		Translate,
		Rotate,
	};

	// How far Ctrl snaps a drag: a centimetre, and five degrees.
	inline constexpr float kTranslateSnapMetres = 0.01f;
	inline constexpr float kRotateSnapDegrees = 5.0f;

	// Which of the three axes a gizmo offers, X, Y and Z. A target whose kind restricts either keeps its
	// own Z the plan's +Z, so its handles mean the same whether ImGuizmo draws them in the plan's axes or
	// in the target's (WORLD or LOCAL).
	using GizmoAxes = std::array<bool, 3>;

	struct GizmoTarget
	{
		// Where the handles stand and how they are turned: a camera or light at its mounting point (what its
		// offset places), in camera-frame axes, as its aim turns it; a plane or key as its aim turns it; a
		// mount, box, prop or room in the plan's axes turned by its yaw.
		lain::math::Mat4d planFromTarget{1.0};
		GizmoAxes translate{false, false, false};
		GizmoAxes rotate{false, false, false};
	};

	// The gizmo for `selection` in `plan`, laid out as `layout` has it, with a mover at `frame` (the frame
	// shown), or none: nothing to move, or no pose to stand at (a camera on a mount with no position, or a
	// path that cannot be followed, say).
	std::optional<GizmoTarget> gizmoTarget(const lain::rigplan::RigPlan& plan, const lain::rigplan::NominalLayout& layout,
										   const Selection& selection, std::size_t frame);

	// ImGuizmo's handles for `target` in `mode`, or none (zero) when the mode moves nothing of it.
	lain::gui::gizmo::OPERATION operation(const GizmoTarget& target, GizmoMode mode);

	// Writes `manipulated`, the gizmo's matrix after a drag, back into what `selection` names: in Translate
	// mode its position alone and in Rotate mode its turn alone, each held to the table above, so a
	// component the plan cannot hold is ignored (a pole's height, a box's tilt) and one the mode does not
	// move is left exactly as it was. A matrix with scale in it (ImGuizmo works in float) is read for its
	// rotation and translation. A mover with no key at `frame` is keyed there first, so the change lands on
	// that key and what is selected stays the mover. Returns whether the plan changed.
	bool apply(lain::rigplan::RigPlan& plan, const lain::rigplan::NominalLayout& layout, const Selection& selection, std::size_t frame,
			   GizmoMode mode, const lain::math::Mat4d& manipulated);

	// The view and projection ImGuizmo expects for `view`'s pinhole: column-major float[16], OpenGL's
	// camera axes (x right, y up, looking down -z) and clip space (y up, depth -1 at the near plane and 1
	// at the far one). So a point lain's projection puts at pixel (u, v) of `view` is where ImGuizmo puts
	// it on a pane showing the view: (u + 0.5, v + 0.5) of the image's width and height, from its
	// top-left, however many points the pane is.
	struct GizmoMatrices
	{
		std::array<float, 16> view;
		std::array<float, 16> projection;
	};
	GizmoMatrices gizmoMatrices(const lain::render::View& view);
} // namespace rigplan
