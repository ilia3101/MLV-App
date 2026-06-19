/*! @file stats.c

*  @brief 
*
*  @version 1.0.0
*
*  (C) Copyright 2017 GoPro Inc (http://gopro.com/).
*
*  Licensed under either:
*  - Apache License, Version 2.0, http://www.apache.org/licenses/LICENSE-2.0  
*  - MIT license, http://opensource.org/licenses/MIT
*  at your option.
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*/

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "config.h"
#include "stats.h"


void DumpString(char *txt)
{
	FILE *fp;
	int err = 0;

#ifdef _WIN32
	err = fopen_s(&fp, "dumpstring.txt", "a");
#else
	fp = fopen("dumpstring.txt", "a");
#endif

	if (err == 0 && fp)
	{
		fprintf(fp, "%s", txt);
		fclose(fp);
	}
}


#if (_STATS || 0)

#define MAX_ZEROS	360
static int value_then_zero_dist[13][65] = { 0 }; 
static int zero_dist_count[MAX_ZEROS+1] = { 0 }; 
static int zero_dist[MAX_ZEROS+1] = { 0 }; 
static int value_dist[17*3][259] = { 0 };
static int overflow_neg_peek[17*3] = { 0 };
static int overflow_pos_peek[17*3] = { 0 };
static int overflow_neg[17*3] = { 0 };
static int overflow_pos[17*3] = { 0 };
static int value_dist_old[256] = { 0 };
/*static int value_dist_dup[256] = { 0 };
static int value_dist_dup2[256] = { 0 };
static int value_dist_dup3[256] = { 0 };
static int value_dist_dup4[256] = { 0 };
static int value_dist_dup5[256] = { 0 };
*/static int once = 0;

#define STATS_FILENAME_FREQ "c:/cedoc/Logfiles/newstats.txt"
#define STATS_FILENAME2 "c:/cedoc/Logfiles/subband-energy.txt"
#define STATS_FILENAME3 "c:/cedoc/Logfiles/averages.txt"
#define STATS_FILENAME4 "c:/cedoc/Logfiles/memory.txt"

double subband_energy = 0.0;
double subband_energy_no_quant = 0.0;
int newquant = 0;

#define DO_ENERGY		0
#define DO_FREQ			1
#define DO_AVERAGES		0
#define DO_MEMORY		0

void DumpText(char *txt, int hex)
{
	FILE *fp;
	
	fp = fopen("C:\\Cedoc\\Logfiles\\dump.txt","a");
	fprintf(fp, txt, hex);

	fclose(fp);
}


void DumpData(int a, int b, int c)
{
	static int count = 0;
	static FILE *fp = NULL;

	if(fp == NULL)
		if(count == 0)
			fp = fopen("C:\\Cedoc\\Logfiles\\dumpdata.txt","w");
		else
			fp = fopen("C:\\Cedoc\\Logfiles\\dumpdata.txt","a");

	fprintf(fp, "in = %d, out = %d, %d bits\n", a,b,c);
	count++;

	if(count == 1000)
	{
		fclose(fp);
		fp = NULL;
		count = 1;
	}
}


void SetQuantStats(int quantization)
{
	newquant = quantization;
}

