/*
 lj92.c
 (c) Andrew Baldwin 2014

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without __restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 of the Software, and to permit persons to whom the Software is furnished to do
 so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef struct _ljp* lj92;

enum LJ92_ERRORS {
    LJ92_ERROR_NONE = 0,
    LJ92_ERROR_CORRUPT = -1,
    LJ92_ERROR_NO_MEMORY = -2,
    LJ92_ERROR_BAD_HANDLE = -3,
    LJ92_ERROR_TOO_WIDE = -4
};
#define EXPORT_DLL __declspec(dllexport)


static int clz32(unsigned int x) {
    int n;
    if (x == 0) return 32;
    for (n = 0; ((x & 0x80000000) == 0); n++, x <<= 1)
        ;
    return n;
}


typedef struct _ljp {
    u8* data;
    u8* dataend;
    int datalen;
    int scanstart;
    int ix;
    int x;           // Width
    int y;           // Height
    int bits;        // Bit depth
	
    int writelen;    // Write rows this long
    int skiplen;     // Skip this many values after each row
    //u16* linearize;  // Linearization table
    int linlen;
    int sssshist[16];

    // Huffman table for each components
    u16* hufflut; 
    int huffbits; 

    // Parse state
    int cnt;
    u32 b;
    u16* image;
    u16* rowcache;
    u16* outrow[2];
 
} ljp;

static int find(ljp* self) {
    int ix = self->ix;
    u8* data = self->data;
    while (data[ix] != 0xFF && ix < (self->datalen - 1)) {
        ix += 1;
    }
    ix += 2;
    if (ix >= self->datalen) {
        // DPRINTF("idx = %d, datalen = %\d\n", ix, self->datalen);
        return -1;
    }
    self->ix = ix;
    // DPRINTF("ix = %d, data = %d\n", ix, data[ix - 1]);
    return data[ix - 1];
}

#define BEH(ptr) ((((int)(*&ptr)) << 8) | (*(&ptr + 1)))

static int parseHuff(ljp* self) {
    int ret = LJ92_ERROR_CORRUPT;
    u8* huffhead = &self->data[self->ix];  // xstruct.unpack('>HB16B',self.data[self.ix:self.ix+19])
    u8* bits = &huffhead[2];
    bits[0] = 0;  // Because table starts from 1
    int hufflen = BEH(huffhead[0]);
    if ((self->ix + hufflen) >= self->datalen) return ret;

    /* Calculate huffman direct lut */
    // How many bits in the table - find highest entry
    u8* huffvals = &self->data[self->ix + 19];
    int maxbits = 16;
    while (maxbits > 0) {
        if (bits[maxbits]) break;
        maxbits--;
    }
    self->huffbits = maxbits;
    /* Now fill the lut */
    u16* hufflut = (u16*)malloc((1 << maxbits) * sizeof(u16));

    
    // DPRINTF("maxbits = %d\n", maxbits);
    if (hufflut == NULL) return LJ92_ERROR_NO_MEMORY;
    self->hufflut = hufflut;
    int i = 0;
    int hv = 0;
    int rv = 0;
    int vl = 0;  // i
    int hcode;
    int bitsused = 1;
    while (i < 1 << maxbits) {
        if (bitsused > maxbits) {
            break;  // Done. Should never get here!
        }
        if (vl >= bits[bitsused]) {
            bitsused++;
            vl = 0;
            continue;
        }
        if (rv == 1 << (maxbits - bitsused)) {
            rv = 0;
            vl++;
            hv++;
            continue;
        }
        hcode = huffvals[hv];
        hufflut[i] = hcode << 8 | bitsused;
        // DPRINTF("%d %d %d\n",i,bitsused,hcode);
        i++;
        rv++;
    }
    ret = LJ92_ERROR_NONE;


    return ret;
}

