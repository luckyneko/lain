# flowview examples

Graph documents to open in flowview (**File ▸ Open**), to run headless, and for the test suite.
Every example opens and shows a result on its own, except `sequence-render`, which is driven by a
frame position. Media paths are stored relative to this folder, so it can be opened from any clone
or copied elsewhere whole.

```sh
flowview                                   # then File ▸ Open an example
flowview run -g apps/flowview/examples/folder-average.json
flowview run -g apps/flowview/examples/sequence-render.json --frame 0-23 --result 'out.<frame:04>.png'
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
