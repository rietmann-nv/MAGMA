/*
    -- MAGMA (version 1.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2010

       @precisions normal z -> c d s

*/

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cublas.h>

// includes, project
#include "magma.h"
#include "magma_lapack.h"
#include "testings.h"
#define PRECISION_z

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing zgesvd
*/
int main( int argc, char** argv) 
{
    TESTING_CUDA_INIT();

    cuDoubleComplex *h_A, *h_R, *U, *VT, *h_work;
    double *S1, *S2;
#if defined(PRECISION_z) || defined(PRECISION_c)
    double *rwork;
#endif
    double gpu_time, cpu_time;

    magma_timestr_t start, end;

    /* Matrix size */
    magma_int_t M = 0, N=0, n2, min_mn;
    magma_int_t size[8] = {1024,2048,3072,4032,5184,6016,7040,8064};

    magma_int_t i, j, info;
    magma_int_t ione     = 1;
    magma_int_t ISEED[4] = {0,0,0,1};

    if (argc != 1){
        for(i = 1; i<argc; i++){
            if (strcmp("-N", argv[i])==0)
                N = atoi(argv[++i]);
	    else if (strcmp("-M", argv[i])==0)
	      M = atoi(argv[++i]);
        }
        if (M>0 && N>0)
	  printf("  testing_zgesvd -M %d -N %d\n\n", M, N);
        else
            {
                printf("\nUsage: \n");
                printf("  testing_zgesvd -M %d -N %d\n\n", 1024, 1024);

		/* Shutdown */
		TESTING_CUDA_FINALIZE();
                exit(1);
            }
    }
    else {
        printf("\nUsage: \n");
        printf("  testing_zgesvd -M %d -N %d\n\n", 1024, 1024);
        M = N = size[7];
    }

    n2  = M * N;
    min_mn = min(M, N);

    /* Allocate host memory for the matrix */
    TESTING_MALLOC(h_A, cuDoubleComplex,  n2);
    TESTING_MALLOC( VT, cuDoubleComplex, N*N);
    TESTING_MALLOC(  U, cuDoubleComplex, M*M);
    TESTING_MALLOC( S1, double,       min_mn);
    TESTING_MALLOC( S2, double,       min_mn);

#if defined(PRECISION_z) || defined(PRECISION_c)
    TESTING_MALLOC(rwork, double,   5*min_mn);
#endif
    TESTING_HOSTALLOC(h_R, cuDoubleComplex, n2);

    magma_int_t nb = 128; // magma_get_zgesvd_nb(N);
    magma_int_t lwork = (2*min_mn + max(M,N))*nb;

    TESTING_HOSTALLOC(h_work, cuDoubleComplex, lwork);

    printf("\n\n");
    printf("  N     CPU Time(s)    GPU Time(s)     ||R||_F / ||A||_F\n");
    printf("==========================================================\n");
    for(i=0; i<8; i++){
        if (argc==1){
            M = N = size[i];
            n2 = M*N;
        }

        /* Initialize the matrix */
        lapackf77_zlarnv( &ione, ISEED, &n2, h_A );
        lapackf77_zlacpy( MagmaUpperLowerStr, &M, &N, h_A, &M, h_R, &M );

#if defined(PRECISION_z) || defined(PRECISION_c)
	magma_zgesvd('A', 'A', M, N,
		     h_R, M, S1, U, M,
		     VT, N, h_work, lwork, rwork, &info); 
#else
	magma_zgesvd('A', 'A', M, N,
		     h_R, M, S1, U, M,
		     VT, N, h_work, lwork, &info); 
#endif
        for(j=0; j<n2; j++)
            h_R[j] = h_A[j];

        /* ====================================================================
           Performs operation using MAGMA
           =================================================================== */
        start = get_current_time();
#if defined(PRECISION_z) || defined(PRECISION_c)
	magma_zgesvd('A', 'A', M, N,
                     h_R, M, S1, U, M,
                     VT, N, h_work, lwork, rwork, &info);
#else
	magma_zgesvd('A', 'A', M, N,
		     h_R, M, S1, U, M,
		     VT, N, h_work, lwork, &info); 
#endif
        end = get_current_time();

        gpu_time = GetTimerValue(start,end)/1000.;

        /* =====================================================================
           Performs operation using LAPACK
           =================================================================== */
        start = get_current_time();
#if defined(PRECISION_z) || defined(PRECISION_c)
	lapackf77_zgesvd("A", "A", &M, &N,
			 h_A, &M, S2, U, &M,
			 VT, &N, h_work, &lwork, rwork, &info);
#else
	lapackf77_zgesvd("A", "A", &M, &N,
			 h_A, &M, S2, U, &M,
			 VT, &N, h_work, &lwork, &info);
#endif
        end = get_current_time();
        if (info < 0)
            printf("Argument %d of zgesvd had an illegal value.\n", -info);

        cpu_time = GetTimerValue(start,end)/1000.;

        /* =====================================================================
           Check the result compared to LAPACK
           =================================================================== */
        double work[1], matnorm = 1., mone = -1;
        magma_int_t one = 1;

        matnorm = lapackf77_dlange("f", &min_mn, &one, S1, &min_mn, work);
        blasf77_daxpy(&min_mn, &mone, S1, &one, S2, &one);

        printf("%5d     %6.2f         %6.2f         %e\n",
               N, cpu_time, gpu_time,
               lapackf77_dlange("f", &min_mn, &one, S2, &min_mn, work) / matnorm);

        if (argc != 1)
            break;
    }

    /* Memory clean up */
    TESTING_FREE(       h_A);
    TESTING_FREE(        VT);
    TESTING_FREE(        S1);
    TESTING_FREE(        S2);
#if defined(PRECISION_z) || defined(PRECISION_c)
    TESTING_FREE(     rwork);
#endif
    TESTING_FREE(         U);
    TESTING_HOSTFREE(h_work);
    TESTING_HOSTFREE(   h_R);

    /* Shutdown */
    TESTING_CUDA_FINALIZE();
}
