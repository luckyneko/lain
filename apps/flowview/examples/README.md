# flowview examples

Graph documents to open in flowview (**File ▸ Open**), to run headless, and for the test suite.
Every example opens and shows a result on its own, except `sequence-render`, which is driven by a
frame position, and the camera examples, which calibrate, register or compare what you bind. Media paths are stored relative to this folder, so it can be opened from any clone
or copied elsewhere whole.

```sh
flowview                                   # then File ▸ Open an example
flowview run -g apps/flowview/examples/folder-average.json
flowview run -g apps/flowview/examples/sequence-render.json --frame 0-23 --result 'out.<frame:04>.png'
flowview run -g apps/flowview/examples/calibrate-fallback.json --footage frames/ --method board-then-targetless \
    --attempts attempts.txt --model model.json
```

The root flags (`--size`, `--threads`, `-v`) go before `run`.

## The examples

| Document | Shows |
|---|---|
| `tint-blur` | gradient → tint → blur, with sigma driven by a Float constant |
| `payload-types` | retyped Constants, Casts into a blur's settings, a Compare over Int opening a Gate |
| `control-flow` | a closed Gate suppressing a branch, Merge taking the first live one, Select choosing |
| `colour-convert` | gradient → convert to BT709 RGB8 |
| `inline-group` | an inline group holding another: a two-deep breadcrumb |
| `linked-group` | two linked instances of `template-soften`, sharing one definition |
| `template-soften` | the template both link: tint, then blur (its input is unbound when opened alone) |
| `count-loop` | a loop folding an image through a blur five times |
| `while-loop` | blur until the picture stops changing; `iterations` says when |
| `loop-index` | the loop's index drives its condition and its body (`index < 4` runs five passes) |
| `load-image` | `loadimage data/stills/a.png` → tint |
| `folder-average` | listDir → map (files split, sigma broadcast) → combine; step through the elements |
| `sequence-clip` | a 12-frame clip from position 6; its first frame shows the shot's 7th |
| `sequence-render` | one frame per position. In the gui, drag **Position** in the Interface pane |
| `video` | `data/shot.mp4` as a sequence, and its frame at position 12 (needs a video build) |
| `bagel` | an inline group holding a map, whose elements run a linked template and a loop |
| `render-board` | a ChArUco board to print for a real-camera fixture: 9 x 6 squares of 30 mm (needs a camera build); see `plugins/camera/test/fixtures/README.md` |
| `calibrate` | board calibration of one camera's footage: its report and its model (needs a camera build). Bind `--footage` |
| `calibrate-fallback` | board calibration, then targetless calibration when the board's attempt is not accepted (needs a camera build). Bind `--footage` and `--method board-then-targetless`; with no method nothing is attempted |
| `register` | a rig registered by a board: its report and its rig document (needs a camera build). Bind `--footage` (a folder of per-camera folders or videos) and `--models` (a folder of camera-model documents named alike) |
| `register-targetless` | a rig registered from the scene its cameras share, no board (needs a camera build). Bind `--footage` and `--models` |
| `calibrate-and-register` | each camera of a rig calibrated through a map, then the rig registered with those models (needs a camera build). Bind `--footage` |
| `compare-rigs` | a registered rig scored against an expected one, such as a synthetic capture's `truth/rig.json`: per camera, its turn and its centre's distance after a scale-only fit, written as one line. Bind `--expected` and `--found`, two rig documents; the expected rig may be relative to another camera |
| `compare-models` | a camera model scored against an expected one, such as a synthetic capture's `truth/models/<camera>.json`: where the found model sends the expected one's rays, written as one line. Bind `--expected` and `--found`, two camera-model documents |

The camera examples expect a pinhole and a 7 x 5 board of 24 mm squares, which is what their footage
shows. For real footage, change the board specification and each calibration's `model`. Their footage
is not committed: draw it into the build folder with

```sh
cmake --build build --target camera-footage
```

and `build/camera-footage/README.txt` lists each folder and the command that runs its example.

## Broken on purpose

Each one raises one issue, in the Issues pane or on load. They are what the pane is checked against.

| Document | Raises |
|---|---|
| `broken-missing-template` | its link names a template that is not there: it loads as a placeholder |
| `broken-map-hole` | one file in `data/holes` is not a PNG: the map's output is cleared |
| `broken-cast-pair` | a Cast from Image to Int, which nothing converts |
| `broken-loop-pin` | a loop interior pin named `count`, which collides with the loop's own port |
| `broken-unknown-kind` | a node of a kind this build does not know: dropped with its edges |
| `broken-v1` | a version-1 document, migrated on load. Hand-written and frozen |

## Data

`data/` is synthetic and generated here, so it needs no licence:
- `stills/`: four 160x90 images.
- `shot/`: 24 frames, each with a moving block and its frame number burned in.
- `shot.mp4`: the same shot as video.
- `holes/`: three stills and one file that is not a PNG.

## Regenerating

The documents (all but `broken-v1`) are written by `flowview-examples` (`src/`). ctest checks that
the committed files are exactly what it writes, so after changing a builder:

```sh
cmake --build build --target examples        # the documents
cmake --build build --target examples-data   # the documents and data/
```

Commit what they wrote. `shot.mp4` is re-encoded only in a build with a video writer, and the
encoder differs by platform, so only regenerate it on purpose.
