"""Source-level wiring guard for PLAY-COUNTERS-CPU.

`MainWindow.cpp` is not compiled by any test project, so the acceptance-gate
call site can't be proven by a unit test that links it. This module provides
`check_evaluate_call_uses_live_arguments`, a checker that parses the unique
`PlaybackGatePolicy::evaluate(...)` call in a piece of C++ source text and
reports any argument that is a bare numeric literal instead of a live
symbol/expression -- i.e. a call site that was satisfied by hardcoding a
constant instead of reading the real session counters.
"""
import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MAIN_WINDOW_CPP = REPO_ROOT / "platform" / "qt" / "MainWindow.cpp"
RENDER_FRAME_THREAD_CPP = REPO_ROOT / "platform" / "qt" / "RenderFrameThread.cpp"

EVALUATE_CALL_MARKER = "PlaybackGatePolicy::evaluate("
COUNTERS_INIT_MARKER = "PlaybackGateCounters{"
FINISH_TELEMETRY_SIGNATURE = "void MainWindow::finishPlaybackSmokeTelemetry("
NOTE_PRESENTED_FRAME_SIGNATURE = "void MainWindow::notePlaybackSmokePresentedFrame("
DECODE_COUNTER_NAME = "m_decodeRequestsIssuedCount"
FORBIDDEN_QUEUE_DEPTH_SYMBOL = "decodeRequestCountAtRequest"

NUMERIC_LITERAL_RE = re.compile(r"^-?\d+(\.\d+)?[fFuUlL]*$")


def _extract_balanced(source, open_index, open_char, close_char):
    """Return the text strictly between the bracket at `open_index` and its
    matching close bracket of the same kind."""
    assert source[open_index] == open_char
    depth = 1
    i = open_index + 1
    while depth > 0:
        if source[i] == open_char:
            depth += 1
        elif source[i] == close_char:
            depth -= 1
        i += 1
    return source[open_index + 1:i - 1]


def _split_top_level_args(text):
    """Split a call/aggregate-init argument list on top-level commas only."""
    args = []
    depth = 0
    current = []
    for ch in text:
        if ch in "([{":
            depth += 1
            current.append(ch)
        elif ch in ")]}":
            depth -= 1
            current.append(ch)
        elif ch == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    tail = "".join(current).strip()
    if tail:
        args.append(tail)
    return args


def check_evaluate_call_uses_live_arguments(source):
    """Return the top-level arguments of the unique
    `PlaybackGatePolicy::evaluate(...)` call in `source` that are bare
    numeric literals.

    If the call's argument is a `PlaybackGateCounters{...}` aggregate init,
    the four fields inside the braces are checked instead of the single
    outer argument. An empty return means every gating argument is a live
    symbol/expression. Raises ValueError unless the call site is unique.
    """
    call_starts = [m.start() for m in re.finditer(re.escape(EVALUATE_CALL_MARKER), source)]
    if len(call_starts) != 1:
        raise ValueError(
            "expected exactly one %r call site, found %d"
            % (EVALUATE_CALL_MARKER, len(call_starts))
        )

    call_open = call_starts[0] + len(EVALUATE_CALL_MARKER) - 1
    call_args_text = _extract_balanced(source, call_open, "(", ")")

    counters_offset = call_args_text.find(COUNTERS_INIT_MARKER)
    if counters_offset != -1:
        brace_open = counters_offset + len(COUNTERS_INIT_MARKER) - 1
        body = _extract_balanced(call_args_text, brace_open, "{", "}")
        args = _split_top_level_args(body)
    else:
        args = _split_top_level_args(call_args_text)

    return [arg for arg in args if NUMERIC_LITERAL_RE.match(arg)]


def _function_body_span(source, signature_marker):
    """Return (start, end) offsets spanning the function whose definition
    begins at `signature_marker`, where `end` is the offset just past the
    closing brace matching the function's opening brace."""
    start = source.index(signature_marker)
    brace_open = source.index("{", start)
    depth = 1
    i = brace_open + 1
    while depth > 0:
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
        i += 1
    return start, i


