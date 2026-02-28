#ifndef BATCHLOGGER_H
#define BATCHLOGGER_H

#include <QString>

/* Tiny static helper that mirrors batch output to a log file.
 * stdout lines go through out(), stderr lines go through err().
 * When no --log was given, these just write to the console. */
class BatchLogger
{
public:
    /* Open the log file.  Call once from runBatch() after parsing args.
     * If logPath is empty, logging goes to console only.
     * If the file can't be opened, prints a warning to stderr and continues. */
    static void init(const QString &logPath);

    /* Write a line to stdout (and mirror to log file if open). */
    static void out(const QString &line);

    /* Write a line to stderr (and mirror to log file if open). */
    static void err(const QString &line);

    /* Flush and close the log file. */
    static void shutdown();

private:
    BatchLogger() = delete; /* Pure static — no instances */
};

#endif // BATCHLOGGER_H
