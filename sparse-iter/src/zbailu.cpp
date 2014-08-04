/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date

       @author Hartwig Anzt 

       @precisions normal z -> s d c
*/
// includes CUDA
#include <cuda_runtime_api.h>
#include <cublas.h>
#include <cusparse_v2.h>
#include <cuda_profiler_api.h>

// project includes
#include "common_magma.h"
#include "../include/magmasparse.h"

#include <assert.h>


#define PRECISION_z


/**
    Purpose
    -------

    Prepares the ILU preconditioner via the asynchronous ILU iteration.

    Arguments
    ---------

    @param
    A           magma_z_sparse_matrix
                input matrix A

    @param
    precond     magma_z_preconditioner*
                preconditioner parameters

    @ingroup magmasparse_zgepr
    ********************************************************************/

magma_int_t
magma_zailusetup( magma_z_sparse_matrix A, magma_z_preconditioner *precond ){

    magma_z_sparse_matrix hAh, hA, hL, hU, hAcopy, hAL, hAU, hAUt, hUT, hAtmp,
                        hACSRCOO, dAinitguess, dL, dU, DL, RL, DU, RU;

    // copy original matrix as CSRCOO to device
    magma_z_mtransfer(A, &hAh, A.memory_location, Magma_CPU);
    magma_z_mconvert( hAh, &hA, hAh.storage_type, Magma_CSR );
    magma_z_mfree(&hAh);

    magma_z_mtransfer( hA, &hAcopy, Magma_CPU, Magma_CPU );

    // in case using fill-in
    magma_zsymbilu( &hAcopy, precond->levels, &hAL, &hAUt ); 
    // add a unit diagonal to L for the algorithm
    magma_zmLdiagadd( &hAL ); 
    // transpose U for the algorithm
    magma_z_cucsrtranspose(  hAUt, &hAU );
    magma_z_mfree( &hAUt );

    // ---------------- initial guess ------------------- //
    magma_z_mconvert( hAcopy, &hACSRCOO, Magma_CSR, Magma_CSRCOO );
    magma_z_mtransfer( hACSRCOO, &dAinitguess, Magma_CPU, Magma_DEV );
    magma_z_mfree(&hACSRCOO);
    magma_z_mfree(&hAcopy);

    // transfer the factor L and U
    magma_z_mtransfer( hAL, &dL, Magma_CPU, Magma_DEV );
    magma_z_mtransfer( hAU, &dU, Magma_CPU, Magma_DEV );
    magma_z_mfree(&hAL);
    magma_z_mfree(&hAU);

    for(int i=0; i<precond->sweeps; i++){
        magma_zailu_csr_a( dAinitguess, dL, dU );
    }

    magma_z_mtransfer( dL, &hL, Magma_DEV, Magma_CPU );
    magma_z_mtransfer( dU, &hU, Magma_DEV, Magma_CPU );
    magma_z_cucsrtranspose(  hU, &hUT );

    magma_z_mfree(&dL);
    magma_z_mfree(&dU);
    magma_z_mfree(&hU);
    magma_zmlumerge( hL, hUT, &hAtmp);

    magma_z_mfree(&hL);
    magma_z_mfree(&hUT);

    magma_z_mtransfer( hAtmp, &precond->M, Magma_CPU, Magma_DEV );

    hAL.diagorder_type = Magma_UNITY;
    magma_z_mconvert(hAtmp, &hAL, Magma_CSR, Magma_CSRL);
    hAL.storage_type = Magma_CSR;
    magma_z_mconvert(hAtmp, &hAU, Magma_CSR, Magma_CSRU);
    hAU.storage_type = Magma_CSR;

    magma_z_mfree(&hAtmp);

    magma_zcsrsplit( 256, hAL, &DL, &RL );
    magma_zcsrsplit( 256, hAU, &DU, &RU );

    magma_z_mtransfer( DL, &precond->LD, Magma_CPU, Magma_DEV );
    magma_z_mtransfer( DU, &precond->UD, Magma_CPU, Magma_DEV );

    // for cusparse uncomment this
    magma_z_mtransfer( hAL, &precond->L, Magma_CPU, Magma_DEV );
    magma_z_mtransfer( hAU, &precond->U, Magma_CPU, Magma_DEV );


    //-- for ba-solve uncomment this
/*
    if( RL.nnz != 0 )
        magma_z_mtransfer( RL, &precond->L, Magma_CPU, Magma_DEV );
    else{ 
        precond->L.nnz = 0;
        precond->L.val = NULL;
        precond->L.col = NULL;
        precond->L.row = NULL;
        precond->L.blockinfo = NULL;
    }

    if( RU.nnz != 0 )
        magma_z_mtransfer( RU, &precond->U, Magma_CPU, Magma_DEV );
    else{ 
        precond->U.nnz = 0;
        precond->L.val = NULL;
        precond->L.col = NULL;
        precond->L.row = NULL;
        precond->L.blockinfo = NULL;
    }
*/
    //-- for ba-solve uncomment this

    magma_z_mfree(&hAL);
    magma_z_mfree(&hAU);
    magma_z_mfree(&DL);
    magma_z_mfree(&RL);
    magma_z_mfree(&DU);
    magma_z_mfree(&RU);

    // CUSPARSE context //
    cusparseHandle_t cusparseHandle;
    cusparseStatus_t cusparseStatus;

    cusparseStatus = cusparseCreate(&cusparseHandle);
     if(cusparseStatus != 0)    printf("error in Handle.\n");

    cusparseMatDescr_t descrL;
    cusparseStatus = cusparseCreateMatDescr(&descrL);
     if(cusparseStatus != 0)    printf("error in MatrDescr.\n");

    cusparseStatus =
    cusparseSetMatType(descrL,CUSPARSE_MATRIX_TYPE_TRIANGULAR);
     if(cusparseStatus != 0)    printf("error in MatrType.\n");

    cusparseStatus =
    cusparseSetMatDiagType (descrL, CUSPARSE_DIAG_TYPE_UNIT);
     if(cusparseStatus != 0)    printf("error in DiagType.\n");

    cusparseStatus =
    cusparseSetMatIndexBase(descrL,CUSPARSE_INDEX_BASE_ZERO);
     if(cusparseStatus != 0)    printf("error in IndexBase.\n");

    cusparseStatus =
    cusparseSetMatFillMode(descrL,CUSPARSE_FILL_MODE_LOWER);
     if(cusparseStatus != 0)    printf("error in fillmode.\n");


    cusparseStatus = cusparseCreateSolveAnalysisInfo(&precond->cuinfoL); 
     if(cusparseStatus != 0)    printf("error in info.\n");

    cusparseStatus =
    cusparseZcsrsv_analysis(cusparseHandle, 
        CUSPARSE_OPERATION_NON_TRANSPOSE, precond->L.num_rows, 
        precond->L.nnz, descrL, 
        precond->L.val, precond->L.row, precond->L.col, precond->cuinfoL );
     if(cusparseStatus != 0)    printf("error in analysis.\n");

    cusparseDestroyMatDescr( descrL );

    cusparseMatDescr_t descrU;
    cusparseStatus = cusparseCreateMatDescr(&descrU);
     if(cusparseStatus != 0)    printf("error in MatrDescr.\n");

    cusparseStatus =
    cusparseSetMatType(descrU,CUSPARSE_MATRIX_TYPE_TRIANGULAR);
     if(cusparseStatus != 0)    printf("error in MatrType.\n");

    cusparseStatus =
    cusparseSetMatDiagType (descrU, CUSPARSE_DIAG_TYPE_NON_UNIT);
     if(cusparseStatus != 0)    printf("error in DiagType.\n");

    cusparseStatus =
    cusparseSetMatIndexBase(descrU,CUSPARSE_INDEX_BASE_ZERO);
     if(cusparseStatus != 0)    printf("error in IndexBase.\n");

    cusparseStatus =
    cusparseSetMatFillMode(descrU,CUSPARSE_FILL_MODE_UPPER);
     if(cusparseStatus != 0)    printf("error in fillmode.\n");

    cusparseStatus = cusparseCreateSolveAnalysisInfo(&precond->cuinfoU); 
     if(cusparseStatus != 0)    printf("error in info.\n");

    cusparseStatus =
    cusparseZcsrsv_analysis(cusparseHandle, 
        CUSPARSE_OPERATION_NON_TRANSPOSE, precond->U.num_rows, 
        precond->U.nnz, descrU, 
        precond->U.val, precond->U.row, precond->U.col, precond->cuinfoU );
     if(cusparseStatus != 0)    printf("error in analysis.\n");

    cusparseDestroyMatDescr( descrU );
    cusparseDestroy( cusparseHandle );

    return MAGMA_SUCCESS;

}


