/*
 *  -- MAGMA (version 1.1) --
 *     Univ. of Tennessee, Knoxville
 *     Univ. of California, Berkeley
 *     Univ. of Colorado, Denver
 *     November 2011
 *
 * @precisions normal z -> c d s
 *
 **/
// includes, system
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cuda_runtime_api.h>
#include <cublas.h>

// includes, project
#include "flops.h"
#include "magma.h"
#include "magma_lapack.h"
#include "testings.h"

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing zgesv
*/
int main(int argc , char **argv)
{
    TESTING_CUDA_INIT();

    real_Double_t   gflops, gpu_perf, gpu_time;
    double          Rnorm, Anorm, Xnorm, *work;
    cuDoubleComplex c_one     = MAGMA_Z_ONE;
    cuDoubleComplex c_neg_one = MAGMA_Z_NEG_ONE;
    cuDoubleComplex *h_A, *h_LU, *h_B, *h_X;
    magma_int_t *ipiv;
    magma_int_t lda, ldb;
    magma_int_t i, info, szeA, szeB;
    magma_int_t ione     = 1;
    magma_int_t N        = 0;
    magma_int_t NRHS     = 100;
    magma_int_t ISEED[4] = {0,0,0,1};
    const int MAXTESTS   = 10;
    magma_int_t size[MAXTESTS] = { 1024, 2048, 3072, 4032, 5184, 6016, 7040, 8064, 9088, 10112 };
    
    // process command line arguments
    printf( "\nUsage: %s -N <matrix size> -R <right hand sides>\n", argv[0] );
    printf( "  -N can be repeated up to %d times\n\n", MAXTESTS );
    int ntest = 0;
    for( int i = 1; i < argc; ++i ) {
        if ( strcmp("-N", argv[i]) == 0 and i+1 < argc ) {
            magma_assert( ntest < MAXTESTS, "error: -N repeated more than maximum %d tests\n", MAXTESTS );
            size[ntest] = atoi( argv[++i] );
            magma_assert( size[ntest] > 0, "error: -N %s is invalid; must be > 0.\n", argv[i] );
            N = max( N, size[ntest] );
            ntest++;
        }
        else if ( strcmp("-R", argv[i]) == 0 and i+1 < argc ) {
            NRHS = atoi( argv[++i] );
            magma_assert( NRHS > 0, "error: -R %is is invalid; must be > 0.\n", argv[i] );
        }
        else {
            printf( "invalid argument: %s\n", argv[i] );
            exit(1);
        }
    }
    if ( ntest == 0 ) {
        ntest = MAXTESTS;
        N = size[ntest-1];
    }
    
    // allocate maximum amount of memory required
    lda = ldb = N;
    TESTING_MALLOC( h_A,  cuDoubleComplex, lda*N    );
    TESTING_MALLOC( h_LU, cuDoubleComplex, lda*N    );
    TESTING_MALLOC( h_B,  cuDoubleComplex, ldb*NRHS );
    TESTING_MALLOC( h_X,  cuDoubleComplex, ldb*NRHS );
    TESTING_MALLOC( work, double,          N        );
    TESTING_MALLOC( ipiv, magma_int_t,     N        );

    printf("    N   NRHS   GPU GFlop/s (sec)   ||B - AX|| / ||A||*||X||\n");
    printf("===========================================================\n");

    for( i = 0; i < ntest; ++i ) {
        N   = size[i];
        lda = ldb = N;
        gflops = ( FLOPS_ZGETRF( (double)N, (double)N ) +
                   FLOPS_ZGETRS( (double)N, (double)NRHS ) ) / 1e9;

        /* Initialize the matrices */
        szeA = lda*N;
        szeB = ldb*NRHS;
        lapackf77_zlarnv( &ione, ISEED, &szeA, h_A );
        lapackf77_zlarnv( &ione, ISEED, &szeB, h_B );
        
        // copy A to LU and B to X; save A and B for residual
        lapackf77_zlacpy( "F", &N, &N,    h_A, &lda, h_LU, &lda );
        lapackf77_zlacpy( "F", &N, &NRHS, h_B, &ldb, h_X,  &ldb );

        //=====================================================================
        // Solve Ax = b through an LU factorization
        //=====================================================================
        gpu_time = magma_wtime();
        magma_zgesv( N, NRHS, h_LU, lda, ipiv, h_X, ldb, &info );
        gpu_time = magma_wtime() - gpu_time;
        if (info != 0)
            printf("magma_zgesv returned error %d.\n", (int) info);

        gpu_perf = gflops / gpu_time;

        //=====================================================================
        // Residual
        //=====================================================================
        Anorm = lapackf77_zlange("I", &N, &N,    h_A, &lda, work);
        Xnorm = lapackf77_zlange("I", &N, &NRHS, h_X, &ldb, work);

        blasf77_zgemm( MagmaNoTransStr, MagmaNoTransStr, &N, &NRHS, &N, 
                       &c_one,     h_A, &lda, 
                                   h_X, &ldb, 
                       &c_neg_one, h_B, &ldb);
        
        Rnorm = lapackf77_zlange("I", &N, &NRHS, h_B, &ldb, work);

        printf( "%5d  %5d   %7.2f (%7.2f)   %8.2e\n",
                (int) N, (int) NRHS, gpu_perf, gpu_time, Rnorm/(Anorm*Xnorm) );
    }

    /* Memory clean up */
    TESTING_FREE( h_A  );
    TESTING_FREE( h_LU );
    TESTING_FREE( h_B  );
    TESTING_FREE( h_X  );
    TESTING_FREE( work );
    TESTING_FREE( ipiv );

    /* Shutdown */
    TESTING_CUDA_FINALIZE();
}