static int parseSof3(ljp* self) {
    if (self->ix + 6 >= self->datalen) return LJ92_ERROR_CORRUPT;
    self->y = BEH(self->data[self->ix + 3]);
    self->x = BEH(self->data[self->ix + 5]);
    self->bits = self->data[self->ix + 2];
    self->ix += BEH(self->data[self->ix]);
    return LJ92_ERROR_NONE;
}

static int parseBlock(ljp* self, int marker) {
    self->ix += BEH(self->data[self->ix]);
    if (self->ix >= self->datalen) {
        return LJ92_ERROR_CORRUPT;
    }
    return LJ92_ERROR_NONE;
}


inline static int nextdiff(ljp* self, int Px) {
    u32 b = self->b;
    int cnt = self->cnt;
    int huffbits = self->huffbits;
    int ix = self->ix;
    int next;
    while (cnt < huffbits) {
        next = *(u16*)&self->data[ix];
        int one = next & 0xFF;
        int two = next >> 8;
        b = (b << 16) | (one << 8) | two;
        cnt += 16;
        ix += 2;
        if (one == 0xFF) {
            // DPRINTF("%x %x %x %x %d\n",one,two,b,b>>8,cnt);
            b >>= 8;
            cnt -= 8;
        } else if (two == 0xFF)
            ix++;
    }
    int index = b >> (cnt - huffbits);

    u16 ssssused = self->hufflut[index];
    int usedbits = ssssused & 0xFF;
    int t = ssssused >> 8;
    self->sssshist[t]++;
    cnt -= usedbits;
    int keepbitsmask = (1 << cnt) - 1;
    b &= keepbitsmask;
    while (cnt < t) {
        next = *(u16*)&self->data[ix];
        int one = next & 0xFF;
        int two = next >> 8;
        b = (b << 16) | (one << 8) | two;
        cnt += 16;
        ix += 2;
        if (one == 0xFF) {
            b >>= 8;
            cnt += -8;
        } else if (two == 0xFF)
            ix++;
    }
    cnt -= t;
    int diff = b >> cnt;
    int vt = 1 << (t - 1);
    if (diff < vt) {
        vt = (-1 << t) + 1;
        diff += vt;
    }
    keepbitsmask = (1 << cnt) - 1;
    self->b = b & keepbitsmask;
    self->cnt = cnt;
    self->ix = ix;

    return diff;
}



static int parseScan(ljp* self) {
    int ret = LJ92_ERROR_CORRUPT;
    memset(self->sssshist, 0, sizeof(self->sssshist));
    self->ix = self->scanstart;
    int compcount = self->data[self->ix + 2];
    int pred = self->data[self->ix + 3 + 2 * compcount];
    if (pred < 0 || pred > 7) return ret;
    //if (pred == 6) return parsePred6(self);  // Fast path
    
    self->ix += BEH(self->data[self->ix]);
    self->cnt = 0;
    self->b = 0;
    // int write = self->writelen;
    // Now need to decode huffman coded values
    // int c = 0;
    int pixels = self->y * self->x * 2;
    u16* out = self->image;
    u16* thisrow = self->outrow[0];
    u16* lastrow = self->outrow[1];

    // First pixel predicted from base value
    int diff;
    int Px = 0;
    int left = 0;

    int row, col, c;
    for (row = 0; row < self->y; row++) {
        for (col = 0; col < self->x; col++) {
            int colx = col * 2; //self->components;
            for (c = 0; c < 2; c++) {
                // DPRINTF("c = %d, col = %d, row = %d\n", c, col, row);
                
                if (col != 0) {
					Px = thisrow[(col - 1) * 2 + c];
				}else{
					if (row != 0) {
						Px = lastrow[c];
					}else{
						Px = 1 << (self->bits - 1);
					}					
				}

                diff = nextdiff(self,  Px);
                left = Px + diff;
				
                thisrow[colx + c] = left;
                out[colx + c]     = left;  // HACK
                
            }                          // c
        }                            // col

        u16* temprow = lastrow;
        lastrow = thisrow;
        thisrow = temprow;

        out += self->x * 2+ self->skiplen;
        // DPRINTF("out = %p, %p, diff = %lld\n", out, self->image, out -
    }  // row

    ret = LJ92_ERROR_NONE;
    return ret;
}

