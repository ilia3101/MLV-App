/*!
 * \file main.cpp
 * \author masc4ii
 * \copyright 2017
 * \brief The main... the start of the horror
 */

#include "MainWindow.h"
#include "MyApplication.h"
#include "../../src/batch/BatchContext.h"
#include "../../src/batch/BatchRunner.h"

#include <QCommandLineParser>
#include <QTextStream>
#include <cstring>

/* Raw argv scan for "--batch" BEFORE QApplication is constructed.
 * QCommandLineParser needs QApplication, but we need to know the
 * mode early to skip MainWindow creation entirely in batch mode. */
static bool hasBatchFlag(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--batch") == 0) return true;
    }
    return false;
}

static int runBatch(QCoreApplication &app)
{
    QTextStream out(stdout);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("MLVApp batch mode — headless Cinema DNG export"));
    parser.addHelpOption();

    /* --batch is already consumed by hasBatchFlag(); add it here so
     * QCommandLineParser doesn't complain about an unknown option. */
    QCommandLineOption batchOpt(
        QStringLiteral("batch"),
        QStringLiteral("Run in headless batch export mode."));
    parser.addOption(batchOpt);

    QCommandLineOption inputOpt(
        QStringList() << QStringLiteral("i") << QStringLiteral("input"),
        QStringLiteral("Input MLV file or folder path."),
        QStringLiteral("path"));
    parser.addOption(inputOpt);

    QCommandLineOption outputOpt(
        QStringList() << QStringLiteral("o") << QStringLiteral("output"),
        QStringLiteral("Output directory for exported DNG sequences."),
        QStringLiteral("path"));
    parser.addOption(outputOpt);

    QCommandLineOption skipErrorsOpt(
        QStringLiteral("skip-errors"),
        QStringLiteral("Skip corrupt frames instead of aborting."));
    parser.addOption(skipErrorsOpt);

    QCommandLineOption logOpt(
        QStringLiteral("log"),
        QStringLiteral("Mirror log output to file."),
        QStringLiteral("file"));
    parser.addOption(logOpt);

    QCommandLineOption verboseOpt(
        QStringLiteral("verbose"),
        QStringLiteral("Enable detailed per-frame logging."));
    parser.addOption(verboseOpt);

    parser.process(app);

    /* --input and --output are required in batch mode */
    if (!parser.isSet(inputOpt) || !parser.isSet(outputOpt))
    {
        out << QStringLiteral("[BATCH] ERROR: --input and --output are required.\n");
        out.flush();
        parser.showHelp(2); /* exits with code 2 (bad arguments) */
    }

    QString inputPath  = parser.value(inputOpt);
    QString outputPath = parser.value(outputOpt);
    bool skipErrors    = parser.isSet(skipErrorsOpt);
    QString logPath    = parser.value(logOpt);
    bool verbose       = parser.isSet(verboseOpt);

    /* Store in BatchContext for global access */
    BatchContext::setBatchMode(true);
    BatchContext::setSkipErrors(skipErrors);
    BatchContext::setVerbose(verbose);
    BatchContext::setLogPath(logPath);

    return BatchRunner::run(inputPath, outputPath);
}

int main(int argc, char *argv[])
{
    bool batch = hasBatchFlag(argc, argv);

    MyApplication a(argc, argv);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    a.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#ifdef Q_OS_WIN
    a.setAttribute(Qt::AA_Use96Dpi);
#endif

    if (batch)
    {
        /* Batch mode — no GUI window, but QApplication stays alive
         * because internal export code may touch widgets/fonts. */
        a.setQuitOnLastWindowClosed(false);
        return runBatch(a);
    }

    /* Normal GUI mode — unchanged */
    MainWindow w(argc, argv);
    w.show();

    return a.exec();
}
