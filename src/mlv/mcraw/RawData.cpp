/*
 * Copyright 2023 MotionCam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Changed:
// - Add c API function mr_decode_video_frame(...)

#include <cstdint>
#include <vector>
#include <cstring>
#include <immintrin.h>

namespace motioncam {
    namespace raw {

    namespace {
    const int ENCODING_BLOCK = 64;
    const int HEADER_LENGTH = 2;
    const int METADATA_OFFSET = 16;

        constexpr int ENCODING_BLOCK_LENGTH[] = {
                0,
                8,
                16,
                24,
                32,
                40,
                48,
                64,
                64,
                80,
                80,
                128,
                128,
                128,
                128,
                128,
                128
        };

// Original push request
// https://github.com/mirsadm/motioncam-decoder/commit/15fd711525e0701205b3805b4777c63b6184782d?diff=split&w=0
// Code from hanatos

#if 1
            struct UInt16x8
            {
                __m128i d;
                UInt16x8(
                        const uint16_t p0,
                        const uint16_t p1,
                        const uint16_t p2,
                        const uint16_t p3,
                        const uint16_t p4,
                        const uint16_t p5,
                        const uint16_t p6,
                        const uint16_t p7)
                {
                    d = _mm_set_epi16(p7, p6, p5, p4, p3, p2, p1, p0);
                }

                UInt16x8(const __m128i other)
                {
                    d = other;
                }

                UInt16x8(const UInt16x8& src)
                {
                    d = src.d;
                }

                UInt16x8(const uint16_t val)
                {
                    d = _mm_set1_epi16(val);
                }

                inline
                UInt16x8 operator&(const UInt16x8& rhs) const {
                    return UInt16x8(_mm_and_si128(d, rhs.d));
                }

                inline
                UInt16x8 operator|(const UInt16x8& rhs) const {
                    return UInt16x8(_mm_or_si128(d, rhs.d));
                }

                inline
                UInt16x8 operator<<(const int n) const {
                    return UInt16x8(_mm_slli_epi16(d, n));
                }

                inline
                UInt16x8 operator>>(const int n) const {
                    return UInt16x8(_mm_srli_epi16(d, n));
                }
            };
            inline
            UInt16x8 Load(const uint8_t* src) {
                // return UInt16x8(_mm_loadu_si128((__m128i *)src)); // this would be wrong, we expand 8 bit to 16 here
                return UInt16x8(
                        src[0],
                        src[1],
                        src[2],
                        src[3],
                        src[4],
                        src[5],
                        src[6],
                        src[7]);
            }

            inline
            void Store(uint16_t* dst, const UInt16x8& src) {
                _mm_storeu_si128((__m128i *)dst, src.d);
            }
#else

    struct UInt16x8 {
        const uint16_t d[8];
        
        UInt16x8(
            const uint16_t p0,
            const uint16_t p1,
            const uint16_t p2,
            const uint16_t p3,
            const uint16_t p4,
            const uint16_t p5,
            const uint16_t p6,
            const uint16_t p7) :
                d{ p0, p1, p2, p3, p4, p5, p6, p7 }
        {
        }

        UInt16x8(const UInt16x8& src) :
            d{src.d[0], src.d[1], src.d[2], src.d[3], src.d[4], src.d[5], src.d[6], src.d[7]}
        {
        }

        UInt16x8(const uint16_t val) : d{val, val, val, val, val, val, val, val}
        {
        }
        
        inline
        UInt16x8 operator&(const UInt16x8& rhs) const {
            return UInt16x8(
                d[0] & rhs.d[0],
                d[1] & rhs.d[1],
                d[2] & rhs.d[2],
                d[3] & rhs.d[3],
                d[4] & rhs.d[4],
                d[5] & rhs.d[5],
                d[6] & rhs.d[6],
                d[7] & rhs.d[7]);
        }

        inline
        UInt16x8 operator|(const UInt16x8& rhs) const {
            return UInt16x8(
                d[0] | rhs.d[0],
                d[1] | rhs.d[1],
                d[2] | rhs.d[2],
                d[3] | rhs.d[3],
                d[4] | rhs.d[4],
                d[5] | rhs.d[5],
                d[6] | rhs.d[6],
                d[7] | rhs.d[7]);
        }
        
        inline
        UInt16x8 operator<<(const int n) const {
            return UInt16x8(
                d[0] << n,
                d[1] << n,
                d[2] << n,
                d[3] << n,
                d[4] << n,
                d[5] << n,
                d[6] << n,
                d[7] << n);
        }
        
        inline
        UInt16x8 operator>>(const int n) const {
            return UInt16x8(
                d[0] >> n,
                d[1] >> n,
                d[2] >> n,
                d[3] >> n,
                d[4] >> n,
                d[5] >> n,
                d[6] >> n,
                d[7] >> n);
        }
    };

    inline
    UInt16x8 Load(const uint8_t* src) {
        return UInt16x8(
            src[0],
            src[1],
            src[2],
            src[3],
            src[4],
            src[5],
            src[6],
            src[7]);
    }

    inline
    void Store(uint16_t* dst, const UInt16x8& src) {
        for(int i = 0; i < 8; i++)
            dst[i] = src.d[i];
    }

#endif

    inline
    void DecodeHeader(uint8_t& bits, uint16_t& reference, const uint8_t* input) {
        bits = ((*input) >> 4) & 0x0F;
        reference = (*(input) & 0x0F) << 8 | *(input + 1);
    }
    
    inline
    const uint8_t* Decode1(uint16_t* output, const uint8_t* input) {
        const UInt16x8 N(0x01);
        const UInt16x8 p = Load(input);

        const UInt16x8 r0 =  p & N;
        const UInt16x8 r1 = (p & (N << 1)) >> 1;
        const UInt16x8 r2 = (p & (N << 2)) >> 2;
        const UInt16x8 r3 = (p & (N << 3)) >> 3;
        const UInt16x8 r4 = (p & (N << 4)) >> 4;
        const UInt16x8 r5 = (p & (N << 5)) >> 5;
        const UInt16x8 r6 = (p & (N << 6)) >> 6;
        const UInt16x8 r7 = (p & (N << 7)) >> 7;

        Store(output,      r0);
        Store(output + 8,  r1);
        Store(output + 16, r2);
        Store(output + 24, r3);
        Store(output + 32, r4);
        Store(output + 40, r5);
        Store(output + 48, r6);
        Store(output + 56, r7);
        
        return input + ENCODING_BLOCK_LENGTH[1];
    }
    
    inline
    const uint8_t* Decode2_One(uint16_t* output, const uint8_t* input) {
        const UInt16x8 N(0x03);
        const UInt16x8 p = Load(input);

        const UInt16x8 r0 =  p & N;
        const UInt16x8 r1 = (p & (N << 2)) >> 2;
        const UInt16x8 r2 = (p & (N << 4)) >> 4;
        const UInt16x8 r3 = (p & (N << 6)) >> 6;
        
        Store(output,      r0);
        Store(output + 8,  r1);
        Store(output + 16, r2);
        Store(output + 24, r3);
        
        return input + 8;
    }
    
    inline
    const uint8_t* Decode2(uint16_t* output, const uint8_t* input) {
        input = Decode2_One(output, input);
        input = Decode2_One(output + 32, input);
        
        return input;
    }
    
    inline
    const uint8_t* Decode3(uint16_t* output, const uint8_t* input) {
        const UInt16x8 N(0x07);
        const UInt16x8 T(0x03);
        const UInt16x8 R(0x01);

        const UInt16x8 p0 = Load(input);
        const UInt16x8 p1 = Load(input+8);
        const UInt16x8 p2 = Load(input+16);

        const UInt16x8 r0  =  p0 & N;
        const UInt16x8 r1  = (p0 & (N << 3)) >> 3;
        const UInt16x8 _r2 = (p0 & (T << 6)) >> 6;

        const UInt16x8 r3  =  p1 & N;
        const UInt16x8 r4  = (p1 & (N << 3)) >> 3;
        const UInt16x8 _r5 = (p1 & (T << 6)) >> 6;

        const UInt16x8 r6  =  p2 & N;
        const UInt16x8 r7  = (p2 & (N << 3)) >> 3;

        // Restore upper bits
        const UInt16x8 r2 = _r2 | (((p2 >> 6) & R) << 2);
        const UInt16x8 r5 = _r5 | (((p2 >> 7) & R) << 2);

        Store(output,      r0);
        Store(output + 8,  r1);
        Store(output + 16, r2);
        Store(output + 24, r3);
        Store(output + 32, r4);
        Store(output + 40, r5);
        Store(output + 48, r6);
        Store(output + 56, r7);
        
        return input + ENCODING_BLOCK_LENGTH[3];
    }

    inline
    const uint8_t* Decode4_One(uint16_t* output, const uint8_t* input) {
        const UInt16x8 N(0x0F);
        const UInt16x8 p = Load(input);
       
        const UInt16x8 r0 =  p & N;
        const UInt16x8 r1 = (p & (N << 4)) >> 4;
        
        Store(output,    r0);
        Store(output+8,  r1);
        
        return input + 8;
    }
    
    inline
    const uint8_t* Decode4(uint16_t* output, const uint8_t* input) {
        input = Decode4_One(output, input);
        input = Decode4_One(output + 16, input);
        input = Decode4_One(output + 32, input);
        input = Decode4_One(output + 48, input);
        
        return input;
    }
    
    inline
    const uint8_t* Decode5(uint16_t* output, const uint8_t* input) {
        const UInt16x8 N(0x1F);
        const UInt16x8 L(0x07);
        const UInt16x8 U(0x03);
        const UInt16x8 F(0x01);

        const UInt16x8 p0 = Load(input);
        const UInt16x8 p1 = Load(input+8);
        const UInt16x8 p2 = Load(input+16);
        const UInt16x8 p3 = Load(input+24);
        const UInt16x8 p4 = Load(input+32);

        const UInt16x8 r0 =  p0 & N;
        const UInt16x8 r1 =  p1 & N;
        const UInt16x8 r2 =  p2 & N;
        const UInt16x8 r3 =  p3 & N;
        const UInt16x8 r4 =  p4 & N;
        
        const UInt16x8 r5 = (p0 >> 5) & L | (((p3 >> 5) & U) << 3);
        const UInt16x8 r6 = (p1 >> 5) & L | (((p4 >> 5) & U) << 3);

        const UInt16x8 tmp0 = (p2 >> 5) & L;
        const UInt16x8 tmp1 = tmp0 | ((p3 >> 7) & F) << 3;
        
        const UInt16x8 r7   = tmp1 | ((p4 >> 7) & F) << 4;
        
        Store(output,      r0);
        Store(output + 8,  r1);
        Store(output + 16, r2);
        Store(output + 24, r3);
        Store(output + 32, r4);
        Store(output + 40, r5);
        Store(output + 48, r6);
        Store(output + 56, r7);

        return input + ENCODING_BLOCK_LENGTH[5];
    }
    
    inline
    const uint8_t* Decode6(uint16_t* output, const uint8_t* input) {
        const UInt16x8 N(0x3F);
        const UInt16x8 L(0x03);

        const UInt16x8 p0 = Load(input);
        const UInt16x8 p1 = Load(input+8);
        const UInt16x8 p2 = Load(input+16);
        const UInt16x8 p3 = Load(input+24);
        const UInt16x8 p4 = Load(input+32);
        const UInt16x8 p5 = Load(input+40);

        const UInt16x8 r0 =  p0 & N;
        const UInt16x8 r1 =  p1 & N;
        const UInt16x8 r2 =  p2 & N;
        const UInt16x8 r3 =  p3 & N;
        const UInt16x8 r4 =  p4 & N;
        const UInt16x8 r5 =  p5 & N;

        const UInt16x8 r6 =
               ((p0 >> 6) & L)
            | (((p1 >> 6) & L) << 2)
            | (((p1 >> 6) & L) << 2)
            | (((p2 >> 6) & L) << 4);
            
        const UInt16x8 r7 =
               ((p3 >> 6) & L)
            | (((p4 >> 6) & L) << 2)
            | (((p5 >> 6) & L) << 4);
        
        Store(output,      r0);
        Store(output + 8,  r1);
        Store(output + 16, r2);
        Store(output + 24, r3);
        Store(output + 32, r4);
        Store(output + 40, r5);
        Store(output + 48, r6);
        Store(output + 56, r7);

        return input + ENCODING_BLOCK_LENGTH[6];
    }
    
    inline
    const uint8_t* Decode8_One(uint16_t* output, const uint8_t* input) {
        Store(output, Load(input));
        
        return input + 8;
    }

    inline
    const uint8_t* Decode8(uint16_t* output, const uint8_t* input) {
        input = Decode8_One(output, input);
        input = Decode8_One(output + 8, input);
        input = Decode8_One(output + 16, input);
        input = Decode8_One(output + 24, input);

        input = Decode8_One(output + 32, input);
        input = Decode8_One(output + 40, input);
        input = Decode8_One(output + 48, input);
        input = Decode8_One(output + 56, input);

        return input;
    }

    inline
    const uint8_t* Decode10(uint16_t* output, const uint8_t* input) {
        const UInt16x8 N(0xFF);
        const UInt16x8 L(0x03);

        const UInt16x8 p0 = Load(input);
        const UInt16x8 p1 = Load(input+8);
        const UInt16x8 p2 = Load(input+16);
        const UInt16x8 p3 = Load(input+24);
        const UInt16x8 p4 = Load(input+32);
        const UInt16x8 p5 = Load(input+40);
        const UInt16x8 p6 = Load(input+48);
        const UInt16x8 p7 = Load(input+56);
        const UInt16x8 p8 = Load(input+64);
        const UInt16x8 p9 = Load(input+72);

        const UInt16x8 _r0 = p0 & N;
        const UInt16x8 _r1 = p1 & N;
        const UInt16x8 _r2 = p2 & N;
        const UInt16x8 _r3 = p3 & N;

        const UInt16x8 r0 = _r0 | ((p4 & L)          << 8);
        const UInt16x8 r1 = _r1 | ((p4 & (L << 2))   << 6);
        const UInt16x8 r2 = _r2 | ((p4 & (L << 4))   << 4);
        const UInt16x8 r3 = _r3 | ((p4 & (L << 6))   << 2);
        
        const UInt16x8 _r4 =  p5 & N;
        const UInt16x8 _r5 =  p6 & N;
        const UInt16x8 _r6 =  p7 & N;
        const UInt16x8 _r7 =  p8 & N;

        const UInt16x8 r4 = _r4 | ((p9 & L)          << 8);
        const UInt16x8 r5 = _r5 | ((p9 & (L << 2))   << 6);
        const UInt16x8 r6 = _r6 | ((p9 & (L << 4))   << 4);
        const UInt16x8 r7 = _r7 | ((p9 & (L << 6))   << 2);

        Store(output,      r0);
        Store(output + 8,  r1);
        Store(output + 16, r2);
        Store(output + 24, r3);
        Store(output + 32, r4);
        Store(output + 40, r5);
        Store(output + 48, r6);
        Store(output + 56, r7);

        return input + ENCODING_BLOCK_LENGTH[10];
    }
    
    inline
    const uint8_t* Decode16_ONE(uint16_t* output, const uint8_t* input) {
        auto input16 = reinterpret_cast<const uint16_t*>(input);
        
        const UInt16x8 p(
            input16[0],
            input16[1],
            input16[2],
            input16[3],
            input16[4],
            input16[5],
            input16[6],
            input16[7]);
        
        Store(output, p);
        
        return input + 16;
    }

    inline
    const uint8_t* Decode16(uint16_t* output, const uint8_t* input) {
        input = Decode16_ONE(output,    input);
        input = Decode16_ONE(output+8,  input);
        input = Decode16_ONE(output+16, input);
        input = Decode16_ONE(output+24, input);

        input = Decode16_ONE(output+32, input);
        input = Decode16_ONE(output+40, input);
        input = Decode16_ONE(output+48, input);
        input = Decode16_ONE(output+56, input);

        return input;
    }
    
    inline
    size_t DecodeBlock(
        uint16_t* output,
        const uint16_t bits,
        const uint8_t* input,
        const size_t offset,
        const size_t len)
    {
        // Don't decode if past end of input
        if(offset + ENCODING_BLOCK_LENGTH[bits] > len)
            return len - offset;
     
        input += offset;

        switch (bits) {
            case 0:
                std::memset(output, 0, sizeof(uint16_t)*ENCODING_BLOCK);
                break;
            case 1:
                Decode1(output, input);
                break;
            case 2:
                Decode2(output, input);
                break;
            case 3:
                Decode3(output, input);
                break;
            case 4:
                Decode4(output, input);
                break;
            case 5:
                Decode5(output, input);
                break;
            case 6:
                Decode6(output, input);
                break;
            case 7:
            case 8:
                Decode8(output, input);
                break;
            case 9:
            case 10:
                Decode10(output, input);
                break;
            default:
            case 16:
                Decode16(output, input);
                break;
        }

        return ENCODING_BLOCK_LENGTH[bits];
    }
    
    inline
    size_t DecodeMetadata(
        const uint8_t* input,
        size_t offset,
        const size_t len,
        std::vector<uint16_t>& outMetadata)
    {
        uint32_t numBlocks =
                 static_cast<uint32_t>(input[offset])
            |   (static_cast<uint32_t>(input[offset+1]) << 8)
            |   (static_cast<uint32_t>(input[offset+2]) << 16)
            |   (static_cast<uint32_t>(input[offset+3]) << 24);
    
        outMetadata.resize(numBlocks);
        offset += 4;
        
        uint8_t bits;
        uint16_t reference;

        // Decode bits
        uint16_t* data = outMetadata.data();

        for(int i = 0; i < numBlocks; i+=ENCODING_BLOCK) {
            DecodeHeader(bits, reference, input+offset);
            
            offset += HEADER_LENGTH;
            offset += DecodeBlock(data, bits, input, offset, len);
            
            for(int x = 0; x < ENCODING_BLOCK; x++)
                data[x] += reference;
            
            data += ENCODING_BLOCK;
        }
        
        return offset;
    }
    
    void ReadMetadataHeader(const uint8_t* input, uint32_t& encodedWidth, uint32_t& encodedHeight, uint32_t& bitsOffset, uint32_t& refsOffset) {
        encodedWidth =
                 static_cast<uint32_t>(input[0])
            |   (static_cast<uint32_t>(input[1]) << 8)
            |   (static_cast<uint32_t>(input[2]) << 16)
            |   (static_cast<uint32_t>(input[3]) << 24);

        encodedHeight =
                 static_cast<uint32_t>(input[4])
            |   (static_cast<uint32_t>(input[5]) << 8)
            |   (static_cast<uint32_t>(input[6]) << 16)
            |   (static_cast<uint32_t>(input[7]) << 24);

        bitsOffset =
                 static_cast<uint32_t>(input[8])
            |   (static_cast<uint32_t>(input[9])  << 8)
            |   (static_cast<uint32_t>(input[10]) << 16)
            |   (static_cast<uint32_t>(input[11]) << 24);

        refsOffset =
                 static_cast<uint32_t>(input[12])
            |   (static_cast<uint32_t>(input[13]) << 8)
            |   (static_cast<uint32_t>(input[14]) << 16)
            |   (static_cast<uint32_t>(input[15]) << 24);
    }
    
    } // unnamed namespace

    size_t Decode(
        uint16_t* output,
        const int width,
        const int height,
        const uint8_t* input,
        const size_t len)
    {
        uint16_t* outputStart = output;
        
        uint16_t p0[ENCODING_BLOCK];
        uint16_t p1[ENCODING_BLOCK];
        uint16_t p2[ENCODING_BLOCK];
        uint16_t p3[ENCODING_BLOCK];

        std::vector<uint16_t> bits, refs;
        uint32_t encodedWidth, encodedHeight, bitsOffset, refsOffset;

        ReadMetadataHeader(input, encodedWidth, encodedHeight, bitsOffset, refsOffset);
        
        if(bitsOffset > len || refsOffset > len)
            return 0;
        
        if(encodedWidth % ENCODING_BLOCK > 0)
            return 0;
            
        if(encodedWidth < width)
            return 0;

        // Decode bits
        DecodeMetadata(input, bitsOffset, len, bits);
        
        // Decode refs
        DecodeMetadata(input, refsOffset, len, refs);

        size_t offset = METADATA_OFFSET;
        
        std::vector<uint16_t> row0(encodedWidth);
        std::vector<uint16_t> row1(encodedWidth);
        std::vector<uint16_t> row2(encodedWidth);
        std::vector<uint16_t> row3(encodedWidth);
        
        int metadataIdx = 0;

        for(int y = 0; y < encodedHeight; y+=4) {
            for(int x = 0; x < encodedWidth; x += ENCODING_BLOCK) {
                uint16_t blockBits[4] = { bits[metadataIdx], bits[metadataIdx+1], bits[metadataIdx+2], bits[metadataIdx+3] };
                uint16_t blockRef[4] = { refs[metadataIdx], refs[metadataIdx+1], refs[metadataIdx+2], refs[metadataIdx+3] };
            
                offset += DecodeBlock(&p0[0], blockBits[0], input, offset, len);
                offset += DecodeBlock(&p1[0], blockBits[1], input, offset, len);
                offset += DecodeBlock(&p2[0], blockBits[2], input, offset, len);
                offset += DecodeBlock(&p3[0], blockBits[3], input, offset, len);

                for(int i = 0; i < ENCODING_BLOCK; i+=2) {
                    row0[x + i]     = p0[i/2] + blockRef[0];
                    row0[x + i + 1] = p1[i/2] + blockRef[1];
                    
                    row1[x + i]     = p2[i/2] + blockRef[2];
                    row1[x + i + 1] = p3[i/2] + blockRef[3];

                    row2[x + i]     = p0[ENCODING_BLOCK/2+i/2] + blockRef[0];
                    row2[x + i + 1] = p1[ENCODING_BLOCK/2+i/2] + blockRef[1];
                    
                    row3[x + i]     = p2[ENCODING_BLOCK/2+i/2] + blockRef[2];
                    row3[x + i + 1] = p3[ENCODING_BLOCK/2+i/2] + blockRef[3];
                }
                
                metadataIdx += 4;
            }

            std::memcpy(output, row0.data(), width * 2);
            output += width;

            std::memcpy(output, row1.data(), width * 2);
            output += width;

            std::memcpy(output, row2.data(), width * 2);
            output += width;

            std::memcpy(output, row3.data(), width * 2);
            output += width;
        }
        
        return (output - outputStart);
    }
}}

extern "C" size_t mr_decode_video_frame(uint8_t *dstData, uint8_t *srcData, uint32_t srcSize, int width, int height)
{
    return motioncam::raw::Decode((uint16_t*)dstData, width, height, srcData, srcSize);
}
