# lain
A set of C++ libraries used to rapidly prototype ideas.

## Third-party licenses

Third-party dependencies default to permissive licenses. A non-permissive dependency requires an
explicit, scoped exception under
[ADR-0015](docs/adr/0015-permissive-by-default-production-dependencies.md). Eigen is currently the
only approved exception. Keeping the inventory here makes dependency cost and replacement candidates
visible without exposing third-party types through lain-owned interfaces.

Versions marked **current** are pinned by the repository's CMake modules unless a compatible system
package satisfies the target. Versions marked **planned** are approved for the camera work but are
not part of the build yet. This inventory does not replace the license and notice files shipped by
each upstream source or binary package; those remain authoritative for redistribution.

| Dependency | Version/status | License | Scope |
| --- | --- | --- | --- |
| [Taskflow](https://github.com/taskflow/taskflow) | 3.7.0, current | [MIT] | `lain::task` executor |
| [GLM](https://github.com/g-truc/glm) | 1.0.3, current | [MIT] | `lain::math` implementation |
| [{fmt}](https://github.com/fmtlib/fmt) | 10.2.1, current | [MIT] | Formatting and logging |
| [spdlog](https://github.com/gabime/spdlog) | 1.14.1, current | [MIT] | `lain::log` implementation |
| [magic_enum](https://github.com/Neargye/magic_enum) | 0.9.8, current | [MIT] | Enum reflection |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3, current | [MIT] | JSON codec plugin |
| [stb](https://github.com/nothings/stb) | `31c1ad3`, current | [MIT] OR public domain | JPEG codec plugin |
| [libpng](https://github.com/pnggroup/libpng) | 1.6.44, current | [libpng-2.0] | PNG codec plugin |
| [zlib](https://github.com/madler/zlib) | 1.3.1, current | [Zlib] | PNG/TIFF codec dependency |
| [libtiff](https://gitlab.com/libtiff/libtiff) | 4.7.0, current | [libtiff] | TIFF codec plugin |
| [GLFW](https://github.com/glfw/glfw) | 3.4, current | [Zlib] | App windowing and input |
| [CLI11](https://github.com/CLIUtils/CLI11) | 2.4.2, current | [BSD-3-Clause] | App command-line parsing |
| [Dear ImGui](https://github.com/ocornut/imgui) | `162ce49`, current | [MIT] | GUI implementation |
| [imnodes](https://github.com/Nelarius/imnodes) | `eb36902`, current | [MIT] | Flow graph canvas |
| [portable-file-dialogs](https://github.com/samhocevar/portable-file-dialogs) | 0.1.0, current | [WTFPL] (version 2) | Native app file dialogs |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | SDK 1.4.341.0, current | [Apache-2.0] OR [MIT] | Vulkan API headers |
| [Vulkan-Loader](https://github.com/KhronosGroup/Vulkan-Loader) | SDK 1.4.341.0, current | [Apache-2.0] | Vulkan runtime loader |
| [MoltenVK](https://github.com/KhronosGroup/MoltenVK) | 1.4.1, current on macOS | [Apache-2.0] | Vulkan portability runtime |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | selected by MoltenVK, current on macOS | [MIT] | MoltenVK shader translation |
| [Catch2](https://github.com/catchorg/Catch2) | 3.14.0, test only | [BSL-1.0] | Unit and integration tests |
| [glslang](https://github.com/KhronosGroup/glslang) | SDK 1.4.341.0, build only | [BSD-3-Clause] | Standalone Archimedes shader compilation fallback |
| [clang-format static binaries](https://github.com/muttleyxd/clang-tools-static-binaries) | LLVM 20, build only | [Apache-2.0] WITH [LLVM-exception] | Reproducible formatting tool |
| [OpenCV](https://github.com/opencv/opencv) | planned, version/modules TBD | [Apache-2.0] | ChArUco detection and calibration plugin |
| [Ceres Solver](https://github.com/ceres-solver/ceres-solver) | planned, version/config TBD | [BSD-3-Clause] | Registration refinement plugin |
| [Eigen](https://gitlab.com/libeigen/eigen) | planned, version TBD; approved exception | [MPL-2.0] | Ceres linear algebra, with `EIGEN_MPL2_ONLY` |
| [Abseil](https://github.com/abseil/abseil-cpp) | planned, version TBD | [Apache-2.0] | Ceres dependency |

Platform SDKs, GPU drivers, and operating-system utilities invoked by a dependency are not
redistributed by this repository and are not included in the table. Any newly enabled optional
module or transitive library updates this inventory in the same change that introduces it.

[Apache-2.0]: https://spdx.org/licenses/Apache-2.0.html
[BSD-3-Clause]: https://spdx.org/licenses/BSD-3-Clause.html
[BSL-1.0]: https://spdx.org/licenses/BSL-1.0.html
[libpng-2.0]: https://spdx.org/licenses/libpng-2.0.html
[libtiff]: https://spdx.org/licenses/libtiff.html
[LLVM-exception]: https://spdx.org/licenses/LLVM-exception.html
[MIT]: https://spdx.org/licenses/MIT.html
[MPL-2.0]: https://spdx.org/licenses/MPL-2.0.html
[WTFPL]: https://spdx.org/licenses/WTFPL.html
[Zlib]: https://spdx.org/licenses/Zlib.html
