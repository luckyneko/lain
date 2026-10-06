#pragma once

// What the feature facades answer (ADR-0016, "Producers propose; lain decides"): always a value, so
// a caller learns about a missing capability from the same field it reads every other outcome from.
namespace lain::camera::feature
{
	enum class Status
	{
		Ok,
		TooFew,			   // too little input for the question: fewer features, pairs or points than it needs
		NoSolution,		   // the backend proposed nothing that the measurement accepts
		Unsupported,	   // a backend is registered, but none can do what was asked
		NoBackend,		   // this build has no backend for this seam
		BackendMisbehaved, // the backend's answer broke its contract; refused rather than indexed
		InvalidInput,	   // the caller's input is malformed (a camera named twice, a frame its footage lacks)
	};
} // namespace lain::camera::feature
