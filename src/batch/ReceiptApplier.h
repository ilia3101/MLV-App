#ifndef RECEIPTAPPLIER_H
#define RECEIPTAPPLIER_H

/* C API types — mlvObject_t and processingObject_t are anonymous typedefs,
 * so we must include the full header (forward declaration won't work). */
#include "../../src/mlv_include.h"

class ReceiptSettings;

/* Applies parsed ReceiptSettings to the runtime mlvObject_t / processingObject_t
 * using the same C API calls the GUI's setSliders() triggers through its
 * signal chain.  This is the standalone batch-mode equivalent.
 *
 * Also provides a FINGERPRINT printer that reads back the actual runtime
 * state from the objects — proving settings reached the pipeline. */
class ReceiptApplier
{
public:
    /* Apply all CDNG-relevant receipt settings to the MLV pipeline.
     * receipt is non-const because the GUI logic mutates certain fields
     * (e.g. dualIsoForced, dualIso) during application. */
    static void applyToMlv(ReceiptSettings *receipt,
                            mlvObject_t *mlvObject,
                            processingObject_t *processingObject);

    /* Read back actual runtime state from mlvObject/processingObject and
     * print a structured [BATCH] FINGERPRINT line via BatchLogger.
     * Can be called even when no receipt was loaded (prints defaults). */
    static void printFingerprint(mlvObject_t *mlvObject,
                                 processingObject_t *processingObject);

private:
    ReceiptApplier() = delete; /* Pure static — no instances */
};

#endif // RECEIPTAPPLIER_H
