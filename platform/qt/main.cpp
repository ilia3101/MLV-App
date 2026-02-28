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
#include "../../src/batch/BatchLogger.h"

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
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("MLVApp batch mode — headless Cinema DNG export"));

    /* Do NOT call parser.addHelpOption() — on Windows/Qt it triggers a
     * QMessageBox.  We handle -h/--help manually below. */
    QCommandLineOption helpOpt(
        QStringList() << QStringLiteral("h") << QStringLiteral("help"),
        QStringLiteral("Show this help text and exit."));
    parser.addOption(helpOpt);

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

    QCommandLineOption receiptOpt(
        QStringList() << QStringLiteral("r") << QStringLiteral("receipt"),
        QStringLiteral("Apply .marxml receipt settings to export."),
        QStringLiteral("file"));
    parser.addOption(receiptOpt);

    parser.process(app);

    /* Init log file mirror as early as possible so that --help and
     * missing-arg errors are captured in the log file too. */
    QString logPath = parser.value(logOpt);
    BatchLogger::init(logPath);

    /* --help: print to stdout (+ log), exit 0.  No QMessageBox. */
    if( parser.isSet(helpOpt) )
    {
        BatchLogger::out(parser.helpText() + QStringLiteral("\n"));
        BatchLogger::shutdown();
        return 0;
    }

    /* --input and --output are required in batch mode */
    if( !parser.isSet(inputOpt) || !parser.isSet(outputOpt) )
    {
        BatchLogger::err(QStringLiteral("[BATCH] ERROR: --input and --output are required.\n\n"));
        BatchLogger::err(parser.helpText() + QStringLiteral("\n"));
        BatchLogger::shutdown();
        return 2;
    }

    QString inputPath   = parser.value(inputOpt);
    QString outputPath  = parser.value(outputOpt);
    bool skipErrors     = parser.isSet(skipErrorsOpt);
    bool verbose        = parser.isSet(verboseOpt);
    QString receiptPath = parser.value(receiptOpt);

    /* Store in BatchContext for global access */
    BatchContext::setBatchMode(true);
    BatchContext::setSkipErrors(skipErrors);
    BatchContext::setVerbose(verbose);
    BatchContext::setLogPath(logPath);
    BatchContext::setReceiptPath(receiptPath);

    int exitCode = BatchRunner::run(inputPath, outputPath);
    BatchLogger::shutdown();
    return exitCode;
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
