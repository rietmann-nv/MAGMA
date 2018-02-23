/*
    -- MAGMA (version 2.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date

       @author Hartwig Anzt

       @precisions normal z -> s d c
*/

#include "magmasparse_internal.h"
#ifdef _OPENMP
#include <omp.h>
#endif

#define PRECISION_z


/***************************************************************************//**
    Purpose
    -------

    Generates an incomplete threshold LU preconditioner via the ParILUT 
    algorithm. The strategy is to interleave a parallel fixed-point 
    iteration that approximates an incomplete factorization for a given nonzero 
    pattern with a procedure that adaptively changes the pattern. 
    Much of this algorithm has fine-grained parallelism, and can efficiently 
    exploit the compute power of shared memory architectures.

    This is the routine used in the publication by Anzt, Chow, Dongarra:
    ''ParILUT - A new parallel threshold ILU factorization''
    submitted to SIAM SISC in 2017.
    
    This version uses the default setting which adds all candidates to the
    sparsity pattern.

    This function requires OpenMP, and is only available if OpenMP is activated.
    
    The parameter list is:
    
    precond.sweeps : number of ParILUT steps
    precond.atol   : absolute fill ratio (1.0 keeps nnz count constant)


    Arguments
    ---------

    @param[in]
    A           magma_z_matrix
                input matrix A

    @param[in]
    b           magma_z_matrix
                input RHS b

    @param[in,out]
    precond     magma_z_preconditioner*
                preconditioner parameters

    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_zgepr
*******************************************************************************/
extern "C"
magma_int_t
magma_zparilut_gpu(
    magma_z_matrix A,
    magma_z_matrix b,
    magma_z_preconditioner *precond,
    magma_queue_t queue)
{
    magma_int_t info = 0;
    
#ifdef _OPENMP

    real_Double_t start, end;
    real_Double_t t_rm=0.0, t_add=0.0, t_res=0.0, t_sweep1=0.0, t_sweep2=0.0, 
        t_cand=0.0, t_transpose1=0.0, t_transpose2=0.0, t_selectrm=0.0,
        t_selectadd=0.0, t_nrm=0.0, t_total = 0.0, accum=0.0;
                    
    double sum, sumL, sumU;

    magma_z_matrix hA={Magma_CSR}, hAT={Magma_CSR}, hL={Magma_CSR}, 
        hU={Magma_CSR}, oneL={Magma_CSR}, oneU={Magma_CSR},
        L={Magma_CSR}, U={Magma_CSR}, L_new={Magma_CSR}, U_new={Magma_CSR}, 
        UT={Magma_CSR}, L0={Magma_CSR}, U0={Magma_CSR};
    magma_z_matrix dA={Magma_CSR}, dL={Magma_CSR}, dhL={Magma_CSR}, dU={Magma_CSR}, dUT={Magma_CSR}, dhU={Magma_CSR}, dL0={Magma_CSR}, dU0={Magma_CSR}, dLt={Magma_CSR}, dUt={Magma_CSR} ; 
    magma_int_t num_rmL, num_rmU;
    double thrsL = 0.0;
    double thrsU = 0.0;

    magma_int_t num_threads = 1, timing = 1; // print timing
    magma_int_t L0nnz, U0nnz;

    #pragma omp parallel
    {
        num_threads = omp_get_max_threads();
    }
    
    CHECK(magma_zmtransfer(A, &hA, A.memory_location, Magma_CPU, queue));
    
    // in case using fill-in
    if (precond->levels > 0) {
        CHECK(magma_zsymbilu(&hA, precond->levels, &hL, &hU , queue));
        magma_zmfree(&hU, queue);
        magma_zmfree(&hL, queue);
    }
    CHECK(magma_zmtransfer(hA, &dA, Magma_CPU, Magma_DEV, queue));
    CHECK(magma_zmatrix_tril(hA, &L0, queue));
    CHECK(magma_zmatrix_triu(hA, &U0, queue));
    CHECK(magma_zmtransfer(L0, &dL0, Magma_CPU, Magma_DEV, queue));
    CHECK(magma_zmtransfer(U0, &dU0, Magma_CPU, Magma_DEV, queue));
    magma_zmfree(&hU, queue);
    magma_zmfree(&hL, queue);
    CHECK(magma_zmatrix_tril(hA, &L, queue));
    CHECK(magma_zmtranspose(hA, &hAT, queue));
    CHECK(magma_zmatrix_tril(hAT, &U, queue));
    CHECK(magma_zmatrix_addrowindex(&L, queue)); 
    CHECK(magma_zmatrix_addrowindex(&U, queue)); 
    L.storage_type = Magma_CSRCOO;
    U.storage_type = Magma_CSRCOO;
    CHECK(magma_zmtransfer(L, &dL, Magma_CPU, Magma_DEV, queue));
    CHECK(magma_zmtransfer(U, &dU, Magma_CPU, Magma_DEV, queue));
    L0nnz=L.nnz;
    U0nnz=U.nnz;
    oneL.memory_location = Magma_CPU;
    oneU.memory_location = Magma_CPU;
        
    if (timing == 1) {
        printf("ilut_fill_ratio = %.6f;\n\n", precond->atol); 
        printf("performance_%d = [\n%%iter L.nnz U.nnz    ILU-Norm    candidat  resid     ILU-norm  selectad  add       transp1   sweep1  selectrm  remove    sweep2    transp2   total       accum\n", 
            (int) num_threads);
    }

    //##########################################################################

    for (magma_int_t iters =0; iters<precond->sweeps; iters++) {
        t_rm=0.0; t_add=0.0; t_res=0.0; t_sweep1=0.0; t_sweep2=0.0; t_cand=0.0;
        t_transpose1=0.0; t_transpose2=0.0; t_selectrm=0.0;
        t_selectadd=0.0; t_nrm=0.0; t_total = 0.0;
     
        // step 1: find candidates
        start = magma_sync_wtime(queue);
        magma_zmfree(&dUT, queue);
        dU.storage_type = Magma_CSR;
        CHECK(magma_zmtranspose( dU, &dUT, queue));
        end = magma_sync_wtime(queue); t_transpose1+=end-start;
        start = magma_sync_wtime(queue);
        CHECK(magma_zparilut_candidates_gpu(dL0, dU0, dL, dUT, &dhL, &dhU, queue));
        
        
     //  {
     //      magma_z_matrix hLr={Magma_CSR}, hUr={Magma_CSR};
     //      magma_zmtransfer(dhL, &hLr, Magma_DEV, Magma_CPU, queue);
     //      magma_zmtransfer(dhU, &hUr, Magma_DEV, Magma_CPU, queue);
     //      const char *filename1 = "gpu_dL1.mtx";
     //      magma_zwrite_csrtomtx( hLr, filename1, queue );
     //      const char *filename2 = "gpu_dU1.mtx";
     //      magma_zwrite_csrtomtx( hUr, filename2, queue );
     //      
     //      printf("candU\n\n");
     //      {
     //        for(int tt=0; tt<10; tt++){
     //         printf("rowptr[%d] = %d\n", tt, hUr.row[tt]);
     //         for(int ttt=hUr.row[tt]; ttt<hUr.row[tt+1]; ttt++) {
     //             printf("\t%2d", hUr.col[ttt]);
     //         }
     //         printf("\n");
     //        for(int ttt=hUr.row[tt]; ttt<hUr.row[tt+1]; ttt++) {
     //             printf("\t%.2f", hUr.val[ttt]);
     //         }
     //         printf("\n");
     //        }
     //        
     //      }
     //      
     //      printf("\n\n");
     //      
     //  }

        
        // {
        //     printf(" debug: %d\n", __LINE__);
        //     magma_z_matrix hLr={Magma_CSR}, hUr={Magma_CSR};
        //     magma_z_matrix hLt={Magma_CSR}, hUt={Magma_CSR};
        //     magma_zmtransfer(dL, &hLr, Magma_DEV, Magma_CPU, queue);
        //     magma_zmtransfer(dUT, &hUr, Magma_DEV, Magma_CPU, queue);
        //     printf(" debug: %d\n", __LINE__);
        //     CHECK(magma_zparilut_candidates(L0, U0, hLr, hUr, &hLt, &hUt, queue));
        //     printf(" debug: %d\n", __LINE__);
        //     hLt.storage_type = Magma_CSRCOO;
        //     hUt.storage_type = Magma_CSRCOO;
        //     magma_zmtransfer(hLt, &dhL, Magma_CPU, Magma_DEV, queue);
        //     magma_zmtransfer(hUt, &dhU, Magma_CPU, Magma_DEV, queue);
        //     printf(" debug: %d\n", __LINE__);
        //     magma_zmfree( &hLt, queue );
        //     magma_zmfree( &hUt, queue );
        //     printf(" debug: %d\n", __LINE__);
        // }

        dhL.storage_type = Magma_CSRCOO;
        dhU.storage_type = Magma_CSRCOO;
        end = magma_sync_wtime(queue); t_cand=+end-start;
        
        // step 2: compute residuals (optional when adding all candidates)
        start = magma_sync_wtime(queue);
        CHECK(magma_zparilut_residuals_gpu(dA, dL, dU, &dhL, queue));
        CHECK(magma_zparilut_residuals_gpu(dA, dL, dU, &dhU, queue));
        magma_zmfree(&hL, queue);
        magma_zmfree(&hU, queue);
        dhL.storage_type = Magma_CSRCOO;
        dhU.storage_type = Magma_CSRCOO;
        end = magma_sync_wtime(queue); t_res=+end-start;
        start = magma_sync_wtime(queue);
        sumL = magma_dznrm2( dhL.nnz, dhL.dval, 1, queue );
        sumU = magma_dznrm2( dhU.nnz, dhU.dval, 1, queue );
        sum = sumL + sumU;
        end = magma_sync_wtime(queue); t_nrm+=end-start;
        

        
        
        // step 3: add candidates
        start = magma_sync_wtime(queue);
        CHECK(magma_zcsr_sort_gpu( &dhL, queue));
        CHECK(magma_zcsr_sort_gpu( &dhU, queue));
      //  {
      //      magma_z_matrix hLr={Magma_CSR}, hUr={Magma_CSR};
      //      magma_zmtransfer(dhL, &hLr, Magma_DEV, Magma_CPU, queue);
      //      magma_zmtransfer(dhU, &hUr, Magma_DEV, Magma_CPU, queue);
      //      const char *filename1 = "gpu_dL3.mtx";
      //      magma_zwrite_csrtomtx( hLr, filename1, queue );
      //      const char *filename2 = "gpu_dU3.mtx";
      //      magma_zwrite_csrtomtx( hUr, filename2, queue );
      //      printf("cand sorted\n\n");
      //      {
      //        for(int tt=0; tt<10; tt++){
      //         printf("rowptr[%d] = %d\n", tt, hUr.row[tt]);
      //         for(int ttt=hUr.row[tt]; ttt<hUr.row[tt+1]; ttt++) {
      //             printf("\t%4d", hUr.col[ttt]);
      //         }
      //         printf("\n");
      //        for(int ttt=hUr.row[tt]; ttt<hUr.row[tt+1]; ttt++) {
      //             printf("\t%.2f", hUr.val[ttt]);
      //         }
      //         printf("\n");
      //        }
      //        
      //      }
      //      
      //      printf("\n\n");
      //  }
        
        magma_zmfree(&dLt, queue);
        magma_zmfree(&dUt, queue);
        dhU.storage_type = Magma_CSR;
        CHECK(magma_zmtranspose( dhU, &dUt, queue));
        dhU.memory_location = Magma_DEV;
        dhL.memory_location = Magma_DEV;
        dUt.memory_location = Magma_DEV;
        dLt.memory_location = Magma_DEV;
        dhL.storage_type = Magma_CSRCOO;
        dhU.storage_type = Magma_CSRCOO;
        dL.storage_type = Magma_CSRCOO;
        dU.storage_type = Magma_CSRCOO;
        dLt.storage_type = Magma_CSRCOO;
        //dUt.storage_type = Magma_CSRCOO;
        CHECK(magma_zmatrix_swap(&dhL, &dLt, queue));
        
        magma_zmfree(&dhL, queue);
        magma_zmfree(&dhU, queue);
        end = magma_sync_wtime(queue); t_selectadd+=end-start;
        start = magma_sync_wtime(queue);
        //dL.storage_type = Magma_CSRCOO;
        //dU.storage_type = Magma_CSRCOO;
        //dLt.storage_type = Magma_CSRCOO;
        //dUt.storage_type = Magma_CSRCOO;
     // {
     //     magma_z_matrix hLr={Magma_CSR}, hUr={Magma_CSR};
     //     magma_zmtransfer(dLt, &hLr, Magma_DEV, Magma_CPU, queue);
     //     const char *filename1 = "gpu_dL4.mtx";
     //     magma_zwrite_csrtomtx( hLr, filename1, queue );
     //     
     //     magma_zmtransfer(dUt, &hUr, Magma_DEV, Magma_CPU, queue);
     //     const char *filename2 = "gpu_dU4.mtx";
     //     magma_zwrite_csrtomtx( hUr, filename2, queue );
     //     
     //     printf("cand sorted transpose\n\n");
     //     {
     //       for(int tt=0; tt<10; tt++){
     //        printf("rowptr[%d] = %d\n", tt, hUr.row[tt]);
     //        for(int ttt=hUr.row[tt]; ttt<hUr.row[tt+1]; ttt++) {
     //            printf("\t%4d", hUr.col[ttt]);
     //        }
     //        printf("\n");
     //       for(int ttt=hUr.row[tt]; ttt<hUr.row[tt+1]; ttt++) {
     //            printf("\t%.2f", hUr.val[ttt]);
     //        }
     //        printf("\n");
     //       }
     //       
     //     }
     //     
     //     printf("\n\n");
     // }
     printf("%d\n\n",__LINE__);
        CHECK(magma_zmatrix_cup_gpu(dL, dLt, &dhL, queue));   
        CHECK(magma_zmatrix_cup_gpu(dU, dUt, &dhU, queue));
        dhL.storage_type = Magma_CSRCOO;
        dhU.storage_type = Magma_CSRCOO;
        CHECK(magma_zmatrix_swap(&dhL, &dL, queue));
        CHECK(magma_zmatrix_swap(&dhU, &dU, queue));
        magma_zmfree(&dhL, queue);
        magma_zmfree(&dhU, queue);
        magma_zmfree(&dLt, queue);
        magma_zmfree(&dUt, queue);
        end = magma_sync_wtime(queue); t_add=+end-start;
      //         {
      //     magma_z_matrix hLr={Magma_CSR}, hUr={Magma_CSR};
      //     magma_zmtransfer(dL, &hLr, Magma_DEV, Magma_CPU, queue);
      //     printf("\n\n");
      //     {
      //       for(int tt=0; tt<10; tt++){
      //        printf("rowptr[%d] = %d\n", tt, hLr.row[tt]);
      //        for(int ttt=hLr.row[tt]; ttt<hLr.row[tt+1]; ttt++) {
      //            printf("\t%4d", hLr.col[ttt]);
      //        }
      //        printf("\n");
      //       for(int ttt=hLr.row[tt]; ttt<hLr.row[tt+1]; ttt++) {
      //            printf("\t%.2f", hLr.val[ttt]);
      //        }
      //        printf("\n");
      //       }
      //       
      //     }
      //     printf("\n\n");
      //     
      //     const char *filename1 = "gpu_dL5.mtx";
      //     magma_zwrite_csrtomtx( hLr, filename1, queue );
      // }
        
        // step 4: sweep
        start = magma_sync_wtime(queue);
        // // GPU kernel
        CHECK(magma_zparilut_sweep_gpu(&dA, &dL, &dU, queue));
        end = magma_sync_wtime(queue); t_sweep1+=end-start;
        
     //   {
     //       magma_z_matrix hLr={Magma_CSR}, hUr={Magma_CSR};
     //       magma_zmtransfer(dL, &hLr, Magma_DEV, Magma_CPU, queue);
     //       magma_zmtransfer(dU, &hUr, Magma_DEV, Magma_CPU, queue);
     //       const char *filename1 = "gpu_dL2.mtx";
     //       magma_zwrite_csrtomtx( hLr, filename1, queue );
     //       const char *filename2 = "gpu_dU2.mtx";
     //       magma_zwrite_csrtomtx( hUr, filename2, queue );
     //       
     //       printf("after sweep nnz: %d %d\n", dL.nnz, dU.nnz);
     //   }
        
        // step 5: select threshold to remove elements
        // CHECK(magma_zmtransfer(dL, &L_new, Magma_DEV, Magma_CPU, queue));
        // CHECK(magma_zmtransfer(dU, &U_new, Magma_DEV, Magma_CPU, queue));
        start = magma_sync_wtime(queue);
        num_rmL = max((dL.nnz-L0nnz*(1+(precond->atol-1.)
            *(iters+1)/precond->sweeps)), 0);
        num_rmU = max((dU.nnz-U0nnz*(1+(precond->atol-1.)
            *(iters+1)/precond->sweeps)), 0);
        // pre-select: ignore the diagonal entries
        CHECK(magma_zpreselect_gpu(0, &dL, &oneL, queue));
        CHECK(magma_zpreselect_gpu(0, &dU, &oneU, queue));
        printf("%d\n\n",__LINE__);
        if (num_rmL>0) {
            magma_z_matrix hLr={Magma_CSR};
            hLr.nnz = oneL.nnz;
            magma_zmalloc_cpu( &hLr.val, hLr.nnz );
            magma_zgetvector( oneL.nnz , oneL.dval, 1, hLr.val, 1, queue );
            
            CHECK(magma_zparilut_set_thrs_randomselect(num_rmL, 
                &hLr, 0, &thrsL, queue));
            magma_zmfree(&hLr, queue);
            //CHECK(magma_zthrsholdselect(1, oneL.nnz, num_rmL, 
              //  oneL.dval, &thrsL, queue));
        } else {
            thrsL = 0.0;
        }
        if (num_rmU>0) {
            magma_z_matrix hUr={Magma_CSR};
            hUr.nnz = oneU.nnz;
            magma_zmalloc_cpu( &hUr.val, hUr.nnz );
            magma_zgetvector( oneU.nnz , oneU.dval, 1, hUr.val, 1, queue );
            
            CHECK(magma_zparilut_set_thrs_randomselect(num_rmL, 
                &hUr, 0, &thrsU, queue));
            magma_zmfree(&hUr, queue);
            //CHECK(magma_zthrsholdselect(1, oneU.nnz, num_rmU, 
            //    oneU.dval, &thrsU, queue));
        } else {
            thrsU = 0.0;
        }
        printf("%d\n\n",__LINE__);
        magma_zmfree(&oneL, queue);
        magma_zmfree(&oneU, queue);
        // magma_zmfree(&L_new, queue);
        // magma_zmfree(&U_new, queue);
        end = magma_sync_wtime(queue); t_selectrm=end-start;
        
        // step 6: remove elements
        start = magma_sync_wtime(queue);
        // GPU kernel
        CHECK(magma_zthrsholdrm_gpu(1, &dL, &thrsL, queue));
        CHECK(magma_zthrsholdrm_gpu(1, &dU, &thrsU, queue));
        printf("%d\n\n",__LINE__);
        // GPU kernel
        end = magma_sync_wtime(queue); t_rm=end-start;
        
        // step 7: sweep
        start = magma_sync_wtime(queue);
        // GPU kernel
        CHECK(magma_zparilut_sweep_gpu(&dA, &dL, &dU, queue));
        // end GPU kernel
        end = magma_sync_wtime(queue); t_sweep2+=end-start;
        
        if (timing == 1) {
            t_total = t_cand+t_res+t_nrm+t_selectadd+t_add+t_transpose1
                +t_sweep1+t_selectrm+t_rm+t_sweep2+t_transpose2;
            accum = accum + t_total;
            printf("%5lld %5lld %5lld  %.4e   %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e  %.2e    %.2e\n",
                (long long) iters, (long long) dL.nnz, (long long) dU.nnz, 
                (double) sum, 
                t_cand, t_res, t_nrm, t_selectadd, t_add, t_transpose1, 
                t_sweep1, t_selectrm, t_rm, t_sweep2, t_transpose2, t_total, 
                accum);
            fflush(stdout);
        }
        
   //   {
   //       magma_z_matrix hLr={Magma_CSR}, hUr={Magma_CSR};
   //       magma_zmtransfer(dL, &hLr, Magma_DEV, Magma_CPU, queue);
   //       magma_zmtransfer(dU, &hUr, Magma_DEV, Magma_CPU, queue);
   //       const char *filename1 = "gpu_dL6.mtx";
   //       magma_zwrite_csrtomtx( hLr, filename1, queue );
   //       const char *filename2 = "gpu_dU6.mtx";
   //       magma_zwrite_csrtomtx( hUr, filename2, queue );
   //       
   //       printf("after sweep nnz: %d %d\n", dL.nnz, dU.nnz);
   //   }
    }
    if (timing == 1) {
        printf("]; \n");
        fflush(stdout);
    }
    //##########################################################################
    
    
    magma_zmfree(&L, queue);
    magma_zmfree(&U, queue);
    CHECK(magma_zmtransfer(dL, &L, Magma_DEV, Magma_CPU, queue));
    CHECK(magma_zmtransfer(dU, &U, Magma_DEV, Magma_CPU, queue));
    magma_zmfree(&dL, queue);
    magma_zmfree(&dU, queue);
    L.storage_type = Magma_CSR;
    U.storage_type = Magma_CSR;
    // for CUSPARSE
    CHECK(magma_zmtransfer(L, &precond->L, Magma_CPU, Magma_DEV , queue));
    CHECK(magma_zmtranspose( U, &UT, queue));
    //CHECK(magma_zcsrcoo_transpose(U, &UT, queue));
    //magma_zmtranspose(U, &UT, queue);
    CHECK(magma_zmtransfer(UT, &precond->U, Magma_CPU, Magma_DEV , queue));
    
    if (precond->trisolver == 0 || precond->trisolver == Magma_CUSOLVE) {
        CHECK(magma_zcumilugeneratesolverinfo(precond, queue));
    } else {
        //prepare for iterative solves
        // extract the diagonal of L into precond->d
        CHECK(magma_zjacobisetup_diagscal(precond->L, &precond->d, queue));
        CHECK(magma_zvinit(&precond->work1, Magma_DEV, hA.num_rows, 1, 
            MAGMA_Z_ZERO, queue));
        // extract the diagonal of U into precond->d2
        CHECK(magma_zjacobisetup_diagscal(precond->U, &precond->d2, queue));
        CHECK(magma_zvinit(&precond->work2, Magma_DEV, hA.num_rows, 1, 
            MAGMA_Z_ZERO, queue));
    }

cleanup:
    magma_zmfree(&hA, queue);
    magma_zmfree(&hAT, queue);
    magma_zmfree(&L, queue);
    magma_zmfree(&U, queue);
    magma_zmfree(&UT, queue);
    magma_zmfree(&L0, queue);
    magma_zmfree(&U0, queue);
    magma_zmfree(&L_new, queue);
    magma_zmfree(&U_new, queue);
    magma_zmfree(&hL, queue);
    magma_zmfree(&hU, queue);
#endif
    return info;
}