/**
    Purpose
    -------

    Performs the left triangular solves using the ILU preconditioner.

    Arguments
    ---------

    @param
    b           magma_z_vector
                RHS

    @param
    x           magma_z_vector*
                vector to precondition

    @param
    precond     magma_z_preconditioner*
                preconditioner parameters

    @ingroup magmasparse_zgepr
    ********************************************************************/

magma_int_t
magma_zapplyailu_l( magma_z_vector b, magma_z_vector *x, 
                    magma_z_preconditioner *precond ){

    magma_int_t iters = 1;
    for(int k=0; k<40; k++)
        magma_zbajac_csr( iters, precond->LD, precond->L, b, x );
           
    return MAGMA_SUCCESS;

}


/**
    Purpose
    -------

    Performs the right triangular solves using the ILU preconditioner.

    Arguments
    ---------

    @param
    b           magma_z_vector
                RHS

    @param
    x           magma_z_vector*
                vector to precondition

    @param
    precond     magma_z_preconditioner*
                preconditioner parameters

    @ingroup magmasparse_zgepr
    ********************************************************************/

magma_int_t
magma_zapplyailu_r( magma_z_vector b, magma_z_vector *x, 
                    magma_z_preconditioner *precond ){

    magma_int_t iters = 1;
    for(int k=0; k<40; k++)
        magma_zbajac_csr( iters, precond->UD, precond->U, b, x );

    return MAGMA_SUCCESS;

}





