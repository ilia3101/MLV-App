#include "BatchContext.h"

/* Static member definitions */
bool BatchContext::s_batchMode = false;
bool BatchContext::s_skipErrors = false;
bool BatchContext::s_verbose = false;
bool BatchContext::s_useDefaultReceipt = false;
bool BatchContext::s_resumeEnabled = false;
QString BatchContext::s_logPath;
QString BatchContext::s_receiptPath;

void BatchContext::setBatchMode(bool enabled) { s_batchMode = enabled; }
bool BatchContext::isBatchMode() { return s_batchMode; }

void BatchContext::setSkipErrors(bool skip) { s_skipErrors = skip; }
bool BatchContext::skipErrors() { return s_skipErrors; }

void BatchContext::setVerbose(bool verbose) { s_verbose = verbose; }
bool BatchContext::isVerbose() { return s_verbose; }

void BatchContext::setLogPath(const QString &path) { s_logPath = path; }
QString BatchContext::logPath() { return s_logPath; }

void BatchContext::setReceiptPath(const QString &path) { s_receiptPath = path; }
QString BatchContext::receiptPath() { return s_receiptPath; }

void BatchContext::setUseDefaultReceipt(bool use) { s_useDefaultReceipt = use; }
bool BatchContext::useDefaultReceipt() { return s_useDefaultReceipt; }

void BatchContext::setResumeEnabled(bool enabled) { s_resumeEnabled = enabled; }
bool BatchContext::resumeEnabled() { return s_resumeEnabled; }
