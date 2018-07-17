#ifndef _MT19937AR_H
#define _MT19937AR_H

#ifdef __cplusplus
extern "C" {
#endif

#define MT19937AR_VERSION "0.20100917"

    void mt_init_genrand(unsigned long s);
    double mt_genrand_res53(void);

#ifdef __cplusplus
}
#endif

#endif /* !_MT19937AR_H */
