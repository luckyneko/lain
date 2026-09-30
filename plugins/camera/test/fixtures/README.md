# Real-camera fixtures

Each folder here is one real-camera fixture (ADR-0016): footage of one printed board from one camera,
in two capture **sessions**, the camera unplugged and re-mounted between them. `test-camera-fixture`
loads and runs every folder holding a `fixture.json`, in any build with a camera backend
(`LAIN_CAMERA_OPENCV=ON`). With no folder here, that case SKIPs and says no capture is committed.

Nothing here names a device. The first capture is expected from an Intel RealSense D455's colour
stream, and the steps below say where that matters, but a second camera is a second folder with its
own capture records, never a second format.

A fixture passes when:

- at least two sessions calibrate **Ready** on their own (held-out error, coverage, stability);
- every Ready session's model, held fixed, **predicts** every other session's views to the Ready
  held-out angle;
- every two Ready sessions **agree** on the intrinsics, within three standard deviations of their
  combined resampled spread.

Together those are the repeat check. A manufacturer's model is compared and reported, and never
passes or fails anything.

## 1. Print and measure the board

In a build with `LAIN_CAMERA_OPENCV=ON`:

```sh
flowview run -g apps/flowview/examples/render-board.json --board board.png --description board.txt
```

`board.png` is 9 x 6 squares, markers three quarters of a square, from ARUCO 5x5_100: 30 mm squares
at 300 dpi, sized to fit A4 landscape. `board.txt` is the machine-readable description of the
pattern.

- **Print at 100% scale** on matte paper, with no "fit to page". Printing on US Letter will need
  fit to page. The scale does not matter, because the next step measures it.
- **Mount it flat** on something rigid (foam board, glass, aluminium composite). A board that bows
  breaks the flat-board assumption every estimate makes.
- **Measure** with a steel rule or calipers across a whole row of 9 squares, from the first edge to
  the last, and divide by 9. Do the same down a column of 6. If the two disagree by more than about
  0.2%, the printer scaled one axis, so print again. Record the value to 0.1 mm.

## 2. Capture two sessions

Frames are PNG stills in one folder per session, named so they sort in order
(`frame.0000.png`, ...). Save them as **8-bit grey**: detection needs no colour, and one channel
is a third of the data of three.

For the D455, use the **colour stream at 1280 x 800**, at any frame rate. Keep the resolution the
record states: frames of another size than the stream fail to load, because a calibration of resized
frames is not a calibration of that stream.

For each pose:

- keep the board **0.35 to 0.7 m** from the camera, filling roughly a quarter to a half of the
  frame;
- **cover the whole image**, the corners especially. Ready needs the calibration views to reach at
  least 60% of an 8 x 6 grid over the image;
- **tilt it** up to about 40 degrees, left, right, up and down, and turn it in the image plane. A
  board always square to the camera cannot separate focal length from distance;
- **hold still**: use a tripod or rest the board, since motion blur moves every corner. Avoid glare
  on the print.

Capture **session A** with about 40 poses. Then **unplug the camera, take it off its mount, and put
it back**, and capture **session B** the same way. The remount is the point: the check is that
calibration finds the same camera twice.

## 3. Choose what is committed

Commit **about 14 frames per session**, covering the corners and the tilts. Each session has to be
Ready on its own: every fifth usable frame is held out to validate on, and Ready needs 10
calibration views, so 12 usable frames is the least that works. 14 leaves room for a frame the
detector turns down. Everything you captured goes to the extended tier (step 5).

## 4. Write fixture.json

```sh
camera-fixture-tool template plugins/camera/test/fixtures/<name> <name>
```

This writes a `fixture.json` for this board with sessions `a` and `b`. Put the committed frames in
`<name>/a/` and `<name>/b/`, then fill in:

- `board.instance.squareLengthMm`: the measured square;
- `board.instance.identity`: whatever tells this printed board apart from another print;
- each session's `capture`:
  - `device`: make, model and serial number;
  - `stream`: width, height, the device's name for the format, and `framesPerSecond`;
  - `properties`: SDK and firmware versions, for example.

  Leave anything unknown empty; nothing is guessed.
- `model`: the distortion model to calibrate with. `BrownConrady5` suits most lenses; a fisheye
  wants `KannalaBrandt4`.

The file is read strictly: a misspelled key or name is an error naming it, never a default.

**A manufacturer's model** goes in a session's capture record as `importedModel`:

```json
"importedModel": {
  "source": "RealSense factory calibration (rs-enumerate-devices -c, SDK 2.x)",
  "parameters": {
    "image": { "width": 1280, "height": 800 },
    "intrinsics": { "fx": 0.0, "fy": 0.0, "cx": 0.0, "cy": 0.0 },
    "distortion": { "type": "inverseBrownConrady5", "value": { "k1": 0.0, "k2": 0.0, "p1": 0.0, "p2": 0.0, "k3": 0.0 } }
  }
}
```

On a RealSense, `rs-enumerate-devices -c` prints each stream profile's intrinsics. Take the colour
1280 x 800 profile:

- `fx`, `fy` are `fx`, `fy`;
- `cx`, `cy` are `ppx`, `ppy`. Both lain and librealsense put pixel (0, 0) at the centre of the
  top-left pixel, so they carry over unchanged;
- the five coefficients go in the SDK's order, k1, k2, p1, p2, k3.

The distortion `type` comes from the model the SDK names:

| SDK | `type` |
|---|---|
| Inverse Brown Conrady | `inverseBrownConrady5` |
| Brown Conrady | `brownConrady5` |
| Modified Brown Conrady | `modifiedBrownConrady5` |
| Kannala Brandt4 | `kannalaBrandt4` (k1 to k4) |
| None | `none` (no `value`) |

Forward and inverse Brown-Conrady are not interchangeable, so copy the name exactly.

## 5. The extended tier

Keep the full capture outside the repository, as a fixture folder of its own: the same
`fixture.json`, with every frame. Then pin it with a manifest committed beside the compact fixture:

```sh
camera-fixture-tool manifest <full-capture-folder> <dataset-name> plugins/camera/test/fixtures/<name>/extended.json
```

Wherever the data is kept, it runs when `LAIN_CAMERA_FIXTURE_DATA` names the folder holding
`<dataset-name>/`. Check it with `camera-fixture-tool verify <extended.json>`.

There are three outcomes:

- **Verified**: the dataset runs like the compact tier.
- **Unavailable**: no data here, reported with the reason and not a failure.
- **Mismatch**: data is present that the manifest does not pin. This is a failure naming each file,
  and different data is never run in its place.

## 6. Run it

```sh
ctest --test-dir build -R "camera::fixture" --output-on-failure
./build/plugins/camera/test/test-camera-fixture "[real]" -s   # the full report, passing or not
```

The report prints each session's verdict and both halves of the repeat check. It also prints the
manufacturer comparison, which shows how far the factory model sits from lain's estimate.
