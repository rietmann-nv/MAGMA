/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date

       @precisions normal z -> c d s

*/

#include "common_magma.h"
#include "../include/magmasparse_z.h"
#include "../../include/magma.h"


#define PRECISION_z



// every row is handled by one threadblock
__global__ void 
magma_zailu_csr_s_kernel(   magma_int_t Lnum_rows, 
                            magma_int_t Lnnz,  
                            magmaDoubleComplex *AL, 
                            magmaDoubleComplex *valL, 
                            magma_index_t *rowptrL, 
                            magma_index_t *rowidxL, 
                            magma_index_t *colidxL, 
                            magmaDoubleComplex *valL_n,
                            magma_int_t Unum_rows, 
                            magma_int_t Unnz,  
                            magmaDoubleComplex *AU, 
                            magmaDoubleComplex *valU, 
                            magma_index_t *rowptrU, 
                            magma_index_t *rowidxU, 
                            magma_index_t *colidxU, 
                            magmaDoubleComplex *valU_n){

    int i, j;
    int k = blockDim.x * blockIdx.x + threadIdx.x;

    magmaDoubleComplex zero = MAGMA_Z_MAKE(0.0, 0.0);
    magmaDoubleComplex s, sp;
    int il, iu, jl, ju;


    if (k < Lnnz)
    {     

        i = (blockIdx.y == 0 ) ? rowidxL[k] : rowidxU[k]  ;
        j = (blockIdx.y == 0 ) ? colidxL[k] : colidxU[k]  ;

        s = (blockIdx.y == 0 ) ? AL[k] : AU[k] ;

        il = rowptrL[i];
        iu = rowptrL[j];

        while (il < rowptrL[i+1] && iu < rowptrL[j+1])
        {
            sp = zero;
            jl = colidxL[il];
            ju =  rowidxU[iu];

            if (jl < ju)
                il++;
            else if (ju < jl)
                iu++;
            else
            {
                // we are going to modify this u entry
                sp = valL[il] * valU[iu];
                s -= sp;
                il++;
                iu++;
            }
        }

        // undo the last operation (it must be the last)
        s += sp;
__syncthreads();
        // modify u entry
        if (blockIdx.y == 0){
            valL[k] =  s / valU[rowptrU[j+1]-1];}
        else{
            valU[k] = s;
        }
    }

}// kernel 













/*  -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date

    Purpose
    =======
    
    This routine computes the ILU approximation of a matrix iteratively. 
    The idea is according to Edmond Chow's presentation at SIAM 2014.
    The input format of the matrix is Magma_CSRCSCL and MagmaCSRCSCU for
    the lower and upper part. L is in CSRL, U is in CSRU, but column-major.
    Every component of L and U is handled by one thread. Hence, we need to 
    synchronize to avoid read/write accidents.

    Arguments
    =========

    magma_z_sparse_matrix A_L               input matrix L
    magma_z_sparse_matrix A_U               input matrix U
    magma_z_sparse_matrix L                 input/output matrix L
    magma_z_sparse_matrix U                 input/output matrix U

    ======================================================================    */

extern "C" magma_int_t
magma_zailu_csr_s( magma_z_sparse_matrix A_L,
                   magma_z_sparse_matrix A_U,
                   magma_z_sparse_matrix L,
                   magma_z_sparse_matrix U ){



    
    int blocksize1 = 256;
    int blocksize2 = 1;

    int dimgrid1 = ( A_L.nnz + blocksize1 -1 ) / blocksize1;
    int dimgrid2 = 2;
    int dimgrid3 = 1;

    magmaDoubleComplex *val_Ln, *val_Un;
  //  magma_zmalloc( &val_Ln, A_L.nnz);
  //  magma_zmalloc( &val_Un, A_U.nnz);

    dim3 grid( dimgrid1, dimgrid2, dimgrid3 );
    dim3 block( blocksize1, blocksize2, 1 );
    magma_zailu_csr_s_kernel<<< grid, block, 0, magma_stream >>>
            ( A_L.num_rows, A_L.nnz,  A_L.val, L.val, L.row, L.blockinfo, L.col, val_Ln,
              A_U.num_rows, A_U.nnz,  A_U.val, U.val, U.row, U.blockinfo, U.col, val_Un );

  //  cudaMemcpy( L.val, val_Ln, A_L.nnz*sizeof( magmaDoubleComplex ), 
           //                                         cudaMemcpyDeviceToDevice );

   // cudaMemcpy( U.val, val_Un, A_U.nnz*sizeof( magmaDoubleComplex ), 
      //                                              cudaMemcpyDeviceToDevice );


    cudaFree(val_Ln);
    cudaFree(val_Un);
    return MAGMA_SUCCESS;
}



