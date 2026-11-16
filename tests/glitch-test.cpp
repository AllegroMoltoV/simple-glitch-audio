// Tests of the glitch engine: pass-through, slice lengths, the content of each glitch, the click bound,
// reproducibility and the bit crusher.
#include "glitch-engine.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what, double sampleRate, double value = 0.0)
{
	if (!ok) {
		std::printf("FAIL %s at %.0f Hz (value %.6g)\n", what, sampleRate, value);
		++failures;
	}
}

constexpr double kRates[] = {44100.0, 48000.0, 96000.0};

uint32_t nextRandom(uint32_t &state)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

std::vector<float> noise(size_t frames, uint32_t seed)
{
	std::vector<float> x(frames);
	for (auto &v : x)
		v = float(nextRandom(seed) >> 8) / float(1 << 24) * 2.0f - 1.0f;
	return x;
}

sg::Transport timeline(double tempo, double beats)
{
	sg::Transport t;
	t.playing = true;
	t.hasBeats = true;
	t.hasTempo = true;
	t.tempo = tempo;
	t.beats = beats;
	return t;
}

// Runs the engine in blocks, advancing the timeline when one is given.
void run(sg::GlitchEngine &engine, std::vector<float> &left, std::vector<float> &right, uint32_t block,
	 double sampleRate, const sg::Transport *start)
{
	for (size_t at = 0; at < left.size(); at += block) {
		const auto frames = uint32_t(std::min<size_t>(block, left.size() - at));
		sg::Transport t;
		if (start) {
			t = *start;
			t.beats += double(at) * t.tempo / 60.0 / sampleRate;
		}
		engine.process(left.data() + at, right.data() + at, frames, t);
	}
}

// With Amount and Crush at 0, the output is the input itself, at every Mix, so the latency is 0.
void testPassThrough()
{
	for (double rate : kRates) {
		for (double mix : {1.0, 0.5, 0.0}) {
			for (bool withTimeline : {true, false}) {
				const auto inL = noise(size_t(rate), 1);
				const auto inR = noise(size_t(rate), 2);
				auto l = inL, r = inR;
				sg::GlitchEngine engine;
				engine.prepare(rate);
				engine.setParams(0.0, 1, 0.0, mix);
				const auto t = timeline(128.0, 3.0);
				run(engine, l, r, 512, rate, withTimeline ? &t : nullptr);
				bool same = true;
				for (size_t i = 0; i < l.size(); ++i)
					same = same && l[i] == inL[i] && r[i] == inR[i];
				check(same, "pass-through", rate, mix);
			}
		}
	}
}

// Slices follow the tempo: on the timeline, slice n starts at n * length; on the internal clock, every
// ceil(length) samples.
void testSliceLengths()
{
	for (double rate : kRates) {
		for (int slice = 0; slice < 4; ++slice) {
			for (double tempo : {90.0, 120.0, 174.0}) {
				const double length = rate * 60.0 / tempo * sg::GlitchEngine::kSliceBeats[size_t(slice)];
				const size_t frames = size_t(rate * 4.0);
				std::vector<float> l(frames), r(frames);

				sg::GlitchEngine onTimeline;
				onTimeline.prepare(rate);
				onTimeline.setParams(0.0, slice, 0.0, 1.0);
				const auto t = timeline(tempo, 0.0);
				bool startsMatch = true;
				for (size_t i = 0; i < frames; ++i) {
					auto ti = t;
					ti.beats = double(i) * tempo / 60.0 / rate;
					onTimeline.process(&l[i], &r[i], 1, ti);
					const auto expected = int64_t(std::floor(double(i) / length + 1e-9));
					startsMatch = startsMatch && onTimeline.sliceIndex() == expected;
				}
				check(startsMatch, "timeline slice index", rate, length);

				sg::GlitchEngine internal;
				internal.prepare(rate);
				internal.setParams(0.0, slice, 0.0, 1.0);
				sg::Transport stopped;
				stopped.hasTempo = true;
				stopped.tempo = tempo;
				std::vector<float> l2(frames), r2(frames);
				run(internal, l2, r2, 333, rate, nullptr);
				// run() without a timeline sends no tempo, so the internal clock is checked at 120 BPM here.
				const double internalLength = std::ceil(rate * 60.0 / 120.0 * sg::GlitchEngine::kSliceBeats[size_t(slice)]);
				const auto expectedSlices = uint64_t(std::ceil(double(frames) / internalLength));
				check(internal.slicesStarted() == expectedSlices, "internal clock slices at 120 BPM", rate,
				      double(internal.slicesStarted()));

				sg::GlitchEngine withTempo;
				withTempo.prepare(rate);
				withTempo.setParams(0.0, slice, 0.0, 1.0);
				for (size_t at = 0; at < frames; at += 256)
					withTempo.process(&l2[at], &r2[at], uint32_t(std::min<size_t>(256, frames - at)), stopped);
				const auto expectedWithTempo = uint64_t(std::ceil(double(frames) / std::ceil(length)));
				check(withTempo.slicesStarted() == expectedWithTempo, "internal clock slices at the host tempo", rate,
				      double(withTempo.slicesStarted()));
			}
		}
	}
}

