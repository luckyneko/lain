# A rig plan is nominal; a synthetic capture renders one seeded realisation of it

---
Status: accepted (designed 2026-10-21 to 10-22, Milestone 15; nothing built yet)
---

Milestone 15 renders synthetic captures from rig plans, so that calibration and registration can
be run on them and scored against their ground truth. The repo owner asked for "a range or some
amount of error" on intrinsics, for testing and realism, which is deterministic and can be scaled
from off to the full range. This ADR fixes what that error is, how it is drawn, and what a capture
can be reproduced from, since every test written against a capture depends on those answers.

## Decision

**A rig plan states nominal values and tolerances. A realisation draws every tolerance once, from
the rig plan's seed, and a synthetic capture renders that realisation. Its ground truth is the
realisation, never the plan. A capture is reproducible from the rig plan and the seed alone.**

- **A tolerance is a symmetric bound in its part's own unit**, as a datasheet states it: a
  percentage of focal length on the lens, micrometres of decentring on the sensor, millimetres and
  degrees on a mount and a placement. Specifications are therefore split into a sensor and a lens,
  and a camera's nominal model is derived from the pair rather than typed in. A tolerance in pixels
  would stop meaning anything once the lens went on another sensor.
- **A draw is uniform within its bound.** The tool exists to stress the methods, so the edges of a
  band should be visited as often as its middle, and a uniform draw has no hidden parameter. A
  normal draw with σ at a third of the bound, the realistic reading, was refused for visiting the
  edges rarely.
- **A lens's distortion tolerance is one percentage on its strength**, scaling every coefficient
  together. Independent per-coefficient bounds were refused: the coefficients are correlated, their
  effects differ by orders of magnitude, and independent draws often fold inside the image, so at
  full scale most realisations would be refused.
- **A draw is seeded by the rig plan's seed, the stable UUID of what it belongs to, and the
  parameter's document key**, never by list order and never by a display name. Adding, removing,
  reordering or renaming one camera then re-rolls nothing else. A duplicate mints a new UUID and
  gets its own draw, because duplicating a camera means installing a second unit.
- **The draw is a unit deviation, and deviation scales multiply it.** There is one scale per
  category, intrinsic and installation, locked together by default. 0 is the nominal plan exactly,
  and 1 is the full tolerance. Since the seed fixes each deviation's direction, sweeping a scale
  degrades the same build steadily. They are separate because intrinsic mismatch is what stresses
  registration given datasheet models, while installation error mostly changes coverage.
- **A draw that does not make a valid camera model is refused, naming the camera.** It is never
  drawn again: a silent redraw is a hidden bias toward valid corners of the band.
- **Noise is not a tolerance.** A deviation is fixed for the life of a unit, while noise is new
  every frame. The **sensor model** (a fixed gain, read and shot noise, quantisation, encoding) runs
  in lain's C++ code after the GPU's radiance is read back. Its noise is drawn from the seed, camera,
  frame and pixel, and scaled by its own **noise scale**. So the recorded bytes are identical on every
  machine, whatever GPU rendered the light, and the sensor model is testable in CI without one.
- **Gain is fixed per sensor and never metered.** Auto-exposure, auto-white-balance and every other
  automatic adjustment are **refused, not deferred**: a detector's evidence must not change with
  what happens to be in view.
- **A rig plan is self-contained.** It copies in every specification it uses from a catalogue, and
  never links one. A linked specification would let a catalogue edit re-render an old rig plan
  differently behind its back, which breaks "rig plan plus seed reproduces the capture", and it
  would need M7's cache, reload and interface-repair machinery a second time.
- **Every capture writes both model sets**: the nominal models (the capture record's imported
  manufacturer model, as a datasheet would state it) and the realised ones in its ground truth.
  Registering with one or the other is a choice made at binding time, not at generation, so one
  capture serves both "uncalibrated rig" and "perfect calibration" experiments.

## Consequences

- Ground truth is computed in C++ through lain's projection, from the realisation, so it is exact
  and identical on every machine, even where the pixels are not.
- Scoring a result against ground truth is camera geometry, not rig design: rig comparison and model
  comparison live in `lain::camera`, and score a survey or a manufacturer's model alike.
- A per-coefficient distortion tolerance, a normal draw and linked specifications each remain
  available later as additions. None of them needs a change to what is already drawn.