static int currband=0;
void NewSubBand(int width, int height, int first, int bits, int overhead )
{
	if(first==1)
	{
		if(width > 89/*50 for SD*/)//Y
			currband = 0; 
		else
			currband++;
	}
	else
		currband++;

	if(currband >= 17*3)
		currband = 0;

#if DO_ENERGY
	{
		static FILE *fp = NULL;
		static int count = 0;
		static int bandbits[17*3] = {0};
		static int bandpixels[17*3] = {0};
		

		if(fp == NULL)
			if(count == 0)
				fp = fopen(STATS_FILENAME2,"w");
			else
				fp = fopen(STATS_FILENAME2,"a");

		if (fp)
		{
			if(first==1)
			{
				fprintf(fp,"\n");
			}
				
	//		fprintf(fp,"%3d,%3d:energy = %4.1f (average %2.3f),  pre-quant event = %6.1f (average %4.3f)\n", width, height, (float)sqrt(subband_energy), (float)sqrt(subband_energy/(double)(width*height)), (float)sqrt(subband_energy_no_quant), (float)sqrt(subband_energy_no_quant/(double)(width*height)) );
	/*		fprintf(fp,"%3d,%3d:energy = %10.1f (average %5.1f), \
	pre-quant event = %10.1f (average %5.1f), \
	total bits = %d, overhead bits = %d\n", 

						width, height, (float)(subband_energy), (float)(subband_energy/(double)(width*height)), 
						(float)(subband_energy_no_quant), (float)(subband_energy_no_quant/(double)(width*height)), 
						bits, overhead );
						*/
			fprintf(fp,"%3d,%3d: band %d:total bits = %6d,   overhead bits = %3d,  bits per pixel = %3.3f\n", width, height, currband, bits, overhead, ((float)(bits/*+overhead*/))/((float)(width * height)));

			bandbits[currband] += bits;
			bandpixels[currband] += (width * height);
		}
		subband_energy = 0.0;
		subband_energy_no_quant = 0.0;

		count++;

		if(count >= 17*3*15-1)
		{
			int i;
			fprintf(fp,"\n");

			for(i=0; i<17*3; i++)
			{
				if(bandpixels[i])
					fprintf(fp,"  band %d: bits = %10d,  bits per pixel = %3.3f\n", i, bandbits[i], ((float)bandbits[i])/((float)bandpixels[i]));
				else
					fprintf(fp,"  band %d: bits = %10d,  bits per pixel = %s\n", i, bandbits[i], "infinite");

				if(i==16 || i==33) fprintf(fp,"\n");
			}
			fprintf(fp,"\n");
			fprintf(fp,"\n");

			fclose(fp);
			fp = NULL;
			count = 1;
		}
	}
#endif 
}

/*
void ReadStats(int stats)
{
#if DO_FREQ
	FILE *fp;
	short value,*ptr;
	int i,x,y,count,offset;
	
	if(once == 0)
	{
		// Read the old statistics
		fp = fopen(STATS_FILENAME_FREQ, "r");
		if (fp)
		{
			int val,c;
			for(i=1;i<=MAX_ZEROS;i++)
			{
				fscanf(fp,"%4d,%d\n", &c, &val);
				zero_dist[i] = val;
			}

			for(i=0;i<256;i++)
			{
				fscanf(fp,"%4d,%d\n", &c, &val);
				value_dist[i] = val;
			}

			fclose(fp);
		}
	}
#endif
}
*/

void CountValues(int stats, int value, int num)
{
#if DO_FREQ	
	static int last_value=0,last_value2=0,last_value3=0,last_value4=0,last_value5=0,last_value6=0;
	int signed_value = value, oldvalue = value;
	// Update the histogram of coefficient values

	subband_energy += (double)abs(value/* * value*/);
	subband_energy_no_quant += (double)abs(value * newquant/* * value * newquant*/);

	oldvalue = abs(value);
	if(oldvalue > 255) oldvalue = 255;
	value_dist_old[oldvalue]+=num;

	if(oldvalue > 128)
		oldvalue++;
/*
	if(value > 127) 
	{
		int over = value - 127;
		overflow_pos[currband] += over;
		if(overflow_pos_peek[currband] < over)
			overflow_pos_peek[currband] = over;
			
		value = 256;
	}
	if(value < -128) 
	{
		int over = abs(value) - 128;
		overflow_neg[currband] += over;
		if(overflow_neg_peek[currband] < over)
			overflow_neg_peek[currband] = over;
			
		value = 257;
	}
	if(value < 0) value += 256;

	
	if(oldvalue > 257)
		oldvalue = 258;

	//assert(0 <= value && value < sizeof(value_dist)/sizeof(value_dist[0]));
	if (0 <= value )
	{
		value_dist[currband][value]+=num;
	}
	*/

/*	if(value == 0)
	{
		if(last_value >= -6 && last_value <= 6 && last_value != 0)
		{
			int run = num;
			switch(abs(last_value))
			{
				case 1: // 5 bit of runs
					if(run > 64) run = 64;
					break;
				case 2: // 3 bit of runs
					if(run > 8) run = 8;
					break;
				case 3: // 2 bit of runs
				case 4: // 2 bit of runs
					if(run > 4) run = 4;
					break;
				case 5: // 1 bit of runs
				case 6: // 1 bit of runs
					if(run > 1) run = 1;
					break;
			}
			value_then_zero_dist[last_value+6][run]++;	
		}
	}
	*/

/*	if(signed_value != last_value && last_value != 0)
	{
		value = last_value;
		if (value < 0) value += 256;
		
		if(abs(last_value) <= 1 && last_value == last_value2 && last_value == last_value3 && last_value == last_value4 && last_value == last_value5 && last_value == last_value6)
			value_dist_dup5[value]++;
		else if(abs(last_value) <= 1 && last_value == last_value2 && last_value == last_value3 && last_value == last_value4 && last_value == last_value5)
			value_dist_dup4[value]++;
		else if(abs(last_value) <= 2 && last_value == last_value2 && last_value == last_value3 && last_value == last_value4)
			value_dist_dup3[value]++;
		else if(abs(last_value) <= 4 && last_value == last_value2 && last_value == last_value3)
			value_dist_dup2[value]++;
		else if(abs(last_value) <= 6 && last_value == last_value2)
			value_dist_dup[value]++;
	}
	last_value6 = last_value5;
	last_value5 = last_value4;
	last_value4 = last_value3;
	last_value3 = last_value2;
	last_value2 = last_value;
*/	last_value = signed_value;

#endif
}

