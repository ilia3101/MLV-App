#ifndef BATCHPROMPTS_H
#define BATCHPROMPTS_H

#include <QString>

/* Helper class for replacing QMessageBox dialogs in batch mode.
 * In GUI mode, the original QMessageBox calls remain active.
 * In batch mode, these static methods decide skip-or-abort based on
 * BatchContext::skipErrors() and log the decision to stdout.
 *
 * Phase 4 will provide the real implementation.
 * For now, both methods are stubs that return false (abort). */
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

private:
    BatchPrompts() = delete; /* Pure static — no instances */
};

#endif // BATCHPROMPTS_H
