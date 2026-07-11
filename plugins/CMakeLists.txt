# Top-level plugins/ — swappable satellite implementations that plug into a lain seam,
# kept apart from the stable interfaces in libs/ (as in thorax's own layout). Static
# today, thorax DSOs later, behind the same registration seam. Currently: the image
# codec plugins (lain::io::image) and the serialization codec plugins (lain::io::data).
add_subdirectory(io/image)
add_subdirectory(io/data)
