#include <motioncam/RawData.hpp>
#include <vector>

namespace motioncam {
    namespace raw {
        namespace {
        
    const int BLOCK_SIZE = 16;
    const int ENCODING_BLOCK = BLOCK_SIZE*2;
    const int HEADER_LENGTH = 2;
    
    // block bits -> encoding size in bytes mapping
    const int ENCODING_BLOCK_LENGTH[] = {
        0,  // 0 bits
        2,  // 1 bit
        4,  // 2 bits
        6,  // 3 bits
        8,  // 4 bits
        10, // 5 bits
        12, // 6 bits
        14, // 7 bits
        16, // 8 bits
        18, // 9 bits
        20, // 10 bits
        // Everything above 10 bits is encoded with 16 bits
        32, // 11 bits
        32, // 12 bits
        32, // 13 bits
        32, // 14 bits
        32, // 15 bits
        32  // 16 bits
    };
        
    int GetPaddedWidth(int width) {
        return (ENCODING_BLOCK)*((width + ENCODING_BLOCK - 1) / (ENCODING_BLOCK));
    }
    
    const uint8_t* Decode1(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 8; i++) {
            *(output) = (*input >> 7) & 0x01;
            ++output;
            
            *(output) = (*input >> 6) & 0x01;
            ++output;

            *(output) = (*input >> 5) & 0x01;
            ++output;

            *(output) = (*input >> 4) & 0x01;
            ++output;

            *(output) = (*input >> 3) & 0x01;
            ++output;

            *(output) = (*input >> 2) & 0x01;
            ++output;

            *(output) = (*input >> 1) & 0x01;
            ++output;

            *(output) = (*input >> 0) & 0x01;
            ++output;
            
            ++input;
        }
        
        return input;
    }
    
    const uint8_t* Decode2(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 4; i++) {
            *output = (*input >> 6) & 0x03;
            ++output;
            
            *output = (*input >> 4) & 0x03;
            ++output;

            *output = (*input >> 2) & 0x03;
            ++output;

            *output = (*input >> 0) & 0x03;
            ++output;
            
            ++input;
        }
        
        return input;
    }
    
    const uint8_t* Decode3(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 8; i++) {
            *output = (*input >> 5) & 0x07;
            ++output;
            
            *output = (*input >> 2) & 0x07;
            ++output;

            *output = ((*input & 0x03) << 1) | ((*(input + 1) >> 7) & 0x01);
            ++output;
            ++input;
            
            *output = (*input >> 4) & 0x07;
            ++output;

            *output = (*input >> 1) & 0x07;
            ++output;

            *output = ((*input & 0x01) << 2) | ((*(input + 1) >> 6) & 0x03);
            ++output;
            ++input;

            *output = (*input >> 3) & 0x07;
            ++output;
            
            *output = (*input >> 0) & 0x07;
            ++output;

            ++input;
        }
        
