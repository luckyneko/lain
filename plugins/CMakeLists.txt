# Top-level plugins/ — swappable satellite implementations that plug into a lain seam,
# kept apart from the stable interfaces in libs/ (as in thorax's own layout). Static
# today, thorax DSOs later, behind the same registration seam. Currently: the image
# codec plugins (lain::io::image), the serialization codec plugins (lain::io::data),
# the FFmpeg video codec plugin (lain::io::video), and the OpenCV camera plugin (M9).
#
# Every plugin follows one shape: an option says what is wanted, the plugin's add<Dep>.cmake says
# whether this platform can have it (LAIN_<DEP>_FOUND), and an unavailable plugin is simply not
# built — so anything that needs to know asks for the plugin TARGET, never the option. The two
# fetched prebuilts (FFmpeg, OpenCV) default ON only for a top-level build; the source-built codecs
# default ON everywhere.
add_subdirectory(io/image)
add_subdirectory(io/data)
add_subdirectory(io/video)
add_subdirectory(camera)
