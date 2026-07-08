# Top-level plugins/ — swappable satellite implementations that plug into a lain seam,
# kept apart from the stable interfaces in libs/ (as in thorax's own layout). Static
# today, thorax DSOs later, behind the same registration seam. Currently: the image
# codec plugins that register into lain::io::image.
add_subdirectory(io/image)
