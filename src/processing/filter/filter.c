#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "filter.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))
#define LIMIT16(X) MAX(MIN(X, 65535), 0)

/* Nothing to see here */
#include "film.h"

filterObject_t * initFilterObject()
{
    filterObject_t * filter = malloc(sizeof(filterObject_t));

    /* 
     * """""""""Diclaimer"""""""""
     * These networks are *definitely* *not* imitating any well known plugin 
     * that claims to convert anything to film or anything like that.
     */

    /* FJ preset */
    char filmprofile_fj[] = FILM_FJ;
    filter->net_fj = genann_read(filmprofile_fj);

    /* Kodak Vision 3 preset */
    char filmprofile_vis3[] = FILM_VIS3;
    filter->net_vis3 = genann_read(filmprofile_vis3);

    /* Kodak Portra 400 preset */
    char filmprofile_p400[] = FILM_P400;
    filter->net_p400 = genann_read(filmprofile_p400);

    /* Kodak Ektar 100 */
    char filmprofile_kodak_ektar[] = FILM_KODAK_EKTAR;
    filter->net_kodak_ektar = genann_read(filmprofile_kodak_ektar);

    /* Toy Camera */
    char filmprofile_toyc[] = FILM_TOYC;
    filter->net_toyc = genann_read(filmprofile_toyc);

    /* Sepia Tone */
    char filmprofile_sepia[] = FILM_SEPIA;
    filter->net_sepia = genann_read(filmprofile_sepia);

    filterObjectSetFilterStrength(filter, 1.0);

    return filter;
}

void applyFilterObject( filterObject_t * filter,
                        int width, int height,
                        uint16_t * image )
{
    if (filter->strength < 0.01) return;

    double pixel[3];

    uint16_t * end = image + (width * height * 3);

    /* We need a copy or it gets messed up on many threads */
    genann * net;
    if (filter->filter_option == FILTER_FILM_FJ)
        net = genann_copy(filter->net_fj);
    else if (filter->filter_option == FILTER_FILM_VIS3)
        net = genann_copy(filter->net_vis3);
    else if (filter->filter_option == FILTER_FILM_P400)
        net = genann_copy(filter->net_p400);
    else if (filter->filter_option == FILTER_FILM_E100)
        net = genann_copy(filter->net_kodak_ektar);
    else if (filter->filter_option == FILTER_TOYC)
        net = genann_copy(filter->net_toyc);
    else if (filter->filter_option == FILTER_SEPIA)
        net = genann_copy(filter->net_sepia);

    for (uint16_t * pix = image; pix < end; pix += 3)
    {
        pixel[0] = ((double)pix[0])/65535.0;
        pixel[1] = ((double)pix[1])/65535.0;
        pixel[2] = ((double)pix[2])/65535.0;
        const double * filtered = genann_run(net, pixel);
        pix[0] = LIMIT16(filter->processed[(size_t)(filtered[0]*65535.0)] + filter->original[pix[0]]);
        pix[1] = LIMIT16(filter->processed[(size_t)(filtered[1]*65535.0)] + filter->original[pix[1]]);
        pix[2] = LIMIT16(filter->processed[(size_t)(filtered[2]*65535.0)] + filter->original[pix[2]]);
    }

    genann_free(net);
}

/* Set effect strength, 0.0-1.0 */
void filterObjectSetFilterStrength(filterObject_t * filter, double strength)
{
    filter->strength = strength;
    double istrength = 1.0 - strength;
    for (int i = 0; i < 65536; ++i)
    {
        filter->processed[i] = (int32_t)(strength * (double)i);
        filter->original[i] = (int32_t)(istrength * (double)i);
    }
}

void freeFilterObject(filterObject_t * filter)
{
    genann_free(filter->net_fj);
    free(filter);
}

void filterObjectSetFilter(filterObject_t * filter, int filterID)
{
    filter->filter_option = filterID;
}