class PlaybackGatePolicyWiringTest(unittest.TestCase):
    def test_checker_rejects_a_hardcoded_literal_argument_fixture(self):
        # In-test fixture, not the real file: proves the checker itself
        # detects a hardcoded literal, rather than a regex matched against
        # the literal in isolation.
        fixture = """
        void run()
        {
            PlaybackGatePolicy::evaluate(PlaybackGateCounters{
                m_playbackSmokePresentedFrames,
                render_thread_decode_request_count_at_request,
                120,
                m_playbackSmokeExpectedFrames
            });
        }
        """
        violations = check_evaluate_call_uses_live_arguments(fixture)
        self.assertEqual(violations, ["120"])

    def test_checker_accepts_an_all_live_argument_fixture(self):
        fixture = """
        void run()
        {
            PlaybackGatePolicy::evaluate(PlaybackGateCounters{
                m_playbackSmokePresentedFrames,
                render_thread_decode_request_count_at_request,
                parity_match,
                m_playbackSmokeExpectedFrames
            });
        }
        """
        violations = check_evaluate_call_uses_live_arguments(fixture)
        self.assertEqual(violations, [])

    def test_checker_requires_a_unique_call_site(self):
        with self.assertRaises(ValueError):
            check_evaluate_call_uses_live_arguments("// no call site in this source")

    def test_main_window_evaluate_call_uses_live_arguments(self):
        source = MAIN_WINDOW_CPP.read_text(encoding="utf-8")
        if EVALUATE_CALL_MARKER not in source:
            self.skipTest(
                "MainWindow.cpp has no PlaybackGatePolicy::evaluate(...) call site yet; "
                "that wiring is added by PLAY-COUNTERS-CPU-B"
            )
        violations = check_evaluate_call_uses_live_arguments(source)
        self.assertEqual(violations, [])

    def test_evaluate_call_site_is_inside_finish_not_note(self):
        source = MAIN_WINDOW_CPP.read_text(encoding="utf-8")
        if EVALUATE_CALL_MARKER not in source:
            self.skipTest(
                "MainWindow.cpp has no PlaybackGatePolicy::evaluate(...) call site yet; "
                "that wiring is added by PLAY-COUNTERS-CPU-B"
            )
        finish_start, finish_end = _function_body_span(
            source, FINISH_TELEMETRY_SIGNATURE)
        note_start, note_end = _function_body_span(
            source, NOTE_PRESENTED_FRAME_SIGNATURE)
        call_offset = source.index(EVALUATE_CALL_MARKER)
        self.assertTrue(
            finish_start <= call_offset < finish_end,
            "evaluate(...) call must sit inside finishPlaybackSmokeTelemetry")
        self.assertFalse(
            note_start <= call_offset < note_end,
            "evaluate(...) call must not sit inside notePlaybackSmokePresentedFrame")

    def test_evaluate_call_does_not_reference_queue_depth_counter(self):
        source = MAIN_WINDOW_CPP.read_text(encoding="utf-8")
        if EVALUATE_CALL_MARKER not in source:
            self.skipTest(
                "MainWindow.cpp has no PlaybackGatePolicy::evaluate(...) call site yet; "
                "that wiring is added by PLAY-COUNTERS-CPU-B"
            )
        call_starts = [
            m.start() for m in re.finditer(re.escape(EVALUATE_CALL_MARKER), source)
        ]
        self.assertEqual(len(call_starts), 1)
        call_open = call_starts[0] + len(EVALUATE_CALL_MARKER) - 1
        call_args_text = _extract_balanced(source, call_open, "(", ")")
        self.assertNotIn(FORBIDDEN_QUEUE_DEPTH_SYMBOL, call_args_text)

    def test_decode_requests_issued_counter_is_only_ever_incremented(self):
        source = RENDER_FRAME_THREAD_CPP.read_text(encoding="utf-8")
        occurrences = [
            m.start() for m in re.finditer(re.escape(DECODE_COUNTER_NAME), source)
        ]
        self.assertGreaterEqual(
            len(occurrences), 1,
            "expected at least one use of %r" % DECODE_COUNTER_NAME)
        saw_increment = False
        for offset in occurrences:
            prefix = source[max(0, offset - 2):offset]
            after = source[offset + len(DECODE_COUNTER_NAME):offset + len(DECODE_COUNTER_NAME) + 12]
            after_stripped = after.lstrip()
            is_increment = (
                after_stripped.startswith(".fetch_add(")
                or after_stripped.startswith("++")
                or prefix == "++"
            )
            is_read = after_stripped.startswith(".load(")
            is_size_assignment = ".size()" in source[offset:offset + 60] and "=" in after_stripped[:4]
            is_bare_assignment = (
                after_stripped.startswith("=") and not after_stripped.startswith("==")
            )
            self.assertFalse(
                is_size_assignment or is_bare_assignment,
                "found a disallowed assignment to %r: %r"
                % (DECODE_COUNTER_NAME, source[offset - 2:offset + 60]))
            if is_increment:
                saw_increment = True
            self.assertTrue(
                is_increment or is_read,
                "found an unrecognized use of %r: %r"
                % (DECODE_COUNTER_NAME, source[offset - 2:offset + 40]))
        self.assertTrue(
            saw_increment,
            "expected at least one increment site for %r" % DECODE_COUNTER_NAME)


if __name__ == "__main__":
    unittest.main()
