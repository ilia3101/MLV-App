/* Basic matrix functions for 3x3 matrices */
#include <stdint.h>
#include <stdio.h>

#include "matrix.h"

/* Very uzeful diagonal flip feature */
static uint8_t diagonal_flip[9] = { 0, 3, 6, 1, 4, 7, 2, 5, 8 };
/* Makes it like a function */
#define diag_flip(X) diagonal_flip[(X)]

/* Removes the 10000 dividers from ML matrices - returns doubles */
void matrixRemoveDividers(int32_t * inputMatrix, double * outputMatrix)
{
    for (int i = 0; i < 9; ++i) /* Divide term by the following term */
        outputMatrix[i] = (double)inputMatrix[2*i] / (double)inputMatrix[2*i+1];
}

/* Multiplies matrices A and B to outputMatrix */
void multiplyMatrices(double * A, double * B, double * outputMatrix)
{
    outputMatrix[0] = A[0] * B[0] + A[1] * B[3] + A[2] * B[6];
    outputMatrix[1] = A[0] * B[1] + A[1] * B[4] + A[2] * B[7];
    outputMatrix[2] = A[0] * B[2] + A[1] * B[5] + A[2] * B[8];
    outputMatrix[3] = A[3] * B[0] + A[4] * B[3] + A[5] * B[6];
    outputMatrix[4] = A[3] * B[1] + A[4] * B[4] + A[5] * B[7];
    outputMatrix[5] = A[3] * B[2] + A[4] * B[5] + A[5] * B[8];
    outputMatrix[6] = A[6] * B[0] + A[7] * B[3] + A[8] * B[6];
    outputMatrix[7] = A[6] * B[1] + A[7] * B[4] + A[8] * B[7];
    outputMatrix[8] = A[6] * B[2] + A[7] * B[5] + A[8] * B[8];
}

void invertMatrix(double * inputMatrix, double * outputMatrix)
{
    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            /* Determenant locations for 2 x 2 */
            int dX[2] = { (x + 1) % 3, (x + 2) % 3 };
            int dY[2] = { 3 * ((y + 1) % 3), 3 * ((y + 2) % 3) };

            outputMatrix[ diag_flip(y*3 + x) ] = 
            (   /* Determinant caluclation 2 x 2 */
                  inputMatrix[dY[0] + dX[0]] 
                * inputMatrix[dY[1] + dX[1]]
                - inputMatrix[dY[0] + dX[1]] 
                * inputMatrix[dY[1] + dX[0]]
            );
        }
    }

    /* Calculate whole matrix determinant */
    double determinant = 1.0 / (
          inputMatrix[0] * ( inputMatrix[8] * inputMatrix[4] - inputMatrix[7] * inputMatrix[5] )
        - inputMatrix[3] * ( inputMatrix[8] * inputMatrix[1] - inputMatrix[7] * inputMatrix[2] )
        + inputMatrix[6] * ( inputMatrix[5] * inputMatrix[1] - inputMatrix[4] * inputMatrix[2] )
    );

    /* Multiply all elements by the determinant */
    for (int i = 0; i < 9; ++i) outputMatrix[i] *= determinant;
}

void printMatrix(double * matrix)
{
#ifndef STDOUT_SILENT
    for (int i = 0; i < 9; i += 3)
        printf("[ %.4f, %.4f, %.4f ]\n", matrix[i], matrix[i+1], matrix[i+2]);
#else
    (void)matrix; //hide warning
#endif
}

/* SLOW, V is vector, M is matrix */
void applyMatrix(double * V, double * M)
{
    V[0] = M[0] * V[0] + M[1] * V[1] + M[2] * V[2];
    V[1] = M[3] * V[0] + M[4] * V[1] + M[5] * V[2];
    V[2] = M[6] * V[0] + M[7] * V[1] + M[8] * V[2];
}
