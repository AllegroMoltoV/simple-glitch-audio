#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sg {

// The host's transport for one block. When the host is not playing or gives no beat position, the slices
// follow an internal clock at the host's tempo, or at 120 BPM without one.
struct Transport {
	bool playing = false;
	bool hasBeats = false;
	bool hasTempo = false;
	double beats = 0.0; // Position of the first sample of the block, in quarter notes.
	double tempo = 120.0;
};

enum class GlitchType { None, Stutter, Gate, Reverse };

struct SliceDecision {
	GlitchType type = GlitchType::None;
	int division = 1; // Stutter only: the slice head of 1/division repeats.
};

// splitmix64's finalizer. The same slice number always gives the same decision.
inline uint64_t mix64(uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

inline SliceDecision decideSlice(int64_t sliceIndex, uint64_t salt, double amount)
{
	const uint64_t h = mix64(uint64_t(sliceIndex) ^ salt);
	const double u = double(h >> 11) * (1.0 / 9007199254740992.0); // [0, 1) from 53 bits
	if (u >= amount)
		return {};
	const uint64_t h2 = mix64(h);
	static constexpr std::array<int, 3> kDivisions{2, 4, 8};
	switch (h2 % 3) {
	case 0:
		return {GlitchType::Stutter, kDivisions[(h2 / 3) % 3]};
	case 1:
		return {GlitchType::Gate, 1};
	default:
		return {GlitchType::Reverse, 1};
	}
}

class GlitchEngine {
public:
	// Slice lengths as note values (1/32, 1/16, 1/8 and 1/4 notes), in quarter-note beats.
	static constexpr std::array<double, 4> kSliceBeats{1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0};
	static constexpr std::array<const char *, 4> kSliceNames{"1/32", "1/16", "1/8", "1/4"};
	static constexpr int kDefaultSlice = 1;
	static constexpr double kMinTempo = 30.0;
	static constexpr double kMaxTempo = 300.0;
	static constexpr double kDefaultTempo = 120.0;
	static constexpr double kFadeSeconds = 0.002;
	// Different salts keep the internal clock's slices from repeating the timeline's decisions.
	static constexpr uint64_t kTimelineSalt = 0x5347;
	static constexpr uint64_t kInternalSalt = 0x5347494E54ull;

	static int clampSlice(int index)
	{
		return std::clamp(index, 0, int(kSliceBeats.size()) - 1);
	}

	void prepare(double sampleRate)
	{
		sampleRate_ = sampleRate;
		fade_ = std::max(1, int(std::lround(sampleRate * kFadeSeconds)));
		// Reverse reads the whole previous slice, so the history holds two of the longest slices.
		const double longest = sampleRate * 60.0 / kMinTempo * kSliceBeats.back();
		size_t size = 1;
		while (double(size) < 2.0 * longest + 4.0 * fade_ + 16.0)
			size <<= 1;
		for (auto &buffer : history_)
			buffer.assign(size, 0.0f);
		mask_ = int64_t(size - 1);
		// Reverse reads 2 * offset + 1 samples behind the newest one.
		reverseLimit_ = int64_t(size / 2) - 2 * fade_;
		reset();
	}

	void reset()
	{
		for (auto &buffer : history_)
			std::fill(buffer.begin(), buffer.end(), 0.0f);
		now_ = 0;
		started_ = false;
		usingTimeline_ = false;
		sliceIndex_ = 0;
		sliceStart_ = 0;
		offset_ = 0;
		decision_ = {};
		repeat_ = 1;
		gateAt_ = 0;
		expected_ = 0;
		direction_ = 1;
		oldPos_ = 0;
		oldDirection_ = 1;
		fadeLeft_ = 0;
		gain_ = 1.0f;
		holdPhase_ = 1.0;
		held_ = {0.0f, 0.0f};
		slicesStarted_ = 0;
	}

	// amount, crush and mix are 0 to 1. slice indexes kSliceBeats.
	void setParams(double amount, int slice, double crush, double mix)
	{
		amount_ = std::clamp(amount, 0.0, 1.0);
		slice_ = clampSlice(slice);
		crush_ = std::clamp(crush, 0.0, 1.0);
		mix_ = float(std::clamp(mix, 0.0, 1.0));
	}

	// Processes the two channels in place. left and right must point to different buffers.
	void process(float *left, float *right, uint32_t frames, const Transport &transport)
	{
		const double tempo = transport.hasTempo ? std::clamp(transport.tempo, kMinTempo, kMaxTempo) : kDefaultTempo;
		const double samplesPerBeat = sampleRate_ * 60.0 / tempo;
		const double sliceBeats = kSliceBeats[size_t(slice_)];
		const double sliceLength = std::max(1.0, sliceBeats * samplesPerBeat);
		const bool timeline = transport.playing && transport.hasBeats;
		float *channels[2] = {left, right};

		for (uint32_t i = 0; i < frames; ++i) {
			const size_t w = size_t(now_ & mask_);
			history_[0][w] = left[i];
			history_[1][w] = right[i];

			if (timeline) {
				const double beat = transport.beats + double(i) / samplesPerBeat;
				const auto index = int64_t(std::floor(beat / sliceBeats + 1e-9));
				if (!started_ || !usingTimeline_ || index != sliceIndex_)
					startSlice(index, true, sliceLength);
			} else if (!started_ || usingTimeline_ || double(offset_) >= sliceLength) {
				startSlice(started_ && !usingTimeline_ ? sliceIndex_ + 1 : 0, false, sliceLength);
			}

			int64_t pos = now_;
			int direction = 1;
			float targetGain = 1.0f;
			switch (decision_.type) {
			case GlitchType::Stutter: {
				// The last repeat runs to the end of the slice, so no jump falls just before the next slice.
				const int64_t repeatIndex = std::min<int64_t>(offset_ / repeat_, decision_.division - 1);
				pos = sliceStart_ + offset_ - repeatIndex * repeat_;
				break;
			}
			case GlitchType::Gate:
				targetGain = offset_ < gateAt_ ? 1.0f : 0.0f;
				break;
			case GlitchType::Reverse:
				// Reading on past the previous slice keeps the reader moving without a jump when the timeline
				// makes this slice a sample longer than the previous one. The history bounds the distance.
				pos = sliceStart_ - 1 - offset_ % reverseLimit_;
				direction = -1;
				break;
			case GlitchType::None:
				break;
			}

			// A jump in the read position starts a crossfade from the position the reader would have read next.
			if (pos != expected_) {
				oldPos_ = expected_;
				oldDirection_ = direction_;
				fadeLeft_ = fade_;
			}
			const float step = 1.0f / float(fade_);
			gain_ = targetGain > gain_ ? std::min(targetGain, gain_ + step) : std::max(targetGain, gain_ - step);
			const float oldWeight = fadeLeft_ > 0 ? float(fadeLeft_) / float(fade_) : 0.0f;

			// Decimation: a new sample is taken every 1 + 15 * Crush samples and held until the next one.
			const bool crushing = crush_ > 0.0;
			bool takeSample = true;
			if (crushing) {
				takeSample = holdPhase_ >= 1.0;
				if (takeSample)
					holdPhase_ -= std::floor(holdPhase_);
				holdPhase_ += 1.0 / (1.0 + 15.0 * crush_);
			}

			for (int c = 0; c < 2; ++c) {
				const auto &buffer = history_[c];
				float wet = buffer[size_t(pos & mask_)];
				if (oldWeight > 0.0f)
					wet += (buffer[size_t(oldPos_ & mask_)] - wet) * oldWeight;
				wet *= gain_;
				if (crushing) {
					if (takeSample)
						held_[c] = quantize(wet);
					wet = held_[c];
				}
				// dry + (wet - dry) can differ from wet in the last bit, so Mix 100% takes wet as it is.
				// The blend keeps the dry signal exact when nothing is processed.
				const float dry = channels[c][i];
				channels[c][i] = mix_ == 1.0f ? wet : dry + mix_ * (wet - dry);
			}

			if (fadeLeft_ > 0) {
				oldPos_ += oldDirection_;
				--fadeLeft_;
			}
			expected_ = pos + direction;
			direction_ = direction;
			++offset_;
			++now_;
		}
		// When Crush is raised from 0, the first sample is taken at once.
		if (crush_ == 0.0)
			holdPhase_ = 1.0;
	}

	// For the tests and for the host's display: the decision for the slice being played.
	const SliceDecision &decision() const
	{
		return decision_;
	}
	int64_t sliceIndex() const
	{
		return sliceIndex_;
	}
	// The position of the last processed sample in its slice.
	int64_t offsetInSlice() const
	{
		return offset_ - 1;
	}
	int64_t repeatLength() const
	{
		return repeat_;
	}
	int64_t gateAt() const
	{
		return gateAt_;
	}
	uint64_t slicesStarted() const
	{
		return slicesStarted_;
	}
	int fadeLength() const
	{
		return fade_;
	}

private:
	void startSlice(int64_t index, bool timeline, double sliceLength)
	{
		started_ = true;
		usingTimeline_ = timeline;
		sliceIndex_ = index;
		sliceStart_ = now_;
		offset_ = 0;
		++slicesStarted_;
		decision_ = decideSlice(index, timeline ? kTimelineSalt : kInternalSalt, amount_);
		const auto length = int64_t(sliceLength);
		gateAt_ = length / 2;
		if (decision_.type == GlitchType::Stutter) {
			// A repeat shorter than two crossfades would be all crossfade.
			while (decision_.division > 1 && length / decision_.division < 2 * fade_)
				decision_.division /= 2;
			if (decision_.division == 1)
				decision_ = {};
		}
		repeat_ = std::max<int64_t>(1, length / std::max(1, decision_.division));
	}

	float quantize(float x) const
	{
		const double bits = 16.0 - 12.0 * crush_;
		const double levels = std::exp2(bits - 1.0);
		return float(std::round(double(x) * levels) / levels);
	}

	double sampleRate_ = 48000.0;
	int fade_ = 96;
	std::array<std::vector<float>, 2> history_;
	int64_t mask_ = 0;
	int64_t reverseLimit_ = 1;
	int64_t now_ = 0;

	double amount_ = 0.5;
	int slice_ = kDefaultSlice;
	double crush_ = 0.0;
	float mix_ = 1.0f;

	bool started_ = false;
	bool usingTimeline_ = false;
	int64_t sliceIndex_ = 0;
	int64_t sliceStart_ = 0;
	int64_t offset_ = 0;
	SliceDecision decision_{};
	int64_t repeat_ = 1;
	int64_t gateAt_ = 0;

	int64_t expected_ = 0;
	int direction_ = 1;
	int64_t oldPos_ = 0;
	int oldDirection_ = 1;
	int fadeLeft_ = 0;
	float gain_ = 1.0f;

	double holdPhase_ = 1.0;
	std::array<float, 2> held_{};
	uint64_t slicesStarted_ = 0;
};

} // namespace sg
