/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011

       @precisions normal z -> s d c
       @author Mark Gates
       
       This still has poor performance. Work in progress.
*/
#include "common_magma.h"
#include "trace.h"
#include <assert.h>

extern "C" void
magmablas_zsymmetrize( char uplo, magma_int_t m, cuDoubleComplex *dA, magma_int_t ldda );
extern "C" void   
magmablas_zsymmetrize_tiles(
                            char uplo, int m, cuDoubleComplex *dA, int ldda,
                                        int ntile, int mstride, int nstride );
extern "C"
void magmablas_zhemm_mgpu(
    char side, char uplo, magma_int_t m, magma_int_t n,
    cuDoubleComplex alpha, cuDoubleComplex *dA[], magma_int_t ldda,  magma_int_t offset,
                           cuDoubleComplex *dB[], magma_int_t lddb,
    cuDoubleComplex beta,  cuDoubleComplex *dC[], magma_int_t lddc,
                           cuDoubleComplex *dwork[],    magma_int_t lddwork,
                           cuDoubleComplex *C,    magma_int_t ldc,
    magma_int_t ngpu, magma_int_t nb, cudaStream_t streams[][20], magma_int_t nstream )
{
    #define dA(dev, i, j) (dA[dev] + (i) + (j)*ldda)
    #define dB(dev, i, j) (dB[dev] + (i) + (j)*lddb)
    #define dC(dev, i, j) (dC[dev] + (i) + (j)*lddc)
    #define dwork(dev, i, j) (dwork[dev] + (i) + (j)*lddwork)
    #define C(i, j) (C + (i) + (j)*ldc)
    
    assert( ldda >= m );
    assert( lddb >= m );
    assert( lddc >= m );
    assert( lddwork >= m );
    
    cuDoubleComplex c_one  = MAGMA_Z_ONE;
    cuDoubleComplex c_zero = MAGMA_Z_ZERO;
    magma_int_t ione = 1;
    
    // put init/finalize into testing_zhemm_mgpu,
    // so Gflop/s doesn't include writing file.
    //trace_init( 1, ngpu, nstream, (cudaStream_t*) streams );
        
    magma_device_t cdev;
    magma_getdevice( &cdev );
    magma_stream_t cstream;
    magmablasGetKernelStream(&cstream);

    // loop over all blocks
    // Faster to have several loops:
    // first  symmetrizes A[i,i]
    // second does C[i]      = A[i:m,  i]'*B[i:m]
    // third  does C[i+1:m] += A[i+1:m,i] *B[i]
/*    
    // tracing
    for( int dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        trace_gpu_start( dev, 0, "init", "initialize" );
    }
 */   
    
    for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        //magmablas_zlaset( MagmaUpperLower, m, n, dC(dev,0,0), lddc );
        cudaMemset(dC(dev,0,0), 0, (lddc)*(n)*sizeof(cuDoubleComplex) );
        cudaMemset(dwork(dev,0,0), 0, (lddwork)*(n)*sizeof(cuDoubleComplex) );
    }
    
/*
    // tracing
    for( int dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        trace_gpu_end( dev, 0 );
        trace_gpu_start( dev, 0, "symmetrize", "symmetrize" );
    }
*/
    // 1. symmetrize
    for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        magma_int_t nbblk = m/nb; // number of block of size nb. if m%nb>0 then a last block exist and is of size ib=m%nb
        magma_int_t myblk = (nbblk/ngpu) + (nbblk%ngpu > dev ?  1:0 );
        magmablas_zsymmetrize_tiles(  MagmaLower,  nb,  dA(dev, dev*nb, 0),  ldda,  myblk,  ngpu*nb,  nb  );
        if(m%nb>0){
            magma_int_t devlstblk = (nbblk+1)%ngpu;
            if(dev==devlstblk)
                magmablas_zsymmetrize(  MagmaLower,  m % nb,  dA(dev,offset+nbblk*nb, myblk*nb),  ldda );  // last partial tile
        }
    }

