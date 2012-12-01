/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011

       @author Azzam Haidar
       @author Stan Tomov

       @precisions normal z -> s d c

*/
#include "common_magma.h"
#include "trace.h"
#include <assert.h>
#if defined(USEMKL)
#include <mkl_service.h>
#endif
#if defined(USEACML)
#include <omp.h>
#endif

// === Define what BLAS to use ============================================
#define PRECISION_z

#if (defined(PRECISION_s))
     #define magma_ssyr2k magmablas_ssyr2k
#endif
// === End defining what BLAS to use ======================================


extern "C" magma_int_t
magma_zhetrd_he2hb_mgpu( char uplo, magma_int_t n, magma_int_t nb,
                    cuDoubleComplex *a, magma_int_t lda, 
                    cuDoubleComplex *tau,
                    cuDoubleComplex *work, magma_int_t lwork,
                    cuDoubleComplex *dAmgpu[], magma_int_t ldda,
                    cuDoubleComplex *dTmgpu[], magma_int_t lddt,
                    magma_int_t ngpu, magma_int_t distblk, 
                    cudaStream_t streams[][20], magma_int_t nstream, 
                    magma_int_t threads, magma_int_t *info)
{
/*  -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011

    Purpose   
    =======   
    ZHETRD_HE2HB reduces a complex Hermitian matrix A to real symmetric   
    band-diagonal form T by an orthogonal similarity transformation:   
    Q**H * A * Q = T.   
    This version stores the triangular matrices T used in the accumulated
    Householder transformations (I - V T V').

    Arguments   
    =========   
    UPLO    (input) CHARACTER*1   
            = 'U':  Upper triangle of A is stored;   
            = 'L':  Lower triangle of A is stored.   

    N       (input) INTEGER   
            The order of the matrix A.  N >= 0.   

    A       (input/output) COMPLEX_16 array, dimension (LDA,N)   
            On entry, the Hermitian matrix A.  If UPLO = 'U', the leading   
            N-by-N upper triangular part of A contains the upper   
            triangular part of the matrix A, and the strictly lower   
            triangular part of A is not referenced.  If UPLO = 'L', the   
            leading N-by-N lower triangular part of A contains the lower   
            triangular part of the matrix A, and the strictly upper   
            triangular part of A is not referenced.   
            On exit, if UPLO = 'U', the Upper band-diagonal of A is 
            overwritten by the corresponding elements of the   
            band-diagonal matrix T, and the elements above the band   
            diagonal, with the array TAU, represent the orthogonal   
            matrix Q as a product of elementary reflectors; if UPLO   
            = 'L', the the Lower band-diagonal of A is overwritten by 
            the corresponding elements of the band-diagonal   
            matrix T, and the elements below the band-diagonal, with   
            the array TAU, represent the orthogonal matrix Q as a product   
            of elementary reflectors. See Further Details.   

    LDA     (input) INTEGER   
            The leading dimension of the array A.  LDA >= max(1,N).   

    TAU     (output) COMPLEX_16 array, dimension (N-1)   
            The scalar factors of the elementary reflectors (see Further   
            Details).   

    WORK    (workspace/output) COMPLEX_16 array, dimension (MAX(1,LWORK))   
            On exit, if INFO = 0, WORK(1) returns the optimal LWORK.   

    LWORK   (input) INTEGER   
            The dimension of the array WORK.  LWORK >= 1.   
            For optimum performance LWORK >= N*NB, where NB is the   
            optimal blocksize.   

            If LWORK = -1, then a workspace query is assumed; the routine   
            only calculates the optimal size of the WORK array, returns   
            this value as the first entry of the WORK array, and no error   
            message related to LWORK is issued by XERBLA.   

    dT      (output) COMPLEX_16 array on the GPU, dimension N*NB, 
            where NB is the optimal blocksize.
            On exit dT holds the upper triangular matrices T from the 
            accumulated Householder transformations (I - V T V') used
            in the factorization. The nb x nb matrices T are ordered 
            consecutively in memory one after another.

    INFO    (output) INTEGER   
            = 0:  successful exit   
            < 0:  if INFO = -i, the i-th argument had an illegal value   

    Further Details   
    ===============   
    If UPLO = 'U', the matrix Q is represented as a product of elementary   
    reflectors   

       Q = H(n-1) . . . H(2) H(1).   

    Each H(i) has the form   

       H(i) = I - tau * v * v'

    where tau is a complex scalar, and v is a complex vector with   
    v(i+1:n) = 0 and v(i) = 1; v(1:i-1) is stored on exit in   
    A(1:i-1,i+1), and tau in TAU(i).   

    If UPLO = 'L', the matrix Q is represented as a product of elementary   
    reflectors   

       Q = H(1) H(2) . . . H(n-1).   

    Each H(i) has the form   

       H(i) = I - tau * v * v'   

    where tau is a complex scalar, and v is a complex vector with   
    v(1:i) = 0 and v(i+1) = 1; v(i+2:n) is stored on exit in A(i+2:n,i),   
    and tau in TAU(i).

    The contents of A on exit are illustrated by the following examples   
    with n = 5:   

    if UPLO = 'U':                       if UPLO = 'L':   

      (  d   e   v2  v3  v4 )              (  d                  )   
      (      d   e   v3  v4 )              (  e   d              )   
      (          d   e   v4 )              (  v1  e   d          )   
      (              d   e  )              (  v1  v2  e   d      )   
      (                  d  )              (  v1  v2  v3  e   d  )   

    where d and e denote diagonal and off-diagonal elements of T, and vi   
    denotes an element of the vector defining H(i).   
    =====================================================================    */


    #define a_ref(a_1,a_2)  ( a  + ((a_2)-1)*( lda) + (a_1)-1)
    #define da_ref(a_1,a_2) (da  + ((a_2)-1)*(ldda) + (a_1)-1)
    #define tau_ref(a_1)    (tau + (a_1)-1)
    #define t_ref(a_1)      (dT  + ((a_1)-1)*(lddt))

    #define Atest(a_1,a_2)  ( Atest  + ((a_2)-1)*( lda) + (a_1)-1)

    #define dttest(a_0, a_1, a_2)   (dTmgpu[a_0]  + ((a_2)-1)*(lddt))
    #define datest(a_0, a_1, a_2)   (dAmgpu[a_0]  + ((a_2)-1)*(ldda) + (a_1)-1)


    char uplo_[2] = {uplo, 0};




   
    cuDoubleComplex c_neg_one  = MAGMA_Z_NEG_ONE;
    cuDoubleComplex c_neg_half = MAGMA_Z_NEG_HALF;
    cuDoubleComplex c_one  = MAGMA_Z_ONE ;
    cuDoubleComplex c_zero = MAGMA_Z_ZERO;
    double  d_one = MAGMA_D_ONE;

    magma_int_t pm, pn, indi, indj, pk;
    magma_int_t pm_old=0, pn_old=0, indi_old=0, indj_old=0, flipV=-1;
    magma_int_t iblock, idev, di, indi_next;
    int i;
    int lwkopt;
    int lquery;

    assert (nstream>=3);


    *info = 0;
    int upper = lapackf77_lsame(uplo_, "U");
    lquery = lwork == -1;
    if (! upper && ! lapackf77_lsame(uplo_, "L")) {
        *info = -1;
    } else if (n < 0) {
        *info = -2;
    } else if (lda < max(1,n)) {
        *info = -4;
    } else if (lwork < 1 && ! lquery) {
        *info = -9;
    }

    if (*info == 0) {
      /* Determine the block size. */
      lwkopt = n * nb;
      MAGMA_Z_SET2REAL( work[0], lwkopt );
    }


    if (*info != 0)
      return *info;
    else if (lquery)
      return *info;

    /* Quick return if possible */
    if (n == 0) {
        work[0] = c_one;
        return *info;
    }

    magma_int_t mklth = min(threads,12);
#if defined(USEMKL)
    mkl_set_num_threads(mklth);
#endif
#if defined(USEACML)
    omp_set_num_threads(mklth);
#endif

    magma_int_t gnode[MagmaMaxGPUs][MagmaMaxGPUs+2];
    magma_int_t nbcmplx=0;
    magma_buildconnection_mgpu(gnode, &nbcmplx,  ngpu);
    printf(" Initializing communication pattern.... GPU-ncmplx %d\n\n" , nbcmplx);


    magma_device_t cdev;
    magma_getdevice( &cdev );

/*
    cuDoubleComplex *dworkmgpu[MagmaMaxGPUs], *dWmgpu[MagmaMaxGPUs];
    for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
        cudaSetDevice( dev );
        if (MAGMA_SUCCESS != magma_zmalloc( &dworkmgpu[dev], nb*ldda )) {
            *info = MAGMA_ERR_DEVICE_ALLOC;
            return *info;
        }
        if (MAGMA_SUCCESS != magma_zmalloc( &dWmgpu[dev], nb*ldda )) {
            *info = MAGMA_ERR_DEVICE_ALLOC;
            return *info;
        }
    }
        cudaSetDevice( 0 );

    // Use the first panel of da as work space 
    cuDoubleComplex *dwork = dworkmgpu[0];
    cuDoubleComplex *dW    = dWmgpu[0];
    //cuDoubleComplex *da    = damgpu[0];
    //cuDoubleComplex *dT    = dTmgpu[0];

   
 //   cuDoubleComplex *dwork, *dW;
    cuDoubleComplex *da;
    if (MAGMA_SUCCESS != magma_zmalloc( &da, n*ldda )) {
        *info = MAGMA_ERR_DEVICE_ALLOC;
        return *info;
    }

  */   
// ======================

//    cuDoubleComplex *datest[MagmaMaxGPUs];
    cuDoubleComplex *dworktest[MagmaMaxGPUs], *dworktestbis[MagmaMaxGPUs];
    cuDoubleComplex *dvtest[MagmaMaxGPUs], *dwtest[MagmaMaxGPUs];
    cuDoubleComplex *workngpu[MagmaMaxGPUs+1];
    cudaEvent_t     redevents[MagmaMaxGPUs][20]; 
    magma_int_t nbevents =2;

//    cuDoubleComplex *dttest[MagmaMaxGPUs];
//    cuDoubleComplex *Atest = (cuDoubleComplex *) malloc(n*lda*sizeof(cuDoubleComplex));
//    cuDoubleComplex *Vtest = (cuDoubleComplex *) malloc(n*nb*sizeof(cuDoubleComplex));
//    cuDoubleComplex *Wtest = (cuDoubleComplex *) malloc(n*nb*sizeof(cuDoubleComplex));
    cuDoubleComplex *worktest = (cuDoubleComplex *) malloc(n*nb*sizeof(cuDoubleComplex));



    //magma_int_t mlocal = ((n / distblk) / ngpu + 1) * distblk;
    magma_int_t lddv = n;
    magma_int_t lddw = lddv;
    for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
        cudaSetDevice( dev );
        //magma_zmalloc( &datest[dev], mlocal*ldda );
        //magma_zmalloc( &dttest[dev], mlocal*ldda );
        magma_zmalloc( &dvtest[dev], 2*nb*lddv );
        magma_zmalloc( &dwtest[dev], nb*lddw );
        magma_zmalloc( &dworktest[dev], nb*ldda );
        magma_zmalloc( &dworktestbis[dev], 3*nb*ldda );
        workngpu[dev] = (cuDoubleComplex *) malloc(n*nb*sizeof(cuDoubleComplex));
        magmablasSetKernelStream( streams[ dev ][ 0 ] );
       for( magma_int_t i = 0; i < nbevents; ++i ) {
            cudaEventCreateWithFlags(&redevents[dev][i],cudaEventDisableTiming);
       }

    }
    workngpu[ngpu] = (cuDoubleComplex *) malloc(n*nb*sizeof(cuDoubleComplex));    
    //cudaSetDevice(0  );
    // ======================

    #ifdef TRACING
    char buf[80];
    #endif


    trace_init( 1, ngpu, nstream, (cudaStream_t*) streams );
    

    cuDoubleComplex *hT = work + lwork - nb*nb;
    lwork -= nb*nb;
    memset( hT, 0, nb*nb*sizeof(cuDoubleComplex));

    if (upper) {

      printf("ZHETRD_HE2HB is not yet implemented for upper matrix storage. Exit.\n");
      exit(1);

    }else {
            /* Reduce the lower triangle of A */
        for (i = 1; i <= n-nb; i += nb) 
        {
             indi = i+nb;
             indj = i;
             pm   = n - i - nb + 1;
             //pn   = min(i+nb-1, n-nb) -i + 1;
             pn   = nb;
             
             /*   Get the current panel (no need for the 1st iteration) */
             if (i > 1 ){
                 // zpanel_to_q copy the upper oof diagonal part of 
                 // the matrix to work to be restored later. acctually
                 //  the zero's and one's putted are not used this is only
                 //   because we don't have a function that copy only the
                 //    upper part of A to be restored after copying the 
                 //    lookahead panel that has been computted from GPU to CPU. 
                 zpanel_to_q(MagmaUpper, pn-1, a_ref(i, i+1), lda, work);

                 // find the device who own the panel then send it to the CPU.
                // below a -1 was added and then a -1 was done on di because of the fortran indexing
                 iblock = ((i-1) / distblk) / ngpu;          // local block id
                 di     = iblock*distblk + (i-1)%distblk;     // local index in parent matrix
                 idev   = ((i-1) / distblk) % ngpu;          // device with this block


                 //printf("Receiving panel ofsize %d %d from idev %d A(%d,%d) \n",(pm+pn), pn,idev,i-1,di); 

                 trace_gpu_start( idev, 1, "get", "get panel" );
                 cudaSetDevice( idev );

                 /*
                 magma_zgetmatrix( (pm+pn), pn,
                                         datest(idev, i, di+1), ldda,
                                         a_ref ( i, i), lda);
                 */
                 //magma_device_sync();
                 magma_zgetmatrix_async( (pm+pn), pn,
                                         datest(idev, i, di+1), ldda,
                                         a_ref ( i, i), lda, streams[ idev ][ nstream-1 ] );
               
                 
                 /*
                 magma_device_sync();
                 cudaMemcpy2DAsync(a_ref(i,i), lda*sizeof(cuDoubleComplex),
                                  datest(idev,i,di+1), ldda*sizeof(cuDoubleComplex),
                                  (pm+pn)*sizeof(cuDoubleComplex), pn,
                                  cudaMemcpyDeviceToHost, streams[ idev ][ nstream-1 ]);

                 */
                 trace_gpu_end( idev, 1 );

                 //cudaSetDevice( 0 );
                 trace_gpu_start( 0, 2, "her2k", "her2k" );
                 //printf("updating zher2k on A(%d,%d) of size %d %d \n",indi_old+pn_old-1,indi_old+pn_old-1,pm_old-pn_old,pn_old); 
                // compute ZHER2K_MGPU
                 magmablas_zher2k_mgpu2(
                      MagmaLower, MagmaNoTrans, pm_old-pn_old, pn_old,
                      c_neg_one, dvtest, pm_old, flipV*nb*lddv+pn_old,
                                 dwtest, pm_old, pn_old,
                      d_one,     dAmgpu, ldda, indi_old+pn_old-1,
                      ngpu, distblk, streams, 2 );
                 //cudaSetDevice( 0 );

                 trace_gpu_end( 0, 2 );
                 trace_cpu_start( 0, "sync", "sync on 1" );
                 cudaSetDevice( idev );
                 magma_queue_sync( streams[idev][ nstream-1 ] );
                 //cudaSetDevice( 0 );
                 trace_cpu_end( 0 );
                 zq_to_panel(MagmaUpper, pn-1, a_ref(i, i+1), lda, work);
             }

             /* ==========================================================
                QR factorization on a panel starting nb off of the diagonal.
                Prepare the V and T matrices. 
                ==========================================================  */
             #ifdef TRACING
             snprintf( buf, sizeof(buf), "panel %d", i );
             #endif
             trace_cpu_start( 0, "geqrf", buf );
             lapackf77_zgeqrf(&pm, &pn, a_ref(indi, indj), &lda, 
                        tau_ref(i), work, &lwork, info);
             
             /* Form the matrix T */
             pk=min(pm,pn);
             lapackf77_zlarft( MagmaForwardStr, MagmaColumnwiseStr,
                           &pm, &pk, a_ref(indi, indj), &lda,
                           tau_ref(i), hT, &nb);

             /* Prepare V - put 0s in the upper triangular part of the panel
                (and 1s on the diagonal), temporaly storing the original in work */
             zpanel_to_q(MagmaUpper, pk, a_ref(indi, indj), lda, work);
             trace_cpu_end( 0 );



             /* Send V and T from the CPU to the GPU */
             // To be able to overlap the GET with the ZHER2K
             // it should be done on last stream. 
             // TO Avoid a BUG that is overwriting the old_V
             // used atthis moment by zher2k with the new_V 
             // send it now, we decide to have a flipflop 
             // vector of Vs. if step%2=0 use V[0] else use V[nb*n]
             flipV = ((i-1)/nb)%2;
             for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
                 cudaSetDevice( dev );
                 trace_gpu_start( dev, 0, "set", "set V and T" );
                // send V
                 magma_zsetmatrix_async( pm, pk,
                                     a_ref(indi, indj),  lda,
                                     &dvtest[dev][flipV*nb*lddv], pm, streams[dev][nstream-1] );

                // Send the triangular factor T to the GPU 
                magma_zsetmatrix_async( pk, pk,
                                     hT,       nb,
                                     dttest(dev, 1, i), lddt, streams[dev][nstream-1] );
                trace_gpu_end( dev, 0 );
             }

             /* ==========================================================
                Compute W:
                1. X = A (V T)
                2. W = X - 0.5* V * (T' * (V' * X)) 
                ==========================================================  */
             for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
                 // dwork = V T 
                 trace_cpu_start( 0, "sync", "sync on 0" );
                 cudaSetDevice( dev );
                 magmablasSetKernelStream( streams[ dev ][ nstream-1 ] );
                 magma_queue_sync( streams[dev][nstream-1] );
                 trace_cpu_end( 0 );
             
                 trace_gpu_start( dev, 2, "gemm", "work = V*T" );
                 magma_zgemm(MagmaNoTrans, MagmaNoTrans, pm, pk, pk,
                         c_one, &dvtest[dev][flipV*nb*lddv], pm, 
                         dttest(dev, 1, i), lddt,
                         c_zero, dworktest[dev], pm);
                 trace_gpu_end( dev, 2 );
             
                 // W = X = A*V*T = A dwork  
                 trace_gpu_start( 0, 2, "hemm", "X = A*work" );
             }

             // ===============================================
             //   SYNC TO BE SURE THAT BOTH V AND T WERE 
             //   RECEIVED AND VT IS COMPUTED and SYR2K is done
             // ===============================================
             for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
                 cudaSetDevice( dev );
                 for( magma_int_t s = 0; s < nstream; ++s ) 
                 magma_queue_sync( streams[dev][s] );
             }


              // compute ZHEMM_MGPU
              // The broadcast of the result done inside this function
              // should be done in stream [0] because i am assuming this 
              // for the GEMMs below otherwise I have to SYNC over the 
              // Broadcasting stream.
              if(ngpu==1){
                 magmablasSetKernelStream( streams[ 0 ][ 0 ] );
                 magma_zhemm(MagmaLeft, uplo, pm, pk,
                         c_one, dAmgpu[0]+(indi-1)*ldda+(indi-1), ldda,
                         dworktest[0], pm,
                         c_zero, dwtest[0], pm);
              }else{
                 /*     
                 magmablas_zhemm_mgpu(
                       MagmaLeft, uplo, pm, pk,
                       c_one, dAmgpu, ldda, indi-1,
                                   dworktest, pm,
                       c_zero,     dwtest, pm, dworktestbis, pm, worktest, pm, workngpu, pm,
                       ngpu, distblk, streams, nstream-1, redevents, nbevents);
                 */
                 magmablas_zhemm_mgpu_com(
                       MagmaLeft, uplo, pm, pk,
                       c_one, dAmgpu, ldda, indi-1,
                                   dworktest, pm,
                       c_zero,     dwtest, pm, dworktestbis, pm, worktest, pm, workngpu, pm,
                       ngpu, distblk, streams, nstream-1, redevents, nbevents, gnode, nbcmplx);
                 
                 /* 
                 // send X=AVT stored in dW to all GPUs 
                 for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
                     cudaSetDevice( dev );
                     magma_zsetmatrix_async( pm, pk,
                              worktest, pm,
                              dwtest[dev],  pm, streams[dev][0] );

                 }
                */               
             }
             trace_gpu_end( 0, 2 );

             
             /* dwork = V*T already ==> dwork' = T'*V'
              * compute T'*V'*X ==> dwork'*W ==>
              * dwork + pm*nb = ((T' * V') * X) = dwork' * X = dwork' * W */
             for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
                 // Here we have to wait until the broadcast of ZHEMM has been done.
                 // Note that the broadcast should be done on stream[0] so in a way 
                 // we can continue here on the same stream and avoid a sync    
                 cudaSetDevice( dev );
                 magmablasSetKernelStream( streams[ dev ][ 0 ] );
                 // magma_queue_sync( streams[dev][0] );
                 trace_gpu_start( dev, 2, "gemm", "work = T'*V'*X" );
                 magma_zgemm(MagmaConjTrans, MagmaNoTrans, pk, pk, pm,
                             c_one, dworktest[dev], pm, 
                             dwtest[dev], pm,
                             c_zero, dworktestbis[dev], nb);
                 trace_gpu_end( dev, 2 );
                 
                 /* W = X - 0.5 * V * T'*V'*X
                  *   = X - 0.5 * V * (dwork + pm*nb) = W - 0.5 * V * (dwork + pm*nb) */
                 trace_gpu_start( dev, 2, "gemm", "W = X - 0.5*V*(T'*V'*X)" );
                 magma_zgemm(MagmaNoTrans, MagmaNoTrans, pm, pk, pk,
                             c_neg_half, &dvtest[dev][flipV*nb*lddv], pm,
                             dworktestbis[dev], nb, 
                             c_one,     dwtest[dev], pm);
                 trace_gpu_end( dev, 2 );
             }
             /* restore the panel it is put here to overlap with the previous GEMM*/
             zq_to_panel(MagmaUpper, pk, a_ref(indi, indj), lda, work);
             // ===============================================
             //   SYNC TO BE SURE THAT BOTH V AND W ARE DONE
             // ===============================================
             // Synchronise to be sure that W has been computed 
             // because ZHER2K use streaming and may happen 
             // that lunch a gemm on stream 2 while stream 0
             // which compute those 2 GEMM above has not been
             // computed and also used for the same reason in
             // the panel update below and also for the last HER2K
             for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
                 cudaSetDevice( dev );
                 magma_queue_sync( streams[dev][0] );
             }

             /* ==========================================================
                Update the unreduced submatrix A(i+ib:n,i+ib:n), using   
                an update of the form:  A := A - V*W' - W*V' 
                ==========================================================  */
             if (i + nb <= n-nb){
                 /* There would be next iteration;
                    do lookahead - update the next panel */
                 // below a -1 was added and then a -1 was done on di because of the fortran indexing
                 iblock = ((indi-1) / distblk) / ngpu;          // local block id
                 di     = iblock*distblk + (indi-1)%distblk;     // local index in parent matrix
                 idev   = ((indi-1) / distblk) % ngpu;          // device with this block
                 cudaSetDevice( idev );
                 magmablasSetKernelStream( streams[ idev ][ nstream-1 ] );
                 //magma_queue_sync( streams[idev][0] ); removed because the sync has been done in the loop above

                 trace_gpu_start( idev, 2, "gemm", "gemm 4 next panel left" );
                 magma_zgemm(MagmaNoTrans, MagmaConjTrans, pm, pn, pn, c_neg_one,
                             &dvtest[idev][flipV*nb*lddv], pm,
                             dwtest[idev]                , pm, c_one,
                             datest(idev, indi, di+1), ldda);
                 trace_gpu_end( idev, 2 );
             
                 trace_gpu_start( idev, 2, "gemm", "gemm 5 next panel right" );
                 magma_zgemm(MagmaNoTrans, MagmaConjTrans, pm, pn, pn, c_neg_one,
                             dwtest[idev]                , pm,
                             &dvtest[idev][flipV*nb*lddv], pm, c_one,
                             datest(idev, indi, di+1), ldda);
                 trace_gpu_end( idev, 2 );
                 //printf("updating next panel distblk %d  idev %d  on A(%d,%d) of size %d %d %d \n",distblk,idev,indi-1,di,pm,pn,pn); 
             }
             else {
                 /* no look-ahead as this is last iteration */
                 // below a -1 was added and then a -1 was done on di because of the fortran indexing
                 iblock = ((indi-1) / distblk) / ngpu;          // local block id
                 di     = iblock*distblk + (indi-1)%distblk;     // local index in parent matrix
                 idev   = ((indi-1) / distblk) % ngpu;          // device with this block
                 cudaSetDevice( idev );
                 magmablasSetKernelStream( streams[ idev ][ 0 ] );
                 //printf("LAST ZHER2K idev %d on A(%d,%d) of size %d \n",idev, indi-1,di,pk); 

                 trace_gpu_start( idev, 2, "her2k", "her2k last iteration" );
                 magma_zher2k(MagmaLower, MagmaNoTrans, pk, pk, c_neg_one,
                              &dvtest[idev][flipV*nb*lddv], pm,
                              dwtest[idev]                , pm, d_one,
                              datest(idev, indi, di+1), ldda);
                 trace_gpu_end( idev, 2 );


                 /* Send the last block to the CPU */     
                 zpanel_to_q(MagmaUpper, pk-1, a_ref(n-pk+1, n-pk+2), lda, work);
                 trace_gpu_start( idev, 2, "get", "get last block" );
                 magma_zgetmatrix( pk, pk,
                                   datest(idev, indi, di+1), ldda,
                                   a_ref(n-pk+1, n-pk+1),  lda );
                 trace_gpu_end( idev, 2 );
                 zq_to_panel(MagmaUpper, pk-1, a_ref(n-pk+1, n-pk+2), lda, work);
             }

             indi_old = indi;
             indj_old = indj;
             pm_old   = pm;
             pn_old   = pn;
        }  // end loop for(i)

    }// end of LOWER
    //cudaSetDevice( 0 );

    for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
        cudaSetDevice( dev );
        magma_free( dvtest[dev]);
        magma_free( dwtest[dev] );
        magma_free( dworktest[dev]);
        magma_free( dworktestbis[dev]);
        for( magma_int_t e = 0; e < nbevents; ++e ) {
             cudaEventDestroy(redevents[dev][e]);
        }
    }
    magma_setdevice( cdev );




    free(worktest);
    trace_finalize( "zhetrd_he2hb.svg", "trace.css" );
    
    MAGMA_Z_SET2REAL( work[0], lwkopt );
#if defined(USEMKL)
    mkl_set_num_threads(1);
#endif
#if defined(USEACML)
    omp_set_num_threads(1);
#endif


    return *info;
} /* zhetrd_he2hb_ */
