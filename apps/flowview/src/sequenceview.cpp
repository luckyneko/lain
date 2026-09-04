#include "sequenceview.h"

#include <lain/gui/context.h>
#include <lain/gui/gui.h>
#include <lain/image/image.h>
#include <lain/media/framesequence.h>

#include <algorithm>
#include <cmath>

namespace flowview
{
	using namespace lain;

	// What to play a sequence at when it declares no rate of its own — a folder of stills genuinely
	// has none (FrameSpec's zero numerator is a real state, not a missing value). A viewing default
	// only: nothing is written, and the toolbar says so rather than implying the footage is 24 fps.
	static constexpr double kUnspecifiedRateHz = 24.0;

	void SequenceView::advancePlayback(double rate, std::size_t last)
	{
		const double step = 1.0 / rate;
		m_carry += static_cast<double>(gui::GetIO().DeltaTime);
		if (m_carry < step)
			return;

		// However many whole frames the elapsed time covers — so a slow decode DROPS frames instead
		// of stretching the clip. Playback here is best effort (ADR-0018): one decoder behind a
		// mutex cannot promise a frame per refresh, and pretending otherwise would play 4K footage
		// in slow motion while claiming to be at rate.
		const auto elapsed = static_cast<std::size_t>(m_carry / step);
		m_carry -= static_cast<double>(elapsed) * step;
		if (m_position + elapsed >= last)
		{
			m_position = last;
			m_playing = false; // stop at the end rather than wrap: a loop is a choice, not a default
		}
		else
		{
			m_position += elapsed;
		}
	}

	void SequenceView::showFrame(const media::FrameSequence& sequence, std::size_t position, gui::Context& gui)
	{
		// Recorded BEFORE the decode, and whatever the outcome: a frame that will not decode must not
		// be attempted again next frame.
		m_shownFrame = sequence.frame(position);

		const image::Image frame = sequence.image(position);
		if (!frame.valid())
		{
			m_texture = {}; // clear it — a stale frame shown as the current one is the worse failure
			return;
		}
		if (!m_texture.upload(frame))			  // re-upload in place when size/format fits...
			m_texture = gui.createTexture(frame); // ...else first sight or a resize -> reallocate
	}

	void SequenceView::draw(const flow::PortValue& value, const gui::Texture*, math::Vec2f area, gui::Context& gui)
	{
		if (!value.holds<media::FrameSequence>())
		{
			gui::TextDisabled("(no sequence to show)");
			return;
		}
		const media::FrameSequence& sequence = value.get<media::FrameSequence>();
		if (sequence.empty())
		{
			// A VALUE, not a failure: a folder that exists and holds no images opens as an empty
			// sequence, and saying so is different from saying the open failed.
			m_playing = false;
			gui::TextDisabled("(empty sequence - no frames)");
			return;
		}

		// The value can be rebound under the view by a re-run — a shorter clip, a different folder —
		// so the position is clamped every frame rather than trusted from the last one.
		const std::size_t last = sequence.size() - 1;
		m_position = std::min(m_position, last);

		const media::FrameRate rate = sequence.spec().rate;
		if (m_playing)
			advancePlayback(rate.specified() ? rate.hz() : kUnspecifiedRateHz, last);

		// Decode only when what is on screen is not the frame the transport names — which covers the
		// transport moving AND the pin being rebound under a stationary transport.
		if (sequence.frame(m_position) != m_shownFrame)
			showFrame(sequence, m_position, gui);

		// Two rows of chrome below the image: the transport, then the zoom toolbar. Reserved up front
		// so the fit scale is known before either draws.
		const math::Vec2f image{area.x, area.y - ImageCanvas::toolbarHeight() * 2.0f};
		if (m_texture.valid())
		{
			m_canvas.drawImage("frame", m_texture, m_texture.extent(), image);
		}
		else if (image.y > 0.0f)
		{
			// A hole, said out loud and in place. "Not shown" rather than "not decoded" because both
			// are possible — the frame would not decode, or its upload was refused — and this has not
			// established which.
			gui::BeginChild("frame", image);
			gui::TextDisabled("(frame %zu could not be shown)", m_position);
			gui::EndChild();
		}

		// Transport. Scrubbing takes over from playback — a user dragging the slider is steering, and
		// having it snatched back a frame later would be unusable.
		if (gui::Button(m_playing ? "Pause" : "Play"))
		{
			m_playing = !m_playing;
			m_carry = 0.0;
		}
		if (!rate.specified())
			gui::SetItemTooltip("This sequence declares no rate - playing at %.0f fps", kUnspecifiedRateHz);
		gui::SameLine();
		if (gui::Button("<") && m_position > 0)
		{
			--m_position;
			m_playing = false;
		}
		gui::SameLine();
		if (gui::Button(">") && m_position < last)
		{
			++m_position;
			m_playing = false;
		}
		gui::SameLine();
		gui::SetNextItemWidth(-1.0f);
		// 0-based, like every other frame position in lain: `--frame 0-499` addresses the same frames
		// this slider does, and a 1-based display here would be the one place they disagreed.
		int position = static_cast<int>(m_position);
		if (gui::SliderInt("##position", &position, 0, static_cast<int>(last), "frame %d"))
		{
			m_position = static_cast<std::size_t>(std::clamp(position, 0, static_cast<int>(last)));
			m_playing = false;
		}

		m_canvas.drawToolbar();
	}
} // namespace flowview