        return input;
    }
    
    const uint8_t* Decode4(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 2; i++) {
            *output = (*input >> 4) & 0x0F;
            ++output;
            
            *output = (*input >> 0) & 0x0F;
            ++output;
            
            ++input;
        }
        
        return input;
    }
    
    const uint8_t* Decode5(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 8; i++) {
            *output = (*input >> 3) & 0x1F;
            ++output;
            
            *output = ((*input & 0x07) << 2) | ((*(input + 1) >> 6) & 0x03);
            ++output;
            
            ++input;
            
            *output = (*input >> 1) & 0x1F;
            ++output;
            
            *output = ((*input & 0x01) << 4) | ((*(input + 1) >> 4) & 0x0F);
            ++output;
            
            ++input;
            
            *output = ((*input & 0x0F) << 1) | ((*(input + 1) >> 7) & 0x01);
            ++output;
            
            ++input;
            
            *output = (*input >> 2) & 0x1F;
            ++output;
            
            *output = ((*input & 0x03) << 3) | ((*(input + 1) >> 5) & 0x07);
            ++output;
            
            ++input;
            
            *output = (*input) & 0x1F;
            ++output;
            
            ++input;
        }
        
        return input;
    }
    
    const uint8_t* Decode6(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 4; i++) {
            *output = (*input >> 2) & 0x3F;
            ++output;

            *output = ((*input & 0x03) << 4) | ((*(input + 1) >> 4) & 0x0F);
            ++output;
            
            ++input;
            
            *output = ((*input) & 0x0F) << 2 | ((*(input + 1) >> 6) & 0x03);
            ++output;
            
            ++input;

            *output = (*input) & 0x3F;
            ++output;
            
            ++input;
        }
        
        return input;
    }
    
    const uint8_t* Decode7(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 8; i++) {
            *output = (*input >> 1) & 0x7F;
            ++output;

            *output = (((*input) & 0x01) << 6) | ((*(input + 1) >> 2) & 0x3F);
            ++output;
            
            ++input;

            *output = (((*input) & 0x03) << 5) | ((*(input + 1) >> 3) & 0x1F);
            ++output;
            
            ++input;
            
            *output = (((*input) & 0x07) << 4) | ((*(input + 1) >> 4) & 0x0F);
            ++output;

            ++input;
            
            *output = (((*input) & 0x0F) << 3) | ((*(input + 1) >> 5) & 0x07);
            ++output;
            
            ++input;

            *output = (((*input) & 0x1F) << 2) | ((*(input + 1) >> 6) & 0x03);
            ++output;
            
            ++input;
            
            *output = (((*input) & 0x3F) << 1) | ((*(input + 1) >> 7) & 0x01);
            ++output;
            
            ++input;
            
            *output = (*input) & 0x7F;
            ++output;
            
            ++input;
        }
        
        return input;
    }
    
    const uint8_t* Decode8(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 8; i++) {
            *output = static_cast<uint8_t>(*input);
            ++output;
            ++input;

            *output = static_cast<uint8_t>(*input);
            ++output;
            ++input;

            *output = static_cast<uint8_t>(*input);
            ++output;
            ++input;

            *output = static_cast<uint8_t>(*input);
            ++output;
            ++input;

            *output = static_cast<uint8_t>(*input);
            ++output;
            ++input;

            *output = static_cast<uint8_t>(*input);
            ++output;
            ++input;

            *output = static_cast<uint8_t>(*input);
            ++output;
            ++input;

            *output = static_cast<uint8_t>(*input);
            ++output;
            ++input;
        }
        
        return input;
    }
    
    const uint8_t* Decode9(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 8; i++) {
            *output = *(input) << 1 | ((*(input + 1) >> 7) & 0x01);
            
            ++input;
            ++output;
            
            *output = ((*(input) & 0x7F) << 2) | ((*(input + 1) >> 6) & 0x03);
            
            ++input;
            ++output;

            *output = ((*(input) & 0x3F) << 3) | ((*(input + 1) >> 5) & 0x07);
            
            ++input;
            ++output;
            
            *output = ((*(input) & 0x1F) << 4) | ((*(input + 1) >> 4) & 0x0F);
            
            ++input;
            ++output;
            
            *output = ((*(input) & 0x0F) << 5) | ((*(input + 1) >> 3) & 0x1F);
            
            ++input;
            ++output;
            
            *output = ((*(input) & 0x07) << 6) | ((*(input + 1) >> 2) & 0x3F);
            
            ++input;
            ++output;

            *output = ((*(input) & 0x03) << 7) | ((*(input + 1) >> 1) & 0x7F);
            
            ++input;
            ++output;

            *output = ((*(input) & 0x01) << 8) | (*(input + 1) >> 0);
            
            ++input;
            ++input;
            
            ++output;
        }
        
        return input;
    }
    
    const uint8_t* Decode10(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE / 4; i++) {
            *output = ((*input) << 2) | ((*(input + 1) >> 6) & 0x03);
            
            ++output;
            ++input;
            
            *output = (((*input) & 0x3F) << 4) | ((*(input + 1) >> 4) & 0x0F);
            
            ++output;
            ++input;
            
            *output = (((*input) & 0x0F) << 6) | ((*(input + 1) >> 2) & 0x3F);
            
            ++output;
            ++input;

            *output = (((*input) & 0x03) << 8) | (*(input + 1) >> 0);
            
            ++output;
            
            ++input;
            ++input;
        }
        
        return input;
    }
    
    const uint8_t* Decode16(uint16_t* output, const uint8_t* input) {
        for(int i = 0; i < BLOCK_SIZE; i++) {
            *output = (*input << 8) | *(input + 1);

            ++output;
            ++input;
            ++input;
        }

        return input;
    }
    
    void DecodeHeader(uint8_t& bits, uint16_t& reference, const uint8_t* input) {
        bits = ((*input) >> 4) & 0x0F;
        reference = (*(input) & 0x0F) << 8 | *(input + 1);
    }
    
    size_t DecodeBlock(
        uint16_t* output,
        uint16_t& reference,
        const uint8_t* input,
        const size_t offset,
        const size_t len)
    {
        uint8_t bits;

        // Don't decode if past end of input
        if(offset + HEADER_LENGTH >= len)
            return len - offset;

        input += offset;
        
        DecodeHeader(bits, reference, input);
        input += HEADER_LENGTH;
        
        bits = std::min((uint8_t)16, bits);
        
        // Don't decode if past end of input
        if(offset + HEADER_LENGTH + ENCODING_BLOCK_LENGTH[bits] >= len)
            return len - offset;
        
        switch (bits) {
            case 0:
                std::memset(output, 0, sizeof(uint16_t)*BLOCK_SIZE);
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
                Decode7(output, input);
                break;
            case 8:
                Decode8(output, input);
                break;
            case 9:
                Decode9(output, input);
                break;
            case 10:
                Decode10(output, input);
                break;
            default:
            case 16:
                Decode16(output, input);
                break;
        }

        return HEADER_LENGTH + ENCODING_BLOCK_LENGTH[bits];
    }
    } // anonymous namespace

    size_t DecodeLegacy(uint16_t* output, const int width, const int height, const uint8_t* input, const size_t len) {
        uint16_t* outputStart = output;
        
        // Account for padding at the end
        const int paddedWidth = GetPaddedWidth(width);

        // Get decoding offset blocks if available
        std::vector<uint32_t> decodeOffsets;
        decodeOffsets.reserve(5);
        
        uint8_t marker = input[len - 1];
        size_t decodeOffset = len - 1;
        
        while(marker == 0xFF) {
            uint32_t pos =
                ((uint32_t) input[decodeOffset-4] << 24) |
                ((uint32_t) input[decodeOffset-3] << 16) |
                ((uint32_t) input[decodeOffset-2] << 8)  |
                ((uint32_t) input[decodeOffset-1]);
            
            decodeOffsets.push_back(pos);
            
            decodeOffset -= 5;
            marker = input[decodeOffset];
        }


        std::vector<uint16_t> row(paddedWidth);
        uint16_t reference0, reference1;
        uint16_t p[ENCODING_BLOCK];

        size_t offset = 0;

        for(int y = 0; y < height; y++) {
            for(int x = 0; x < paddedWidth; x += ENCODING_BLOCK) {
                offset += DecodeBlock(&p[0], reference0, input, offset, len);
                offset += DecodeBlock(&p[16], reference1, input, offset, len);

                for(int i = 0; i < ENCODING_BLOCK; i+=2) {
                    row[x + i]   = p[i/2] + reference0;
                    row[x + i+1] = p[BLOCK_SIZE+i/2] + reference1;
                }
            }

            // Skip padded garbage at the ned
            std::memcpy(output, row.data(), width * 2);
            output += width;
        }
        
        return (output - outputStart);
    }
    
}} // namespace
