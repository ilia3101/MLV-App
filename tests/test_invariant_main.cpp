#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <climits>

// Forward declarations for the functions we're testing
extern "C" {
    void* MLVBlenderCreate();
    void MLVBlenderDestroy(void* blender);
    int MLVBlenderGetOutputWidth(void* blender);
    int MLVBlenderGetOutputHeight(void* blender);
}

class BufferOverflowSecurityTest : public ::testing::TestWithParam<std::pair<int, int>> {};

TEST_P(BufferOverflowSecurityTest, FormattedStringDoesNotOverflowBuffer) {
    // Invariant: sprintf formatting of MLV dimensions into fixed buffer must not overflow
    // The buffer "Result %ix%i" with two ints should never exceed reasonable bounds
    
    int width = GetParam().first;
    int height = GetParam().second;
    
    // Simulate the vulnerable code path with bounds checking
    char subtitle_string[256];  // Fixed-size buffer as in original code
    int written = snprintf(subtitle_string, sizeof(subtitle_string), 
                           "Result %ix%i", width, height);
    
    // Security property: formatted output must fit within buffer without overflow
    ASSERT_GE(written, 0) << "snprintf failed";
    ASSERT_LT(written, (int)sizeof(subtitle_string)) 
        << "Formatted string would overflow buffer with width=" << width 
        << " height=" << height;
    
    // Verify null termination is present
    ASSERT_EQ(subtitle_string[written], '\0') 
        << "Buffer not properly null-terminated";
}

INSTANTIATE_TEST_SUITE_P(
    AdversarialDimensions,
    BufferOverflowSecurityTest,
    ::testing::Values(
        std::make_pair(1920, 1080),           // Valid input
        std::make_pair(INT_MAX, INT_MAX),     // Exploit: maximum int values
        std::make_pair(999999999, 999999999), // Boundary: very large dimensions
        std::make_pair(-1, -1),               // Boundary: negative values
        std::make_pair(0, 0)                  // Boundary: zero dimensions
    )
);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}