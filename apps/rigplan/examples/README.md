# rigplan examples

Rig plan documents to render a synthetic capture from, and for the test suite. A capture is footage
in the shape flowview's camera examples bind, with the ground truth it was rendered with beside it
(`rigplan render --help`).

```sh
rigplan render apps/rigplan/examples/three-poles.json --estimate
rigplan render apps/rigplan/examples/three-poles.json --out capture
```

The root flags (`--device gpu|cpu`, `--threads`, `-v`) go before `render`.

## The examples

| Document | Shows |
|---|---|
| `three-poles` | six cameras on three poles round a working point, and M9's board turning through it (needs a camera build, to draw the board) |
| `two-methods` | four fixed cameras round a textured room and a board moving through it, registered both ways (needs a camera build) |

## three-poles

Three poles stand on an arc of 1 m to the south of a working point at (0, 0, 1.25), at -40, 0 and +40
degrees, each turned to face it. Each carries a camera at 0.9 m looking up and one at 1.6 m looking
down, and a spot near the top. M9's board, 7 x 5 squares of 24 mm, turns through the working point
over 24 frames, so it is about 0.94 m from every camera.

The plan is stored at intrinsic scale 1 (installation locked to it) with the sensor's own noise, so
its capture's `nominal/` models are a realistic distance from its `truth/models/`. Registered with
flowview's `register.json` and scored with `compare-rigs.json`:

```sh
flowview run -g apps/flowview/examples/register.json --footage capture/rig --models capture/truth/models \
    --report report.txt --rig rig.json
flowview run -g apps/flowview/examples/compare-rigs.json --expected capture/truth/rig.json --found rig.json \
    --comparison comparison.txt
```

With `--models capture/nominal` instead, the rig is registered with the models the cameras were
meant to have, and the comparison shows what not calibrating them costs.

`--deviation 0` renders the plan as designed, where the nominal models are the truth.

`cmake --build build --target synthetic-capture` renders it into `build/synthetic-capture`, with a
README.txt of these commands spelled out for that build. ctest's `test-flowview-capture` runs the
same render, registrations and comparisons on every device the machine has, and CI runs them through
the shipped binaries on the CPU device.

## two-methods

One rig for registering with a board and without one from the same pixels: M9's comparison scene
(`plugins/camera/test/test_comparison.cpp`) as a rig plan. Four cameras stand 0.6 m apart on an arc
3.2 m from a textured 6 x 4.5 x 2.4 m room with a box on its floor, at 1280 x 720 behind a 2 mm lens
(f = 1000 px). A 7 x 5 board of 80 mm squares moves through their shared view 1.6 m away, turning as it
goes, over 16 frames. Targetless registration places the cameras by the room and drops the board as
moving content; board registration places them by the board.

Its board is not M9's 24 mm board, so flowview's `register.json` does not detect it; the targetless
half runs through `register-targetless.json`:

```sh
flowview run -g apps/flowview/examples/register-targetless.json --footage capture/rig \
    --models capture/truth/models --report report.txt --rig rig.json
flowview run -g apps/flowview/examples/compare-rigs.json --expected capture/truth/rig.json --found rig.json \
    --comparison comparison.txt
```

A targetless rig is at an arbitrary scale, which the comparison fits before it measures the centres.

## Regenerating

The documents are written by `rigplan-examples` (`src/`). ctest checks that every committed document
is byte for byte what the builder writes, so after changing a builder, regenerate and commit:

```sh
cmake --build build --target rig-plans
```

**The rig plan document is frozen at version 1.** These are the first rig plans written by an earlier
build than the one reading them, so a change to the document's shape is a new version with a migrator
from version 1, never a regeneration that quietly rewrites them.