// Each glitch plays what the specification says, outside the crossfades.
void testGlitchContent()
{
	for (double rate : kRates) {
		const size_t frames = size_t(rate * 16.0);
		std::vector<float> in(frames);
		for (size_t i = 0; i < frames; ++i)
			in[i] = float(i) / float(frames); // Every sample is different, so the source position is visible.
		auto l = in, r = in;
		sg::GlitchEngine engine;
		engine.prepare(rate);
		engine.setParams(1.0, 1, 0.0, 1.0);
		const int fade = engine.fadeLength();
		int seen[4] = {};
		bool stutterOk = true, gateOk = true, reverseOk = true, linked = true;
		for (size_t i = 0; i < frames; ++i) {
			auto t = timeline(120.0, double(i) * 2.0 / rate);
			engine.process(&l[i], &r[i], 1, t);
			const auto &d = engine.decision();
			const int64_t offset = engine.offsetInSlice();
			const int64_t start = int64_t(i) - offset;
			if (offset == 0)
				++seen[int(d.type)];
			if (l[i] != r[i])
				linked = false; // Both channels follow the same decision.
			switch (d.type) {
			case sg::GlitchType::Stutter: {
				const int64_t repeat = engine.repeatLength();
				const int64_t k = std::min<int64_t>(offset / repeat, d.division - 1);
				const int64_t within = offset - k * repeat;
				if (within >= fade && offset >= fade)
					stutterOk = stutterOk && l[i] == in[size_t(start + within)];
				break;
			}
			case sg::GlitchType::Gate:
				if (offset >= fade && offset < engine.gateAt())
					gateOk = gateOk && l[i] == in[i];
				if (offset >= engine.gateAt() + fade)
					gateOk = gateOk && l[i] == 0.0f;
				break;
			case sg::GlitchType::Reverse: {
				const int64_t source = start - 1 - offset;
				if (offset >= fade)
					reverseOk = reverseOk && l[i] == (source >= 0 ? in[size_t(source)] : 0.0f);
				break;
			}
			case sg::GlitchType::None:
				break;
			}
		}
		check(stutterOk, "stutter repeats the slice head", rate);
		check(gateOk, "gate silences the second half", rate);
		check(reverseOk, "reverse plays the previous slice backwards", rate);
		check(linked, "both channels follow the same decision", rate);
		check(seen[int(sg::GlitchType::None)] == 0, "Amount 100% glitches every slice", rate,
		      seen[int(sg::GlitchType::None)]);
		for (int type = 1; type < 4; ++type)
			check(seen[type] > 0, "every glitch type occurs", rate, type);
	}
}

// The crossfades keep the step between neighbouring samples near the step of the input itself.
void testClickBound()
{
	for (double rate : kRates) {
		for (int slice = 0; slice < 4; ++slice) {
			const size_t frames = size_t(rate * 8.0);
			const double w = 2.0 * 3.141592653589793 * 440.0 / rate;
			std::vector<float> l(frames), r(frames);
			for (size_t i = 0; i < frames; ++i)
				l[i] = r[i] = float(0.5 * std::sin(w * double(i)));
			sg::GlitchEngine engine;
			engine.prepare(rate);
			engine.setParams(1.0, slice, 0.0, 1.0);
			const auto t = timeline(140.0, 0.0);
			run(engine, l, r, 256, rate, &t);
			double worst = 0.0;
			for (size_t i = 1; i < frames; ++i)
				worst = std::max(worst, double(std::fabs(l[i] - l[i - 1])));
			// The input steps by up to 0.5 * w. A crossfade adds up to 1 / fade, the full swing of the sine
			// spread over the fade; the gate's ramp adds up to 0.5 / fade.
			// The fade is the 2 ms of the specification, not the engine's own value, so a shorter fade fails here.
			const double bound = 0.5 * w + 1.5 / std::round(rate * 0.002);
			check(worst <= bound, "click bound", rate, worst);
		}
	}
}

// The same timeline position gives the same output, whatever the block size.
void testReproducible()
{
	for (double rate : kRates) {
		const auto in = noise(size_t(rate * 6.0), 7);
		auto a = in, b = in, a2 = in, b2 = in;
		const auto t = timeline(100.0, 0.0);
		sg::GlitchEngine first;
		first.prepare(rate);
		first.setParams(0.6, 2, 0.3, 0.8);
		run(first, a, a2, 512, rate, &t);
		sg::GlitchEngine second;
		second.prepare(rate);
		second.setParams(0.6, 2, 0.3, 0.8);
		run(second, b, b2, 97, rate, &t);
		bool same = true;
		for (size_t i = 0; i < a.size(); ++i)
			same = same && a[i] == b[i] && a2[i] == b2[i];
		check(same, "same position, same output", rate);

		// After reset, playing the same part again gives the same output.
		auto c = in, c2 = in;
		first.reset();
		run(first, c, c2, 512, rate, &t);
		bool again = true;
		for (size_t i = 0; i < a.size(); ++i)
			again = again && a[i] == c[i];
		check(again, "replay after reset", rate);
	}
}

// Crush at 100%: 4 bits, so multiples of 1/8, and every value is held for 16 samples.
void testCrush()
{
	for (double rate : kRates) {
		const auto in = noise(4096, 11);
		auto l = in, r = in;
		sg::GlitchEngine engine;
		engine.prepare(rate);
		engine.setParams(0.0, 1, 1.0, 1.0);
		run(engine, l, r, 128, rate, nullptr);
		bool levels = true, held = true;
		for (size_t i = 0; i < l.size(); ++i) {
			levels = levels && std::fabs(l[i] * 8.0f - std::round(l[i] * 8.0f)) < 1e-6f;
			if (i % 16 != 0)
				held = held && l[i] == l[i - 1];
		}
		check(levels, "crush to 4 bits", rate);
		check(held, "crush holds each value for 16 samples", rate);
	}
}

} // namespace

int main()
{
	testPassThrough();
	testSliceLengths();
	testGlitchContent();
	testClickBound();
	testReproducible();
	testCrush();
	if (failures == 0)
		std::printf("all glitch engine tests passed\n");
	return failures == 0 ? 0 : 1;
}