/**
    Purpose
    -------

    Prepares the IC preconditioner via the asynchronous IC iteration.

    Arguments
    ---------

    @param
    A           magma_z_sparse_matrix
                input matrix A

    @param
    precond     magma_z_preconditioner*
                preconditioner parameters

    @ingroup magmasparse_zhepr
    ********************************************************************/

magma_int_t
magma_zaiccsetup( magma_z_sparse_matrix A, magma_z_preconditioner *precond ){


    magma_z_sparse_matrix hAh, hA, hAtmp, hAL, hLt, hAUt, hACSRCOO, dAinitguess, dL, DL, RL;



    // copy original matrix as CSRCOO to device
    magma_z_mtransfer(A, &hAh, A.memory_location, Magma_CPU);
    magma_z_mconvert( hAh, &hA, hAh.storage_type, Magma_CSR );
    magma_z_mfree(&hAh);

    // in case using fill-in
    magma_zsymbilu( &hA, precond->levels, &hAL, &hAUt ); 

    // need only lower triangular
    magma_z_mfree(&hAUt);
    magma_z_mfree(&hAL);
    magma_z_mconvert( hA, &hAtmp, Magma_CSR, Magma_CSRL );
    magma_z_mfree(&hA);

    // ---------------- initial guess ------------------- //
    magma_z_mconvert( hAtmp, &hACSRCOO, Magma_CSR, Magma_CSRCOO );
    int blocksize = 1;
    //magma_zmreorder( hACSRCOO, n, blocksize, blocksize, blocksize, &hAinitguess );
    magma_z_mtransfer( hACSRCOO, &dAinitguess, Magma_CPU, Magma_DEV );
    magma_z_mfree(&hACSRCOO);
    magma_z_mtransfer( hAtmp, &dL, Magma_CPU, Magma_DEV );
    magma_z_mfree(&hAtmp);

    for(int i=0; i<precond->sweeps; i++){
        magma_zaic_csr_a( dAinitguess, dL );
    }
    magma_z_mtransfer( dL, &hAL, Magma_DEV, Magma_CPU );
    magma_z_mfree(&dL);
    magma_z_mfree(&dAinitguess);

    // for CUSPARSE
    magma_z_mtransfer( hAL, &precond->M, Magma_CPU, Magma_DEV );

    magma_zcsrsplit( 256, hAL, &DL, &RL );

    magma_z_mtransfer( DL, &precond->LD, Magma_CPU, Magma_DEV );
    magma_z_mtransfer( RL, &precond->L, Magma_CPU, Magma_DEV );

    // for asynchronous iteration also need U
    magma_z_cucsrtranspose(   hAL, &hLt );

    magma_zcsrsplit( 256, hLt, &DL, &RL );

    magma_z_mtransfer( DL, &precond->UD, Magma_CPU, Magma_DEV );
    magma_z_mtransfer( RL, &precond->U, Magma_CPU, Magma_DEV );

    magma_z_mfree(&hAL);
    magma_z_mfree(&hLt);

    magma_z_mfree(&DL);
    magma_z_mfree(&RL);


    // CUSPARSE context //
    cusparseHandle_t cusparseHandle;
    cusparseStatus_t cusparseStatus;
    cusparseStatus = cusparseCreate(&cusparseHandle);
     if(cusparseStatus != 0)    printf("error in Handle.\n");

    cusparseMatDescr_t descrL;
    cusparseStatus = cusparseCreateMatDescr(&descrL);
     if(cusparseStatus != 0)    printf("error in MatrDescr.\n");

    cusparseStatus =
    cusparseSetMatType(descrL,CUSPARSE_MATRIX_TYPE_TRIANGULAR);
     if(cusparseStatus != 0)    printf("error in MatrType.\n");

    cusparseStatus =
    cusparseSetMatDiagType (descrL, CUSPARSE_DIAG_TYPE_NON_UNIT);
     if(cusparseStatus != 0)    printf("error in DiagType.\n");

    cusparseStatus =
    cusparseSetMatIndexBase(descrL,CUSPARSE_INDEX_BASE_ZERO);
     if(cusparseStatus != 0)    printf("error in IndexBase.\n");

    cusparseStatus =
    cusparseSetMatFillMode(descrL,CUSPARSE_FILL_MODE_LOWER);
     if(cusparseStatus != 0)    printf("error in fillmode.\n");


    cusparseStatus = cusparseCreateSolveAnalysisInfo(&precond->cuinfoL); 
     if(cusparseStatus != 0)    printf("error in info.\n");

    cusparseStatus =
    cusparseZcsrsv_analysis(cusparseHandle, 
        CUSPARSE_OPERATION_NON_TRANSPOSE, precond->M.num_rows, 
        precond->M.nnz, descrL, 
        precond->M.val, precond->M.row, precond->M.col, precond->cuinfoL );
     if(cusparseStatus != 0)    printf("error in analysis L.\n");

    cusparseDestroyMatDescr( descrL );

    cusparseMatDescr_t descrU;
    cusparseStatus = cusparseCreateMatDescr(&descrU);
     if(cusparseStatus != 0)    printf("error in MatrDescr.\n");

    cusparseStatus =
    cusparseSetMatType(descrU,CUSPARSE_MATRIX_TYPE_TRIANGULAR);
     if(cusparseStatus != 0)    printf("error in MatrType.\n");

    cusparseStatus =
    cusparseSetMatDiagType (descrU, CUSPARSE_DIAG_TYPE_NON_UNIT);
     if(cusparseStatus != 0)    printf("error in DiagType.\n");

    cusparseStatus =
    cusparseSetMatIndexBase(descrU,CUSPARSE_INDEX_BASE_ZERO);
     if(cusparseStatus != 0)    printf("error in IndexBase.\n");

    cusparseStatus =
    cusparseSetMatFillMode(descrU,CUSPARSE_FILL_MODE_LOWER);
     if(cusparseStatus != 0)    printf("error in fillmode.\n");

    cusparseStatus = cusparseCreateSolveAnalysisInfo(&precond->cuinfoU); 
     if(cusparseStatus != 0)    printf("error in info.\n");

    cusparseStatus =
    cusparseZcsrsv_analysis(cusparseHandle, 
        CUSPARSE_OPERATION_TRANSPOSE, precond->M.num_rows, 
        precond->M.nnz, descrU, 
        precond->M.val, precond->M.row, precond->M.col, precond->cuinfoU );
     if(cusparseStatus != 0)    printf("error in analysis U.\n");

    cusparseDestroyMatDescr( descrU );
    cusparseDestroy( cusparseHandle );

    return MAGMA_SUCCESS;

}
