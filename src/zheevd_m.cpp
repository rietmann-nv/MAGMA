/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011

       @author Stan Tomov

       @precisions normal z -> c

*/
#include "common_magma.h"

#define DWORKFORZ_AND_LD double *rwork, magma_int_t *ldrwork,

extern "C" {
    magma_int_t magma_zhetrd_mgpu(int num_gpus, int k, char uplo, magma_int_t n, 
                                  cuDoubleComplex *a, magma_int_t lda, 
                                  double *d, double *e, cuDoubleComplex *tau,
                                  cuDoubleComplex *work, magma_int_t lwork, 
                                  magma_int_t *info);
    
    void magma_zstedx_(char* range, magma_int_t *n, double *vl, double *vu,
                       magma_int_t *il, magma_int_t *iu, double *D, double *E,
                       cuDoubleComplex *Z, magma_int_t *ldz,
                       double *rwork, magma_int_t *ldrwork, magma_int_t *iwork,
                       magma_int_t *liwork, double* dwork, magma_int_t *info);
    
    magma_int_t magma_zstedx(char range, magma_int_t n, double vl, double vu,
                             magma_int_t il, magma_int_t iu, double *D, double *E,
                             cuDoubleComplex *Z, magma_int_t ldz,
                             double *rwork, magma_int_t ldrwork, magma_int_t *iwork,
                             magma_int_t liwork, double* dwork, magma_int_t *info);

    magma_int_t magma_zstedx_m(magma_int_t nrgpu,
                               char range, magma_int_t n, double vl, double vu,
                               magma_int_t il, magma_int_t iu, double *D, double *E,
                               cuDoubleComplex *Z, magma_int_t ldz,
                               double *rwork, magma_int_t ldrwork, magma_int_t *iwork,
                               magma_int_t liwork, magma_int_t *info);

    magma_int_t magma_zunmtr_m(magma_int_t nrgpu, char side, char uplo, char trans,
                               magma_int_t m, magma_int_t n, 
                               cuDoubleComplex *a,    magma_int_t lda, 
                               cuDoubleComplex *tau, 
                               cuDoubleComplex *c,    magma_int_t ldc,
                               cuDoubleComplex *work, magma_int_t lwork, 
                               magma_int_t *info);
}

