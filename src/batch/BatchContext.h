#ifndef BATCHCONTEXT_H
#define BATCHCONTEXT_H

#include <QString>

/* Static singleton holding batch-mode flags.
 * Set once at startup from CLI args, read throughout the codebase.
 * Intentionally uses a separate .cpp for static member definitions
 * to avoid MinGW linker issues with inline statics. */
class BatchContext
{
public:
    static void setBatchMode(bool enabled);
    static bool isBatchMode();

    static void setSkipErrors(bool skip);
    static bool skipErrors();

    static void setVerbose(bool verbose);
    static bool isVerbose();

    static void setLogPath(const QString &path);
    static QString logPath();

    static void setReceiptPath(const QString &path);
    static QString receiptPath();

private:
    BatchContext() = delete; /* Pure static — no instances */

    static bool s_batchMode;
    static bool s_skipErrors;
    static bool s_verbose;
    static QString s_logPath;
    static QString s_receiptPath;
};

#endif // BATCHCONTEXT_H
