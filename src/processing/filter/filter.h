#ifndef __processing_filter__
#define __processing_filter__

#include "stdint.h"
#include "genann/genann.h"

typedef struct {
    double strength;
    int filter_option;
    genann * net_fj;
    genann * net_vis3;
    genann * net_p400;
    genann * net_sepia;
    genann * net_toyc;
    /* Strength lut */
    int32_t processed[65536];
    int32_t original[65536];
} filterObject_t;

filterObject_t * initFilterObject();

void applyFilterObject( filterObject_t * filter,
                        int width, int height,
                        uint16_t * image );

/* Set effect strength, 0.0-1.0 */
void filterObjectSetFilterStrength(filterObject_t * filter, double strength);

/* Choose ur filter */
#define FILTER_FILM_FJ 0 /* Film "FJ" */
#define FILTER_FILM_VIS3 1 /* Film "Vis3" */
#define FILTER_FILM_P400 2 /* Film "P400" */
#define FILTER_SEPIA 3 /*Sepia Tone*/
#define FILTER_TOYC 4 /*Toy Camera*/
void filterObjectSetFilter(filterObject_t * filter, int filterID);


void freeFilterObject(filterObject_t * filter);


#endif
