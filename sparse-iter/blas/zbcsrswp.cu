/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011

       @precisions normal z -> c d s

*/

#include "common_magma.h"

#if (GPUSHMEM < 200)
   #define BLOCK_SIZE 128
#else
   #define BLOCK_SIZE 512
#endif




/*  -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011

    Purpose
    =======
    
    For a Block-CSR ILU factorization, this routine swaps rows in the vector *x
    according to the pivoting in *ipiv.
    
    Arguments
    =========

    magma_int_t r_blocks            number of blocks
    magma_int_t size_b              blocksize in BCSR
    magma_int_t *ipiv               array containing pivots
    magmaDoubleComplex *x           input/output vector x

    =====================================================================    */

extern "C" magma_int_t
magma_zbcsrswp(   magma_int_t r_blocks,
                  magma_int_t size_b, 
                  magma_int_t *ipiv,
                  magmaDoubleComplex *x ){


    int nrhs = 1, n = r_blocks*size_b, ione = 1, inc = 1;

    magmaDoubleComplex *work; 
    magma_zmalloc_cpu( &work, r_blocks*size_b );

    magma_zgetmatrix(n, 1, x, n, work, n);
    for( magma_int_t k=0; k<r_blocks; k++)
      lapackf77_zlaswp(&nrhs, work+k*size_b, &n, &ione, &size_b, ipiv+k*size_b, &inc);
    magma_zsetmatrix(n, 1, work, n, x, n);
   
    magma_free_cpu(work);

    return MAGMA_SUCCESS;
}



