#include "../common/minitest.h"

#include "../../platform/qt/PlaybackGatePolicy.h"

TEST(PlaybackGatePolicy, PassesOnFullParityAllPresentedVector)
{
    PlaybackGateCounters counters;
    counters.framesPresented = 120;
    counters.decodeRequestCount = 120;
    counters.parityMatchCount = 120;
    counters.framesExpected = 120;

    ASSERT_TRUE(PlaybackGatePolicy::passes(counters));
    ASSERT_TRUE(PlaybackGatePolicy::evaluate(counters) == PlaybackGateVerdict::Pass);
}

TEST(PlaybackGatePolicy, FailsWhenPresentedBelowExpected)
{
    PlaybackGateCounters counters;
    counters.framesPresented = 119;
    counters.decodeRequestCount = 120;
    counters.parityMatchCount = 119;
    counters.framesExpected = 120;

    ASSERT_FALSE(PlaybackGatePolicy::passes(counters));
    ASSERT_TRUE(PlaybackGatePolicy::evaluate(counters)
                == PlaybackGateVerdict::FramesPresentedBelowExpected);
}

TEST(PlaybackGatePolicy, FailsWhenParityBelowPresented)
{
    PlaybackGateCounters counters;
    counters.framesPresented = 120;
    counters.decodeRequestCount = 120;
    counters.parityMatchCount = 119;
    counters.framesExpected = 120;

    ASSERT_FALSE(PlaybackGatePolicy::passes(counters));
    ASSERT_TRUE(PlaybackGatePolicy::evaluate(counters) == PlaybackGateVerdict::ParityMismatch);
}

TEST(PlaybackGatePolicy, FailsWhenDecodeRequestsBelowExpected)
{
    PlaybackGateCounters counters;
    counters.framesPresented = 120;
    counters.decodeRequestCount = 119;
    counters.parityMatchCount = 120;
    counters.framesExpected = 120;

    ASSERT_FALSE(PlaybackGatePolicy::passes(counters));
    ASSERT_TRUE(PlaybackGatePolicy::evaluate(counters)
                == PlaybackGateVerdict::DecodeRequestsBelowExpected);
}

// L-CHEAT: a caller could try to satisfy the gate by hardcoding
// decodeRequestCount to the expected constant instead of reading the live
// counter. The parity comparison is independent of that input and must still
// fail the gate.
TEST(PlaybackGatePolicy, ConstantDecodeRequestCannotMaskAParityFailure)
{
    PlaybackGateCounters counters;
    counters.framesPresented = 120;
    counters.decodeRequestCount = 120; // constant, equal to framesExpected
    counters.parityMatchCount = 0;     // genuinely disagrees
    counters.framesExpected = 120;

    ASSERT_FALSE(PlaybackGatePolicy::passes(counters));
    ASSERT_TRUE(PlaybackGatePolicy::evaluate(counters) == PlaybackGateVerdict::ParityMismatch);
}
