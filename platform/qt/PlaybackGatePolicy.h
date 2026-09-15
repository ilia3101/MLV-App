/*!
 * \file PlaybackGatePolicy.h
 * \brief Deterministic PASS/FAIL acceptance gate for a playback smoke session,
 *        extracted so it can be unit-tested without the GUI.
 */

#ifndef PLAYBACKGATEPOLICY_H
#define PLAYBACKGATEPOLICY_H

/*! \brief Cumulative SESSION totals the gate decides over.
 *
 * Every field is a running total across the whole playback smoke session,
 * never a per-frame or per-tick value -- the policy is evaluated exactly
 * once per session, after the session ends.
 */
struct PlaybackGateCounters
{
    int framesPresented = 0;
    int decodeRequestCount = 0;
    int parityMatchCount = 0;
    int framesExpected = 0;
};

enum class PlaybackGateVerdict
{
    Pass,
    FramesPresentedBelowExpected,
    DecodeRequestsBelowExpected,
    ParityMismatch,
};

/*! \brief Pure decision: does a playback smoke session meet the acceptance bar?
 *
 * Wall-clock fps is deliberately absent from \c PlaybackGateCounters. A caller
 * may compute and report fps alongside the verdict for diagnostics, but it
 * must never be an input to this decision -- frame presentation, decode
 * throughput, and per-frame hash parity are the only gating signals.
 */
class PlaybackGatePolicy
{
public:
    static PlaybackGateVerdict evaluate( const PlaybackGateCounters &counters )
    {
        if( counters.framesPresented < counters.framesExpected )
            return PlaybackGateVerdict::FramesPresentedBelowExpected;
        if( counters.decodeRequestCount < counters.framesExpected )
            return PlaybackGateVerdict::DecodeRequestsBelowExpected;
        if( counters.parityMatchCount < counters.framesPresented )
            return PlaybackGateVerdict::ParityMismatch;
        return PlaybackGateVerdict::Pass;
    }

    static bool passes( const PlaybackGateCounters &counters )
    {
        return evaluate( counters ) == PlaybackGateVerdict::Pass;
    }
};

#endif // PLAYBACKGATEPOLICY_H