extern "C" magma_int_t 
magma_zheevd_m(magma_int_t nrgpu, char jobz, char uplo, 
               magma_int_t n, 
               cuDoubleComplex *a, magma_int_t lda, 
               double *w, 
               cuDoubleComplex *work, magma_int_t lwork,
               double *rwork, magma_int_t lrwork,
               magma_int_t *iwork, magma_int_t liwork,
               magma_int_t *info)
{
/*  -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011

    Purpose   
    =======
    ZHEEVD computes all eigenvalues and, optionally, eigenvectors of a   
    complex Hermitian matrix A.  If eigenvectors are desired, it uses a   
    divide and conquer algorithm.   

    The divide and conquer algorithm makes very mild assumptions about   
    floating point arithmetic. It will work on machines with a guard   
    digit in add/subtract, or on those binary machines without guard   
    digits which subtract like the Cray X-MP, Cray Y-MP, Cray C-90, or   
    Cray-2. It could conceivably fail on hexadecimal or decimal machines   
    without guard digits, but we know of none.   

    Arguments   
    =========   
    JOBZ    (input) CHARACTER*1   
            = 'N':  Compute eigenvalues only;   
            = 'V':  Compute eigenvalues and eigenvectors.   

    UPLO    (input) CHARACTER*1   
            = 'U':  Upper triangle of A is stored;   
            = 'L':  Lower triangle of A is stored.   

    N       (input) INTEGER   
            The order of the matrix A.  N >= 0.   

    A       (input/output) COMPLEX_16 array, dimension (LDA, N)   
            On entry, the Hermitian matrix A.  If UPLO = 'U', the   
            leading N-by-N upper triangular part of A contains the   
            upper triangular part of the matrix A.  If UPLO = 'L',   
            the leading N-by-N lower triangular part of A contains   
            the lower triangular part of the matrix A.   
            On exit, if JOBZ = 'V', then if INFO = 0, A contains the   
            orthonormal eigenvectors of the matrix A.   
            If JOBZ = 'N', then on exit the lower triangle (if UPLO='L')   
            or the upper triangle (if UPLO='U') of A, including the   
            diagonal, is destroyed.   

    LDA     (input) INTEGER   
            The leading dimension of the array A.  LDA >= max(1,N).   

    W       (output) DOUBLE PRECISION array, dimension (N)   
            If INFO = 0, the eigenvalues in ascending order.   

    WORK    (workspace/output) COMPLEX_16 array, dimension (MAX(1,LWORK))   
            On exit, if INFO = 0, WORK(1) returns the optimal LWORK.   

    LWORK   (input) INTEGER   
            The length of the array WORK.   
            If N <= 1,                LWORK must be at least 1.   
            If JOBZ  = 'N' and N > 1, LWORK must be at least N * (NB + 1).   
            If JOBZ  = 'V' and N > 1, LWORK must be at least 2*N + N**2.   

            If LWORK = -1, then a workspace query is assumed; the routine   
            only calculates the optimal sizes of the WORK, RWORK and   
            IWORK arrays, returns these values as the first entries of   
            the WORK, RWORK and IWORK arrays, and no error message   
            related to LWORK or LRWORK or LIWORK is issued by XERBLA.    

    RWORK   (workspace/output) DOUBLE PRECISION array,   
                                           dimension (LRWORK)   
            On exit, if INFO = 0, RWORK(1) returns the optimal LRWORK.   

    LRWORK  (input) INTEGER   
            The dimension of the array RWORK.   
            If N <= 1,                LRWORK must be at least 1.   
            If JOBZ  = 'N' and N > 1, LRWORK must be at least N.   
            If JOBZ  = 'V' and N > 1, LRWORK must be at least   
                           1 + 5*N + 2*N**2.   

            If LRWORK = -1, then a workspace query is assumed; the   
            routine only calculates the optimal sizes of the WORK, RWORK   
            and IWORK arrays, returns these values as the first entries   
            of the WORK, RWORK and IWORK arrays, and no error message   
            related to LWORK or LRWORK or LIWORK is issued by XERBLA.   

    IWORK   (workspace/output) INTEGER array, dimension (MAX(1,LIWORK))   
            On exit, if INFO = 0, IWORK(1) returns the optimal LIWORK.   

    LIWORK  (input) INTEGER   
            The dimension of the array IWORK.   
            If N <= 1,                LIWORK must be at least 1.   
            If JOBZ  = 'N' and N > 1, LIWORK must be at least 1.   
            If JOBZ  = 'V' and N > 1, LIWORK must be at least 3 + 5*N.   

            If LIWORK = -1, then a workspace query is assumed; the   
            routine only calculates the optimal sizes of the WORK, RWORK   
            and IWORK arrays, returns these values as the first entries   
            of the WORK, RWORK and IWORK arrays, and no error message   
            related to LWORK or LRWORK or LIWORK is issued by XERBLA.   

    INFO    (output) INTEGER   
            = 0:  successful exit   
            < 0:  if INFO = -i, the i-th argument had an illegal value   
            > 0:  if INFO = i and JOBZ = 'N', then the algorithm failed   
                  to converge; i off-diagonal elements of an intermediate   
                  tridiagonal form did not converge to zero;   
                  if INFO = i and JOBZ = 'V', then the algorithm failed   
                  to compute an eigenvalue while working on the submatrix   
                  lying in rows and columns INFO/(N+1) through   
                  mod(INFO,N+1).   

    Further Details   
    ===============   
    Based on contributions by   
       Jeff Rutter, Computer Science Division, University of California   
       at Berkeley, USA   

    Modified description of INFO. Sven, 16 Feb 05.   
    =====================================================================   */

    char uplo_[2] = {uplo, 0};
    char jobz_[2] = {jobz, 0};
    magma_int_t c__1 = 1;
    magma_int_t c_n1 = -1;
    magma_int_t c__0 = 0;
    double c_b18 = 1.;
    
    double d__1;

    double eps;
    magma_int_t inde;
    double anrm;
    magma_int_t imax;
    double rmin, rmax;
    double sigma;
    magma_int_t iinfo, lwmin;
    magma_int_t lower;
    magma_int_t llrwk;
    magma_int_t wantz;
    magma_int_t indwk2, llwrk2;
    magma_int_t iscale;
    double safmin;
    double bignum;
    magma_int_t indtau;
    magma_int_t indrwk, indwrk, liwmin;
    magma_int_t lrwmin, llwork;
    double smlnum;
    magma_int_t lquery;
    
    double* dwork;

    wantz = lapackf77_lsame(jobz_, MagmaVectorsStr);
    lower = lapackf77_lsame(uplo_, MagmaLowerStr);
    lquery = lwork == -1 || lrwork == -1 || liwork == -1;

    *info = 0;
    if (! (wantz || lapackf77_lsame(jobz_, MagmaNoVectorsStr))) {
        *info = -1;
    } else if (! (lower || lapackf77_lsame(uplo_, MagmaUpperStr))) {
        *info = -2;
    } else if (n < 0) {
        *info = -3;
    } else if (lda < max(1,n)) {
        *info = -5;
    }

    magma_int_t nb = magma_get_zhetrd_nb(n); 
  
    if (wantz) {
        lwmin = 2 * n + n * n;
        lrwmin = 1 + 5 * n + 2 * n * n;
        liwmin = 5 * n + 3;
    } else {
        lwmin = n * (nb + 1);
        lrwmin = n;
        liwmin = 1;
    }

    MAGMA_Z_SET2REAL(work[0],(double)lwmin);
    rwork[0] = lrwmin;
    iwork[0] = liwmin;

    if ((lwork < lwmin) && !lquery) {
        *info = -8;
    } else if ((lrwork < lrwmin) && ! lquery) {
        *info = -10;
    } else if ((liwork < liwmin) && ! lquery) {
        *info = -12;
    }

    if (*info != 0) {
        magma_xerbla( __func__, -(*info) );
        return *info;
    }
    else if (lquery) {
        return *info;
    }

    /* Quick return if possible */
    if (n == 0) {
        return *info;
    }

    if (n == 1) {
        w[0] = MAGMA_Z_REAL(a[0]);
        if (wantz) {
            a[0] = MAGMA_Z_ONE;
        }
        return *info;
    }

    /* Get machine constants. */
    safmin = lapackf77_dlamch("Safe minimum");
    eps = lapackf77_dlamch("Precision");
    smlnum = safmin / eps;
    bignum = 1. / smlnum;
    rmin = magma_dsqrt(smlnum);
    rmax = magma_dsqrt(bignum);

    /* Scale matrix to allowable range, if necessary. */
    anrm = lapackf77_zlanhe("M", uplo_, &n, a, &lda, rwork);
    iscale = 0;
    if (anrm > 0. && anrm < rmin) {
        iscale = 1;
        sigma = rmin / anrm;
    } else if (anrm > rmax) {
        iscale = 1;
        sigma = rmax / anrm;
    }
    if (iscale == 1) {
        lapackf77_zlascl(uplo_, &c__0, &c__0, &c_b18, &sigma, &n, &n, a, 
                &lda, info);
    }

    /* Call ZHETRD to reduce Hermitian matrix to tridiagonal form. */
    inde = 0;
    indtau = 0;
    indwrk = indtau + n;
    indrwk = inde + n;
    indwk2 = indwrk + n * n;
    llwork = lwork - indwrk;
    llwrk2 = lwork - indwk2;
    llrwk = lrwork - indrwk;

#define ENABLE_TIMER
#ifdef ENABLE_TIMER 
    magma_timestr_t start, end;
    
    start = get_current_time();
#endif
    
    //magma_zhetrd(uplo_[0], n, a, lda, w, &rwork[inde],
    //             &work[indtau], &work[indwrk], llwork, &iinfo);
    magma_zhetrd_mgpu(nrgpu, 4, uplo_[0], n, a, lda, w, &rwork[inde],
                      &work[indtau], &work[indwrk], llwork, &iinfo);
    
#ifdef ENABLE_TIMER    
    end = get_current_time();
    
    printf("time zhetrd = %6.2f\n", GetTimerValue(start,end)/1000.);
#endif   
    
    /* For eigenvalues only, call DSTERF.  For eigenvectors, first call   
       ZSTEDC to generate the eigenvector matrix, WORK(INDWRK), of the   
       tridiagonal matrix, then call ZUNMTR to multiply it to the Householder 
       transformations represented as Householder vectors in A. */
    if (! wantz) {
        lapackf77_dsterf(&n, w, &rwork[inde], info);
    } else {
        //lapackf77_zstedc("I", &n, w, &rwork[inde], &work[indwrk], &n, &work[indwk2], 
        //                 &llwrk2, &rwork[indrwk], &llrwk, iwork, &liwork, info);
        
#ifdef ENABLE_TIMER
        start = get_current_time();
#endif
        
#ifdef USE_SINGLE_GPU
        if (MAGMA_SUCCESS != magma_dmalloc( &dwork, 3*n*(n/2 + 1) )) {
            *info = MAGMA_ERR_DEVICE_ALLOC;
            return *info;
        }
        
/*        double vl = 0;
        double vu = 0;
        magma_int_t il = 0;
        magma_int_t iu = 0;
        
        char range_[2] = {'A', 0};
        magma_zstedx_(range_, &n, &vl, &vu, &il, &iu, w, &rwork[inde],
                      &work[indwrk], &n, &rwork[indrwk],
                      &llrwk, iwork, &liwork, dwork, info);
*/        
        magma_zstedx('A', n, 0, 0, 0, 0, w, &rwork[inde],
                     &work[indwrk], n, &rwork[indrwk],
                     llrwk, iwork, liwork, dwork, info);
        
        magma_free( dwork );
#else
       magma_zstedx_m(nrgpu, 'A', n, 0, 0, 0, 0, w, &rwork[inde],
                      &work[indwrk], n, &rwork[indrwk],
                      llrwk, iwork, liwork, info);
#endif

#ifdef ENABLE_TIMER  
        end = get_current_time();
        
        printf("time zstedc = %6.2f\n", GetTimerValue(start,end)/1000.);
        
        start = get_current_time();
#endif

        //magma_zunmtr(MagmaLeft, uplo, MagmaNoTrans, n, n, a, lda, &work[indtau],
        magma_zunmtr_m(nrgpu, MagmaLeft, uplo, MagmaNoTrans, n, n, a, lda, &work[indtau],
                       &work[indwrk], n, &work[indwk2], llwrk2, &iinfo);
        
        lapackf77_zlacpy("A", &n, &n, &work[indwrk], &n, a, &lda);

#ifdef ENABLE_TIMER    
        end = get_current_time();
        
        printf("time zunmtr + copy = %6.2f\n", GetTimerValue(start,end)/1000.);
#endif        
        
    }

    /* If matrix was scaled, then rescale eigenvalues appropriately. */
    if (iscale == 1) {
        if (*info == 0) {
            imax = n;
        } else {
            imax = *info - 1;
        }
        d__1 = 1. / sigma;
        blasf77_dscal(&imax, &d__1, w, &c__1);
    }

    work[0]  = MAGMA_Z_MAKE((double) lwmin, 0.);
    rwork[0] = (double) lrwmin;
    iwork[0] = liwmin;

    return *info;
} /* magma_zheevd_m */