/*
    // 1. symmetrize
    for( magma_int_t i = 0; i < m; i += nb ) {
        magma_int_t ib     = min( nb, m-i );      // block size
        magma_int_t ioff   = i + offset;          // start global index in parent matrix
        magma_int_t iblock = (ioff / nb) / ngpu;  // local block id
        magma_int_t dev    = (ioff / nb) % ngpu;
        magma_int_t di     = iblock*nb;           // local index in parent matrix
        
        magma_setdevice( dev );
        magma_int_t s = iblock % nstream;
        magmablasSetKernelStream( streams[ dev ][ s ] );
        
        // make diagonal block symmetric
        magmablas_zsymmetrize( MagmaLower, ib, dA(dev,ioff,di), ldda );
    }
    
    for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        for( magma_int_t s = 0; s < nstream; ++s ) {
            magma_queue_sync( streams[ dev ][ s ] );
        }
    }

  */
    magma_int_t gemmstream=1;
/*
    // tracing
    for( int dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        trace_gpu_end( dev, 0 );
        trace_gpu_start( dev, 1, "gemm", "ROW gemm" );
    }
*/
    // ROW GEMM transpose a row and make a gemm with a block
    // if only 1 GPU used the ROW GEMM is integrated with the 
    // COL GEMM (better accuracy observed) and better perf
    if(ngpu>0){
        for( magma_int_t i = nb; i < m; i += nb ) {
            magma_int_t ib     = min( nb, m-i );      // block size
            magma_int_t ioff   = i + offset;          // start global index in parent matrix
            magma_int_t iblock = (ioff / nb) / ngpu;  // local block id
            magma_int_t dev    = (ioff / nb) % ngpu;
            magma_int_t di     = iblock*nb;           // local index in parent matrix
            for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
                magma_int_t nbblk = i/nb;
                magma_int_t myblk = (nbblk/ngpu) + (nbblk%ngpu > dev ?  1:0 );
                magma_int_t myrowsize = myblk * nb;
                magma_setdevice( dev );
                magma_int_t s = iblock % gemmstream;
                //printf("ROW GEMM, dev %d   nblk %d   myblk %d   myrowsize %d  B(%d)  A(%d,%d)\n",dev,nbblk,myblk, myrowsize,i,ioff,dev*nb);
                if(myrowsize>0){
                        magmablasSetKernelStream( streams[ dev ][ 1 ] );    
                        magma_zgemm( MagmaConjTrans, MagmaNoTrans, myrowsize, n, ib,
                                 alpha, dA(dev,ioff,0), ldda,
                                        dB(dev,i,0),    lddb,
                                 c_one, dwork(dev,0,0), lddwork );
                }
            }
        }
    }
/*
    // tracing
    for( int dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        trace_gpu_end( dev, 1 );
        trace_gpu_start( dev, 0, "gemm", "COL gemm" );
        if(ngpu==1) trace_gpu_start( dev, 1, "gemm", "ROW gemm" );        
    }
*/
    // COL GEMM
    for( magma_int_t i = 0; i < m; i += nb ) {
        magma_int_t ib     = min( nb, m-i );      // block size
        magma_int_t ioff   = i + offset;          // start global index in parent matrix
        magma_int_t iblock = (ioff / nb) / ngpu;  // local block id
        magma_int_t dev    = (ioff / nb) % ngpu;
        magma_int_t di     = iblock*nb;           // local index in parent matrix
        
        
        magma_setdevice( dev );
        magma_int_t s = iblock % gemmstream;
        magmablasSetKernelStream( streams[ dev ][ 0 ] );
        // printf("COL GEMM, dev %d   iblock %d   m %d n %d k %d A(%d,%d) B(%d,0) C(%d,0) \n",dev,iblock,m-i,n,ib,ioff,di,i,i);
          magma_zgemm( MagmaNoTrans, MagmaNoTrans, m-i, n, ib,
                         alpha, dA(dev,ioff,di), ldda,
                                dB(dev,i,0),        lddb,
                         c_one, dC(dev,i,0),     lddc );
        // if only 1 GPU is used do the ROW GEMM
        if(ngpu==100){
            // NOTE THAT because the COL gemm write dC below the diagonal (i) 
            // and the ROW GEMM write dC from 0 to diag-1, so they could 
            // run in parallel on diferent stream.        
            magmablasSetKernelStream( streams[ dev ][ 100 ] );
            magma_zgemm( MagmaConjTrans, MagmaNoTrans, i, n, ib,
                         alpha, dA(dev,ioff,0), ldda,
                                dB(dev,i,0),    lddb,
                         c_one, dC(dev,0,0),    lddc );
        }
    }



