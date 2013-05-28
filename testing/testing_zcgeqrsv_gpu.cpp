/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011

       @precisions mixed zc -> ds

*/

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <cuda_runtime_api.h>
#include <cublas.h>

// includes, project
#include "flops.h"
#include "magma.h"
#include "magma_lapack.h"
#include "testings.h"

#define PRECISION_z

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing zcgeqrsv
*/
int main( int argc, char** argv)
{
    TESTING_INIT();

    real_Double_t   gflops, gpu_perf, gpu_time, cpu_perf, cpu_time, gpu_perfd, gpu_perfs;
    double           gpu_error, cpu_error, Anorm, work[1];
    cuDoubleComplex c_one     = MAGMA_Z_ONE;
    cuDoubleComplex c_neg_one = MAGMA_Z_NEG_ONE;
    cuDoubleComplex *h_A, *h_A2, *h_B, *h_X, *h_R;
    cuDoubleComplex *d_A, *d_B, *d_X, *d_T;
    cuFloatComplex  *d_SA, *d_SB, *d_ST;
    cuDoubleComplex *h_workd, *tau, tmp[1];
    cuFloatComplex  *h_works, *tau_s;
    magma_int_t lda,  ldb, lhwork, lworkgpu;
    magma_int_t ldda, lddb, lddx;
    magma_int_t M, N, nrhs, qrsv_iters, info, size, min_mn, max_mn, nb;
    magma_int_t ione     = 1;
    magma_int_t ISEED[4] = {0,0,0,1};

    printf("Epsilon(double): %8.6e\n"
           "Epsilon(single): %8.6e\n\n",
           lapackf77_dlamch("Epsilon"), lapackf77_slamch("Epsilon") );

    magma_opts opts;
    parse_opts( argc, argv, &opts );
    
    nrhs = opts.nrhs;
    
    printf("                    CPU Gflop/s   GPU  Gflop/s                  ||b-Ax|| / (N||A||)\n");
    printf("    M     N  NRHS    double        double    single     mixed   CPU        GPU        Iter\n");
    printf("==============================================================================================\n");
    for( int i = 0; i < opts.ntest; ++i ) {
        for( int iter = 0; iter < opts.niter; ++iter ) {
            M = opts.msize[i];
            N = opts.nsize[i];
            if ( M < N ) {
                printf( "skipping M=%d, N=%d because M < N is not yet supported.\n", M, N );
                continue;
            }
            min_mn = min(M, N);
            max_mn = max(M, N);
            lda    = M;
            ldb    = max_mn;
            ldda   = ((M+31)/32) * 32;
            lddb   = ((max_mn+31)/32)*32;
            lddx   = ((N+31)/32) * 32;
            nb     = max( magma_get_zgeqrf_nb( M ), magma_get_cgeqrf_nb( M ) );
            gflops = (FLOPS_ZGEQRF( M, N ) + FLOPS_ZGEQRS( M, N, nrhs )) / 1e9;
            
            // query for workspace size
            lworkgpu = (M - N + nb)*(nrhs + nb) + nrhs*nb;
            
            lhwork = -1;
            lapackf77_zgels( MagmaNoTransStr, &M, &N, &nrhs,
                             h_A, &lda, h_X, &ldb, tmp, &lhwork, &info );
            lhwork = (magma_int_t) MAGMA_Z_REAL( tmp[0] );
            lhwork = max( lhwork, lworkgpu );
            
            TESTING_MALLOC( tau,     cuDoubleComplex, min_mn   );
            TESTING_MALLOC( h_A,     cuDoubleComplex, lda*N    );
            TESTING_MALLOC( h_A2,    cuDoubleComplex, lda*N    );
            TESTING_MALLOC( h_B,     cuDoubleComplex, ldb*nrhs );
            TESTING_MALLOC( h_X,     cuDoubleComplex, ldb*nrhs );
            TESTING_MALLOC( h_R,     cuDoubleComplex, ldb*nrhs );
            TESTING_MALLOC( h_workd, cuDoubleComplex, lhwork   );
            tau_s   = (cuFloatComplex*)tau;
            h_works = (cuFloatComplex*)h_workd;
            
            TESTING_DEVALLOC( d_A, cuDoubleComplex, ldda*N      );
            TESTING_DEVALLOC( d_B, cuDoubleComplex, lddb*nrhs   );
            TESTING_DEVALLOC( d_X, cuDoubleComplex, lddx*nrhs   );
            TESTING_DEVALLOC( d_T, cuDoubleComplex, ( 2*min_mn + (N+31)/32*32 )*nb );
            d_ST = (cuFloatComplex*)d_T;
            
            /* Initialize the matrices */
            size = lda*N;
            lapackf77_zlarnv( &ione, ISEED, &size, h_A );
            lapackf77_zlacpy( MagmaUpperLowerStr, &M, &N, h_A, &lda, h_A2, &lda );
            
            // make random RHS
            size = ldb*nrhs;
            lapackf77_zlarnv( &ione, ISEED, &size, h_B );
            lapackf77_zlacpy( MagmaUpperLowerStr, &M, &nrhs, h_B, &ldb, h_R, &ldb );
            
            magma_zsetmatrix( M, N,    h_A, lda, d_A, ldda );
            magma_zsetmatrix( M, nrhs, h_B, ldb, d_B, lddb );
            
            //=====================================================================
            //              Mixed Precision Iterative Refinement - GPU
            //=====================================================================
            gpu_time = magma_wtime();
            magma_zcgeqrsv_gpu( M, N, nrhs,
                                d_A, ldda, d_B, lddb,
                                d_X, lddx, &qrsv_iters, &info );
            gpu_time = magma_wtime() - gpu_time;
            gpu_perf = gflops / gpu_time;
            if (info != 0)
                printf("magma_zcgeqrsv returned error %d: %s.\n",
                       (int) info, magma_strerror( info ));
            
            // compute the residual
            magma_zgetmatrix( N, nrhs, d_X, lddx, h_X, ldb );
            blasf77_zgemm( MagmaNoTransStr, MagmaNoTransStr, &M, &nrhs, &N,
                           &c_neg_one, h_A, &lda,
                                       h_X, &ldb,
                           &c_one,     h_R, &ldb);
            Anorm = lapackf77_zlange("f", &M, &N,    h_A, &lda, work);
            
            //=====================================================================
            //                 Double Precision Solve
            //=====================================================================
            magma_zsetmatrix( M, N,    h_A, lda, d_A, ldda );
            magma_zsetmatrix( M, nrhs, h_B, ldb, d_B, lddb );
            
            gpu_time = magma_wtime();
            magma_zgels_gpu( MagmaNoTrans, M, N, nrhs, d_A, ldda,
                             d_B, lddb, h_workd, lworkgpu, &info);
            gpu_time = magma_wtime() - gpu_time;
            gpu_perfd = gflops / gpu_time;
            
            //=====================================================================
            //                 Single Precision Solve
            //=====================================================================
            magma_zsetmatrix( M, N,    h_A, lda, d_A, ldda );
            magma_zsetmatrix( M, nrhs, h_B, ldb, d_B, lddb );
            
            /* The allocation of d_SA and d_SB is done here to avoid
             * to double the memory used on GPU with zcgeqrsv */
            TESTING_DEVALLOC( d_SA, cuFloatComplex, ldda*N    );
            TESTING_DEVALLOC( d_SB, cuFloatComplex, lddb*nrhs );
            magmablas_zlag2c( M, N,    d_A, ldda, d_SA, ldda, &info );
            magmablas_zlag2c( N, nrhs, d_B, lddb, d_SB, lddb, &info );
            
            gpu_time = magma_wtime();
            magma_cgels_gpu( MagmaNoTrans, M, N, nrhs, d_SA, ldda,
                             d_SB, lddb, h_works, lhwork, &info);
            gpu_time = magma_wtime() - gpu_time;
            gpu_perfs = gflops / gpu_time;
            TESTING_DEVFREE( d_SA );
            TESTING_DEVFREE( d_SB );
            
            /* =====================================================================
               Performs operation using LAPACK
               =================================================================== */
            lapackf77_zlacpy( MagmaUpperLowerStr, &M, &nrhs, h_B, &ldb, h_X, &ldb );
            
            cpu_time = magma_wtime();
            lapackf77_zgels( MagmaNoTransStr, &M, &N, &nrhs,
                             h_A, &lda, h_X, &ldb, h_workd, &lhwork, &info );
            cpu_time = magma_wtime() - cpu_time;
            cpu_perf = gflops / cpu_time;
            if (info != 0)
                printf("lapackf77_zgels returned error %d: %s.\n",
                       (int) info, magma_strerror( info ));
            
            blasf77_zgemm( MagmaNoTransStr, MagmaNoTransStr, &M, &nrhs, &N,
                           &c_neg_one, h_A2, &lda,
                                       h_X,  &ldb,
                           &c_one,     h_B,  &ldb );
            
            cpu_error = lapackf77_zlange("f", &M, &nrhs, h_B, &ldb, work) / (min_mn*Anorm);
            gpu_error = lapackf77_zlange("f", &M, &nrhs, h_R, &ldb, work) / (min_mn*Anorm);
            
            printf("%5d %5d %5d   %7.2f       %7.2f   %7.2f   %7.2f   %8.2e   %8.2e   %3d\n",
                   (int) M, (int) N, (int) nrhs,
                   cpu_perf, gpu_perfd, gpu_perfs, gpu_perf, cpu_error, gpu_error,
                   (int) qrsv_iters );
            
            TESTING_FREE( tau  );
            TESTING_FREE( h_A  );
            TESTING_FREE( h_A2 );
            TESTING_FREE( h_B  );
            TESTING_FREE( h_X  );
            TESTING_FREE( h_R  );
            TESTING_FREE( h_workd );
            TESTING_DEVFREE( d_A );
            TESTING_DEVFREE( d_B );
            TESTING_DEVFREE( d_X );
            TESTING_DEVFREE( d_T );
        }
        if ( opts.niter > 1 ) {
            printf( "\n" );
        }
    }
    
    TESTING_FINALIZE();
    return 0;
}
