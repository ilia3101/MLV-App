/*! @file Bilinear.h

*  @brief Scaling tools
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
typedef unsigned char uint8_t;


// Fast image scalers that use bilinear interpolation
class CBilinearScaler : public CImageScaler
{
public:

	CBilinearScaler(IMemAlloc *pMemAlloc) :
		CImageScaler(pMemAlloc)
	{
	}

	~CBilinearScaler()
	{
		// Free the scratch buffers used by the scaling routines
		//FreeScratchMemory();
	}

protected:

#if 0
	// Allocate scratch memory used by the scaling routines
	bool AllocScratchMemory(int outputWidth, int inputHeight)
	{
		size_t horizontal_scale_size = outputWidth * inputHeight * 6;

		horizontalscale = (unsigned short *)Alloc(horizontal_scale_size);
		if (horizontalscale == NULL) return false;

		// Scratch memory has been successfully allocated
		return true;
	}

	// Free scratch memory used by the scaling routines
	void FreeScratchMemory()
	{
		if(horizontalscale) {
			Free((char *)horizontalscale);
			horizontalscale = NULL;
		}
	}
#endif
#if 0
	// Compute the scale factors for interpolating along a row
	void ComputeRowScaleFactors(short *scaleFactors, int inputWidth, int outputWidth);

	// Compute the scale factors for interpolating down a column
	int ComputeColumnScaleFactors(int row, int inputWidth, int outputWidth,
								  int renderFieldType, lanczosmix *lmY);

	// Compute the interpolation coefficients
	int LanczosCoeff(int inputsize, int outputsize, int line, lanczosmix *lm,
					 bool changefielddominance, bool interlaced, int lobes);
#endif

protected:

	// Scratch memory for use by the interpolator
	//float curve2pt2lin[4096];

};

class CBilinearScalerRGB32 : public CBilinearScaler
{
public:

	CBilinearScalerRGB32(IMemAlloc *pMemAlloc) :
		CBilinearScaler(pMemAlloc)
	{
	}

#if 0
	~CBilinearScalerRGB32()
	{
		//FreeScratchMemory();
	}
#endif

	void ScaleToBGRA(uint8_t *inputBuffer,
					 int inputWidth,
					 int inputHeight,
					 int inputPitch,
					 uint8_t *outputBuffer,
					 int outputWidth,
					 int outputHeight,
					 int outputPitch,
					 int flippedFlag,
					 int reorderFlag);

	//TODO: Remove references to QuickTime from the method names
	void ScaleToQuickTimeBGRA(uint8_t *inputBuffer,
							  int inputWidth,
							  int inputHeight,
							  int inputPitch,
							  uint8_t *outputBuffer,
							  int outputWidth,
							  int outputHeight,
							  int outputPitch)
	{
		ScaleToBGRA(inputBuffer, inputWidth, inputHeight, inputPitch,
					outputBuffer, outputWidth, outputHeight, outputPitch, true, false);
	}

	//TODO: Make sure that the BGRA scaling routine works for ARGB
	void ScaleToQuickTimeARGB(uint8_t *inputBuffer,
							  int inputWidth,
							  int inputHeight,
							  int inputPitch,
							  uint8_t *outputBuffer,
							  int outputWidth,
							  int outputHeight,
							  int outputPitch)
	{
		ScaleToBGRA(inputBuffer, inputWidth, inputHeight, inputPitch,
					outputBuffer, outputWidth, outputHeight, outputPitch, true, true);
	}

};