/*
    // tracing
    for( int dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        trace_gpu_end( dev, 0 );
        if(ngpu==1) trace_gpu_end( dev, 1 );        
    }
*/
    /*
    // 2b. sync
    if(gemmstream>1){
        for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
            magma_setdevice( dev );
            for( magma_int_t s = 0; s < gemmstream; ++s ) {
                magma_queue_sync( streams[ dev ][ s ] );
            }
        }
    }
    */

 /*   
    // meanwhile on CPU, scale C := beta*C
    // trace_cpu_start( 0, "scal", "C = beta*C" );
    for( magma_int_t j = 0; j < n; ++j ) {
        blasf77_zscal( &m, &beta, C(0,j), &ione );
    }
    // trace_cpu_end( 0 );
   */ 
    // wait and reduce results
    cuDoubleComplex *Ctmp = C(0,n);
    // receive and put on its placment the row block
  //  if(ngpu>1){
        for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
            magma_setdevice( dev );
            magma_int_t nbblk = (m-nb)/nb;
            magma_int_t myblk = (nbblk/ngpu) + (nbblk%ngpu > dev ?  1:0 );
            magma_int_t myrowsize = myblk * nb;
            if(myrowsize>0){
                // trace_gpu_start( dev, 1, "get", "get C_row" );
                     /*
                     magma_zgetmatrix_async( myrowsize, n,
                                             dwork[dev], lddwork,
                                             Ctmp, ldc, streams[dev][1] );
                     *//*
                     magma_zcopymatrix_async( myrowsize, n,
                                             dwork[dev], lddwork,
                                             Ctmp, ldc, streams[dev][1] );
            */                                 
                cudaMemcpy2DAsync(Ctmp, ldc*sizeof(cuDoubleComplex),
                                  dwork[dev], lddwork*sizeof(cuDoubleComplex),
                                  myrowsize*sizeof(cuDoubleComplex), n,
                                  cudaMemcpyDeviceToHost, streams[ dev ][ 1 ]);
                
                magma_queue_sync( streams[ dev ][ 1 ] );
                // trace_gpu_end( dev, 1 );
                // for each dev put the received block each on its placment
                // trace_cpu_start( 0, "axpy", "C += C_row" );
                for( magma_int_t blki = 0; blki < myblk; ++blki){
                    magma_int_t gbblki = (blki*ngpu + dev)*nb;
                    magma_int_t ib     = nb;// min(nb,m-gbblki);
                    for( magma_int_t j = 0; j < n; ++j ) {
                        blasf77_zaxpy( &ib, &c_one, &Ctmp[blki*nb+j*ldc], &ione, &C[gbblki+j*ldc], &ione );
                    }
                }
                // trace_cpu_end( 0 );
            }        
        }
   // }
    magma_int_t size = ldc*n;
    for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        // trace_gpu_start( dev, 0, "get", "get C_dev" );
        if(ngpu==1) magma_queue_sync( streams[ dev ][ 1 ] );
        magma_queue_sync( streams[ dev ][ 0 ] );
        magma_zgetmatrix( m, n, dC[dev], lddc, Ctmp, ldc );
        // trace_gpu_end( dev, 0 );
        
        // trace_cpu_start( 0, "axpy", "C += C_dev" );
        blasf77_zaxpy( &size, &c_one, Ctmp, &ione, C, &ione );
        // trace_cpu_end( 0 );
    }
    
        
/*
    for( magma_int_t dev = 0; dev < ngpu; ++dev ) {
        magma_setdevice( dev );
        magmablasSetKernelStream( cstream );
    }
*/    
    magma_setdevice( cdev );
    magmablasSetKernelStream( cstream );

}
