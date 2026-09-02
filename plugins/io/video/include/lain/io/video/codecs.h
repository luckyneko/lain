#pragma once

namespace lain::io::video
{
	// Register every built-in video codec — the plugins enabled at build time — into the
	// io::video reader registry. Call once at startup, before io::video::open() or
	// io::sequence::open(); it is the app's single video-codec-wiring point, mirroring
	// registerImageCodecs.
	//
	// TWO WORDS, DELIBERATELY. What is registered is a BACKEND (io::video's registry is keyed by
	// backend name, and "ffmpeg" is neither a container nor a codec — see reader.h); what is
	// BUILT is "the video codec plugin", which is what ADR-0019, the README and the
	// LAIN_IO_VIDEO_FFMPEG option all call the dependency. This function is named for the
	// dependency it wires, and for its image-side peer.
	//
	// The set is determined by the build (which plugins/io/video/<codec> were enabled), not
	// hand-listed: this function is generated from the discovered codec list, so adding a codec
	// plugin needs no edit here.
	//
	// WITH NO CODECS ENABLED IT IS A NO-OP, AND CALLING IT IS STILL RIGHT. That is the whole
	// shape of the video opt-in: the seam is always built, so a document naming an .mp4 keeps its
	// node and its edges and reports a missing capability when run, rather than losing a node kind
	// and coming back structurally damaged (ADR-0019, amended).
	void registerVideoCodecs();
} // namespace lain::io::video
