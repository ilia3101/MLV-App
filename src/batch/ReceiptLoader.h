#ifndef RECEIPTLOADER_H
#define RECEIPTLOADER_H

#include <QString>

class QXmlStreamReader;
class ReceiptSettings;

/* Standalone .marxml receipt parser for batch mode.
 * Extracts XML parsing logic from MainWindow::readXmlElementsFromFile()
 * without requiring a MainWindow instance.
 *
 * Phase 6A: parse and print only.
 * Phase 6B (future): apply parsed settings to mlvObject_t. */
class ReceiptLoader
{
public:
    /* Parse a .marxml receipt file into a ReceiptSettings object.
     * Returns true on success.  On failure, returns false and sets
     * errorMsg to a human-readable description. */
    static bool loadFromFile(const QString &receiptPath,
                             ReceiptSettings *receipt,
                             QString *errorMsg);

    /* Print CDNG-relevant settings from the receipt via BatchLogger.
     * Only prints fields that affect raw/CDNG export — skips color
     * grading, LUT, filter, vignette, sharpening, CA, vidstab, etc. */
    static void printCdngSettings(const ReceiptSettings *receipt);

private:
    ReceiptLoader() = delete; /* Pure static */

    /* Internal XML element parser — copied from
     * MainWindow::readXmlElementsFromFile() to avoid MainWindow
     * dependency.  Parses ALL tags (not just CDNG-relevant ones)
     * so that the ReceiptSettings object is fully populated. */
    static void parseXmlElements(QXmlStreamReader *Rxml,
                                 ReceiptSettings *receipt,
                                 int version);
};

#endif // RECEIPTLOADER_H
