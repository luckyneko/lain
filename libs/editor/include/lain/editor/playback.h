#pragma once

#include <cstddef>

namespace lain::editor
{
	// A transport's clock: what elapsed time does to a position on a timeline of frames at a rate. It
	// moves by WHOLE frames of the time that has passed and carries the rest, so it never runs ahead of
	// the clock, and when a frame takes longer to show than the rate allows it skips the frames it had
	// no time for, rather than playing slowly while claiming to play at rate: best effort (ADR-0018).
	//
	// It holds no position. The caller owns that, because what a position means (a sequence's frame,
	// a capture timeline's) and what else moves it (a scrubber, a selected key) are the caller's; the
	// clock only answers how far one stretch of playing takes it. It reads no clock either: the caller
	// says how much time passed, so a test can say it exactly.
	//
	// The rate an app plays at when its timeline states none is that app's own choice, not this one's.
	class Playback
	{
	public:
		explicit Playback(bool loop = false)
			: m_loop(loop)
		{
		}

		bool playing() const { return m_playing; }
		// Starting counts afresh, so time spent paused is not owed to the next frame.
		void play()
		{
			m_playing = true;
			m_carry = 0.0;
		}
		void pause() { m_playing = false; }
		void toggle()
		{
			if (m_playing)
				pause();
			else
				play();
		}

		// Whether playing past the last frame goes back to the first, or stops there.
		bool loops() const { return m_loop; }
		void setLoop(bool loop) { m_loop = loop; }

		// Where `position` is once `seconds` more of playing have passed at `rate` frames a second, on a
		// timeline of frames 0 to `last`: on by however many whole frames that time covers, the rest
		// carried to the next call. Reaching `last` without a loop stops there and pauses; with one, the
		// position goes round. Paused, or at a rate that is not a positive finite number, it stays put.
		std::size_t advance(std::size_t position, std::size_t last, double rate, double seconds);

	private:
		bool m_playing = false;
		bool m_loop = false;
		double m_carry = 0.0; // seconds played toward the next whole frame
	};
} // namespace lain::editor
