#ifndef BATCHRUNNER_H
#define BATCHRUNNER_H

#include <QString>
#include "BatchTypes.h"

/* Orchestrates headless batch CDNG export.
 * Called from main.cpp after CLI args are parsed.
 * Opens each MLV, calls MainWindow::exportCdngSequence(), logs results. */
class BatchRunner
{
public:
    /* Run the batch export.
     * inputPath: single .mlv file or folder of .mlv files
     * outputPath: root output directory
     * Returns process exit code (see CLAUDE.md exit code table). */
    static int run(const QString &inputPath, const QString &outputPath);

private:
    BatchRunner() = delete; /* Pure static */

    /* Export a single MLV file.  Returns ProcessResult. */
    static ProcessResult exportSingleFile(const QString &mlvPath,
                                          const QString &outputRoot);
};

#endif // BATCHRUNNER_H