void CountRuns(int stats, int count)
{
#if DO_FREQ	
	// Update the histogram of the length of runs of zeros
	int i,diff,limit = 10,input = count,output = count;
	static int once = 0;

	if(once == 0)
	{
		int val = 1;
		for(i=1; i<20; i++)
		{
			zero_dist_count[i] = val;
			val ++;
		}

		val = 20;
		for(i=20; i<100; i++)
		{
			zero_dist_count[i] = val;
			val += 4;
		}

		val = 100;
		for(i=40; i<66; i++)
		{
			zero_dist_count[i] = val;
			val += 10;
		}

		val = 360;
		for(i=66; i<93; i++)
		{
			zero_dist_count[i] = val;
			val += 360;
		}

		val = 10080;
		for(i=93; i<=MAX_ZEROS; i++)
		{
			zero_dist_count[i] = val;
			val += 2000;
		}
		once = 1;
	}


	for(i=0;i<MAX_ZEROS;i++)
	{
		if(zero_dist_count[i] >= count)
			break;
	}

	if(zero_dist_count[i] > count)
		i--;


	zero_dist[i]++;
	count -= zero_dist_count[i];

	if(count)
	{
		CountRuns(stats, count);
	}

		
/*	if(count >= 20 && count < 100) // 20 to 40 (values 20 - 100)
	{
		count -= 20;
		output -= 20;
		count /= 4;
		output /= 4;
		output *= 4;
		count += 20;
		output += 20;
	}
	else if(count >= 100 && count < 360) //40 - 66  (values 20 - 300)
	{
		count -= 100;
		output -= 100;
		count /= 10;
		output /= 10;
		output *= 10;
		count += 40;
		output += 100;

	}
	else if(count >= 360) //66 -> (values 360+)
	{
		count -= 360;
		output -= 360;
		count /= 360;
		output /= 360;
		output *= 360;
		count += 66;
		output += 360;
	}*/




//	assert(0 <= count && count < sizeof(zero_dist)/sizeof(zero_dist[0]));
/*	if (0 <= count )
	{
		zero_dist[count]++;
		zero_dist_count[count] = output;

		if(count > MAX_ZEROS)
			count++;
	}


	diff = input - output;
	if(diff)
		CountRuns(stats, diff);
*/
#endif
}

