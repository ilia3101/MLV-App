#include "BatchPrompts.h"

/* Stub implementations — Phase 4 will add real logic using
 * BatchContext::isBatchMode() and BatchContext::skipErrors(). */

bool BatchPrompts::shouldSkipFrame(const QString &clipName,
                                   int frameIndex,
                                   const QString &errorDetail)
{
    Q_UNUSED(clipName);
    Q_UNUSED(frameIndex);
    Q_UNUSED(errorDetail);
    return false; /* Abort by default until Phase 4 */
}

bool BatchPrompts::shouldContinue(const QString &context,
                                  const QString &message)
{
    Q_UNUSED(context);
    Q_UNUSED(message);
    return false; /* Abort by default until Phase 4 */
}
