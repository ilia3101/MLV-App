#ifndef _matrix_h_
#define _matrix_h_

/* Matrix functions for 3x3
 * Useless for most things */

/* Removes the 10000 dividers from Magic Lantern matrices by dividing 
 * returns doubles to outputMatrix, input to be 18 long and output 9 */
void matrixRemoveDividers(int32_t * inputMatrix, double * outputMatrix);

/* Multiply 3x3 matrices - A and B */
void multiplyMatrices(double * A, double * B, double * outputMatrix);

/* Amazing inversion function */
void invertMatrix(double * inputMatrix, double * outputMatrix);

/* Prints a matrix! */
void printMatrix(double * matrix);

/* SLOW!!! V is vector, M is matrix */
void applyMatrix(double * V, double * M);

#endif