#ifndef BATCHPROMPTS_H
#define BATCHPROMPTS_H

#include <QString>
#include <functional>

/* Helper class for dialog replacement in both batch and GUI mode.
 *
 * Batch mode: logs to stderr, returns based on BatchContext::skipErrors().
 * GUI mode:   shows the original QMessageBox with 3 buttons
 *             (Skip frame / Abort current export / Abort batch export).
 *
 * The "Abort batch export" button in GUI mode invokes an optional
 * callback set via setAbortBatchCallback(). */
class BatchPrompts
{
public:
    /* Returns true = skip this frame and continue, false = abort.
     * Called when saveDngFrame() fails for a single frame. */
    static bool shouldSkipFrame(const QString &clipName,
                                int frameIndex,
                                const QString &errorDetail);

    /* Returns true = continue processing, false = abort.
     * Called for non-frame errors (e.g. disk full, general warnings). */
    static bool shouldContinue(const QString &context,
                               const QString &message);

    /* Set a callback invoked when the GUI user clicks "Abort batch export".
     * MainWindow wires this in its constructor to call exportAbort(). */
    static void setAbortBatchCallback(std::function<void()> fn);

private:
    BatchPrompts() = delete; /* Pure static — no instances */

    static std::function<void()> s_abortBatchFn;
};

#endif // BATCHPROMPTS_H