void UpdateStats(int stats)
{
#if DO_FREQ	
	int i,j;
	int limit,input,output,band;
	char bandtxt[16][8] = {
		"160x90",
		"160x90",
		"160x90",
		"320x180",
		"320x180",
		"320x180",
		"320x180",
		"320x180",
		"320x180",
		"320x180",
		"640x360",
		"640x360",
		"640x360",
		"640x360",
		"640x360",
		"640x360"
	};
	char bandtxt2[16][4] = {
		"HL ",
		"LH ",
		"HH ",
		"HL ",
		"LH ",
		"HH ",
		"LLT",
		"HLT",
		"LHT",
		"HHT",
		"HL ",
		"LH ",
		"HH ",
		"HL ",
		"LH ",
		"HH "
	};
	FILE *fp = fopen(STATS_FILENAME_FREQ,"w");

	if (fp)
	{

		fprintf(fp, "\nZero Run Dist\n");
		for(i=1;i<=MAX_ZEROS;i++)
			fprintf(fp, "%6d,%d\n", zero_dist_count[i], zero_dist[i]);
		for(i=0;i<256;i++)
			fprintf(fp, "%4d,%d\n", i, value_dist_old[i]);

#if 0 
		fprintf(fp, "\nValue Dist\n");
		
		fprintf(fp,"Res ->\t");
		for(band=1; band<17; band++)
			fprintf(fp, "%s\t", bandtxt[band-1]);
		fprintf(fp, "\n");
		
		fprintf(fp,"Type ->\t");
		for(band=1; band<17; band++)
			fprintf(fp, "%s\t", bandtxt2[band-1]);
		fprintf(fp, "\n");
		
		fprintf(fp,"Value\t");
		for(band=1; band<17; band++)
			fprintf(fp, "%d\t", band);
		fprintf(fp, "\n");
		
				
		for(i=0;i<258;i++)
		{
			if(i<128)
				fprintf(fp, "%4d\t", i);
			else if(i==256)
				fprintf(fp, "over+\t");
			else if(i==257)
				fprintf(fp, "over-\t");
			else
				fprintf(fp, "%4d\t", i-256);
				
			for(band=0; band<17*3; band++)
				fprintf(fp, "%d\t", value_dist[band][i]);
			fprintf(fp, "\n");
		}


		fprintf(fp, "peek+\t");
		for(band=0; band<17*3; band++)
				fprintf(fp, "%d\t", overflow_pos_peek[band]);
		fprintf(fp, "\n");
		fprintf(fp, "peek-\t");
		for(band=0; band<17*3; band++)
				fprintf(fp, "%d\t", overflow_neg_peek[band]);
		fprintf(fp, "\n");
		fprintf(fp, "avg+\t");
		for(band=0; band<17*3; band++)
			if(overflow_pos[band])
				fprintf(fp, "%d\t", overflow_pos[band]/value_dist[band][256]);
			else
				fprintf(fp, "0\t");
		fprintf(fp, "\n");
		fprintf(fp, "avg-\t");
		for(band=0; band<17*3; band++)
			if(overflow_neg[band])
				fprintf(fp, "%d\t", overflow_neg[band]/value_dist[band][257]);
			else
				fprintf(fp, "0\t");
		fprintf(fp, "\n");
		
	//	for(i=0;i<256;i++)
	//		fprintf(fp, "%4d,%d,%d,%d,%d,%d\n", i, value_dist_dup[i], value_dist_dup2[i], value_dist_dup3[i], value_dist_dup4[i], value_dist_dup5[i]);

	/*	for(i=0;i<13;i++)
		{	
			for(j=0;j<=64;j++)
			{
				if(i!=6)
					fprintf(fp, "after %2d: zero run = %3d freq = %d\n", i-6, j, value_then_zero_dist[i][j]);
			}
		}
	*/		
#endif
		fclose(fp);
	}
#endif
}


void StatsAverageLevels(IMAGE *frame)
{
#if DO_AVERAGE	
	static int once = 0;
	PIXEL *ptr,val;
	FILE *fp;
	int average;
	
	if(ptr = frame->band[0])
	{
		int w=frame->width;
		int h=frame->height;
		int p=frame->pitch;
		int x,y,samples=0,min=256,max=-256;
		long long some = 0;

		for(y=0; y<h/2;y++)
		{
			for(x=0;x<w;x+=4)
			{
				val = *ptr++;
				ptr+=3;
				if(val<min)
					min = val;
				if(val>max)
					max = val;
				some += val;
				samples++;
			}
			ptr += (p-w);
		}
		
		if(once == 0)
			fp = fopen(STATS_FILENAME3,"w");
		else
			fp = fopen(STATS_FILENAME3,"a");

		average = some/samples;

		if (fp)
		{
			fprintf(fp, "average = %4d, min = %4d, max = %4d\n", average,min,max);
			fclose(fp);
		}		
	}
#endif
}


void StatsMemoryAlloc(int size, char *func)
{
#if DO_MEMORY	
	static int total = 0;
	FILE *fp;

	if(total == 0)
		fp = fopen(STATS_FILENAME4,"w");
	else
		fp = fopen(STATS_FILENAME4,"a");
		

	total += size;
	if (fp)
	{
		fprintf(fp, "alloc = %8d, func = %s, total = %8d\n", size,func, total);
		fclose(fp);
	}	
#endif
}

#endif