static int parseImage(ljp* self) {
    // DPRINTF("parseImage\n");
    int ret = LJ92_ERROR_NONE;
    while (1) {
        int nextMarker = find(self);
        //DPRINTF("marker = 0x%08x\n", nextMarker);
        if (nextMarker == 0xc4)
            ret = parseHuff(self);
        else if (nextMarker == 0xc3)
            ret = parseSof3(self);
        else if (nextMarker == 0xfe)  // Comment 
            ret = parseBlock(self, nextMarker);
        else if (nextMarker == 0xd9)  // End of image
            break;
        else if (nextMarker == 0xda) {
            self->scanstart = self->ix; 
            ret = LJ92_ERROR_NONE;
            break;
        } else if (nextMarker == -1) {
            ret = LJ92_ERROR_CORRUPT; 
            break;
        } else
           ret = parseBlock(self, nextMarker); 
        if (ret != LJ92_ERROR_NONE) break;
        
    }
    
    return ret;
}

static int findSoI(ljp* self) {
    int ret = LJ92_ERROR_CORRUPT;
    if (find(self) == 0xd8) {
        ret = parseImage(self);
    } else {
        //DPRINTF("findSoI: corrupt\n");
    }
    return ret;
}

static void free_memory(ljp* self) {

        free(self->hufflut);
        self->hufflut = NULL;

    free(self->rowcache);
    self->rowcache = NULL;
}

int lj92_open(lj92* lj, const uint8_t* data, int datalen, int* width, int* height, int* bitdepth) {
    ljp* self = (ljp*)calloc(sizeof(ljp), 1);
    if (self == NULL) return LJ92_ERROR_NO_MEMORY;

    self->data = (u8*)data;
    self->dataend = self->data + datalen;
    self->datalen = datalen;


    int ret = findSoI(self);

    if (ret == LJ92_ERROR_NONE) {
        u16* rowcache = (u16*)calloc(self->x * 4, sizeof(u16));
        if (rowcache == NULL)
            ret = LJ92_ERROR_NO_MEMORY;
        else {
            self->rowcache = rowcache;
            self->outrow[0] = rowcache;
            self->outrow[1] = &rowcache[self->x];
        }
    }

    if (ret != LJ92_ERROR_NONE) {  // Failed, clean up
        *lj = NULL;
        free_memory(self);
        free(self);
    } else {
        *width = self->x;
        *height = self->y;
        *bitdepth = self->bits;
        //*components = self->components;
        *lj = self;
    }
    return ret;
}

int lj92_decode(lj92 lj, uint16_t* target, int writeLength, int skipLength) { //uint16_t* linearize, int linearizeLength) {
    int ret = LJ92_ERROR_NONE;
    ljp* self = lj;
    if (self == NULL) return LJ92_ERROR_BAD_HANDLE;
    self->image = target;
    self->writelen = writeLength;
    self->skiplen = skipLength;
    //self->linearize = linearize;
    //self->linlen = linearizeLength;
    ret = parseScan(self);
    return ret;
}

void lj92_close(lj92 lj) {
    ljp* self = lj;
    if (self != NULL) free_memory(self);
    free(self);
}

/* Encoder implementation */

typedef struct _lje {
    uint16_t* image;
    int width;
    int height;
    int bitdepth;
    int components;
    int readLength;
    int skipLength;
    uint16_t* delinearize;
    int delinearizeLength;
    uint8_t* encoded;
    int encodedWritten;
    int encodedLength;
    int hist[17];  // SSSS frequency histogram
    int bits[17];
    int huffval[17];
    u16 huffenc[17];
    u16 huffbits[17];
    int huffsym[17];
} lje;