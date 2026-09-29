# Top-level plugins/ — swappable satellite implementations that plug into a lain seam,
# kept apart from the stable interfaces in libs/ (as in thorax's own layout). Static
# today, thorax DSOs later, behind the same registration seam. Currently: the image
# codec plugins (lain::io::image), the serialization codec plugins (lain::io::data),
# the opt-in FFmpeg video codec plugin (lain::io::video), and the opt-in OpenCV camera
# plugin (M9; a probe of the linked library until slice 1).
add_subdirectory(io/image)
add_subdirectory(io/data)
add_subdirectory(io/video)
add_subdirectory(camera)
