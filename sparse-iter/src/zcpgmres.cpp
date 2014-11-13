/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date

       @author Hartwig Anzt

       @precisions mixed zc -> ds
*/

#include "common_magma.h"
#include "magmasparse.h"


#define PRECISION_z

#define RTOLERANCE     10e-10
#define ATOLERANCE     10e-10


/**
    Purpose
    -------

    Solves a system of linear equations
       A * X = B
    where A is a complex sparse matrix stored in the GPU memory.
    X and B are complex vectors stored on the GPU memory. 
    This is a GPU implementation of the GMRES method.

    Arguments
    ---------

    @param[in]
    A           magma_z_sparse_matrix
                descriptor for matrix A

    @param[in]
    b           magma_z_vector
                RHS b vector

    @param[in,out]
    x           magma_z_vector*
                solution approximation

    @param[in,out]
    solver_par  magma_z_solver_par*
                solver parameters

    magma_precond_parameters *precond_par     preconditioner parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_gesv
    ********************************************************************/

extern "C" magma_int_t
magma_zcpgmres(
    magma_z_sparse_matrix A, magma_z_vector b, magma_z_vector *x,  
    magma_z_solver_par *solver_par,  magma_precond_parameters *precond_par,
    magma_queue_t queue )
{
    // set queue for old dense routines
    magma_queue_t orig_queue;
    magmablasGetKernelStream( &orig_queue );

    #define  q(i)     (q.dval + (i)*dofs)
#define  z(i)     (z.dval + (i)*dofs)
#define  H(i,j)  H[(i)   + (j)*ldh]
#define HH(i,j) HH[(i)   + (j)*ldh]

    // local variables
    magmaDoubleComplex c_zero = MAGMA_Z_ZERO, c_one = MAGMA_Z_ONE, c_mone = MAGMA_Z_NEG_ONE;
    magma_int_t dofs = A.num_rows;
    magma_int_t i, j, k, m, iter, ldh = solver_par->restart+1;
    double rNorm, RNorm, den, nom0, r0 = 0.;

    // CPU workspace
    magmaDoubleComplex H[(ldh+1)*ldh], HH[ldh*ldh]; 
    magmaDoubleComplex  y[ldh], h1[ldh];
    
    // GPU workspace
    magma_z_vector r, q, q_t, z, z_t;
    magma_z_vinit( &r  , Magma_DEV, dofs,     c_zero, queue );
    magma_z_vinit( &q  , Magma_DEV, dofs*ldh, c_zero, queue );
    magma_z_vinit( &q_t, Magma_DEV, dofs,     c_zero, queue );
    magma_z_vinit( &z  , Magma_DEV, dofs*ldh, c_zero, queue );
    magma_z_vinit( &z_t, Magma_DEV, dofs,     c_zero, queue );
    // for mixed precision on GPU
    magma_c_vector qs_t, zs_t;
    magma_c_sparse_matrix AS;
    magma_sparse_matrix_zlag2c( A, &AS, queue );
    magma_c_vinit( &zs_t, Magma_DEV, dofs, MAGMA_C_ZERO, queue );

    magmaDoubleComplex *dy;
    if (MAGMA_SUCCESS != magma_zmalloc( &dy, ldh )) 
        magmablasSetKernelStream( orig_queue );
        return MAGMA_ERR_DEVICE_ALLOC;
    
    magma_zscal( dofs, c_zero, x->dval, 1 );              //  x = 0
    magma_zcopy( dofs, b.dval, 1, r.dval, 1 );             //  r = b

    r0 = magma_dznrm2( dofs, r.dval, 1 );                 //  r0= || r||
    nom0 = r0*r0;
    H(1,0) = MAGMA_Z_MAKE( r0, 0. ); 

    if ((r0 *= solver_par->epsilon) < ATOLERANCE) 
        r0 = ATOLERANCE;
    
    printf("Iteration : %4d  Norm: %f\n", 0, H(1,0)*H(1,0));

    for (iter = 0; iter<solver_par->maxiter; iter++) 
        {
            for(k=1; k<=(solver_par->restart); k++) 
                {
                    magma_zcopy(dofs, r.dval, 1, q(k), 1);                        //  q[k]    = 1.0/H[k][k-1] r
                    magma_zscal(dofs, 1./H(k,k-1), q(k), 1);                     //  (to be fused)

                    q_t.dval = q(k);
                    magma_vector_zlag2c(q_t, &qs_t, queue );                    // conversion to single precision            
                    magma_c_precond( AS, qs_t, &zs_t, *precond_par, queue );   // preconditioner AS * zs =  qs[k]
                    magma_vector_clag2z(zs_t, &z_t, queue );                    // conversion to double precision
                    magma_zcopy( dofs, z_t.dval, 1, z(k), 1 );             // z(k) = z_t
                    magma_z_spmv( c_one, A, z_t, c_zero, r, queue );                    //  r       = A q[k] 
                    
                    for (i=1; i<=k; i++) {
                        H(i,k) =magma_zdotc(dofs, q(i), 1, r.dval, 1);            //  H[i][k] = q[i] . r
                        magma_zaxpy(dofs,-H(i,k), q(i), 1, r.dval, 1);            //  r       = r - H[i][k] q[i]
                    }
                    
                    H(k+1,k) = MAGMA_Z_MAKE( magma_dznrm2(dofs, r.dval, 1), 0. ); //  H[k+1][k] = sqrt(r . r) 
                    
                    /*     Minimization of  || b-Ax ||  in K_k       */ 
                    for (i=1; i<=k; i++) {
                        HH(k,i) = magma_cblas_zdotc( i+1, &H(1,k), 1, &H(1,i), 1 );
                    }
                    
                    h1[k] = H(1,k)*H(1,0);
                    
                    if (k != 1)
                        for (i=1; i<k; i++) {
                            for (m=i+1; m<k; m++)
                                HH(k,m) -= HH(k,i) * HH(m,i);
                           
                            HH(k,k) -= HH(k,i) * HH(k,i) / HH(i,i);
                            HH(k,i) = HH(k,i)/HH(i,i);
                            h1[k] -= h1[i] * HH(k,i);   
                        }    
                    y[k] = h1[k]/HH(k,k); 
                    if (k != 1)  
                        for (i=k-1; i>=1; i--) {
                            y[i] = h1[i]/HH(i,i);
                            for (j=i+1; j<=k; j++)
                                y[i] -= y[j] * HH(j,i);
                        }
                    
                    m = k;
                    
                    rNorm = fabs(MAGMA_Z_REAL(H(k+1,k)));
                    //if (rNorm < r0) break;
                }
            
            /*   Update the current approximation: x += Q y  */
            magma_zsetmatrix(m, 1, y+1, m, dy, m);
            magma_zgemv(MagmaNoTrans, dofs, m, c_one, z(1), dofs, dy, 1, c_one, x->dval, 1); 

            magma_z_spmv( c_mone, A, *x, c_zero, r, queue );                  //  r = - A * x
            magma_zaxpy(dofs, c_one, b.dval, 1, r.dval, 1);              //  r = r + b
            H(1,0) = MAGMA_Z_MAKE( magma_dznrm2(dofs, r.dval, 1), 0. ); //  RNorm = H[1][0] = || r ||
            RNorm = MAGMA_Z_REAL( H(1,0) );
            
            printf("Iteration : %4d  Norm: %f\n", iter, RNorm*RNorm);
            
            if (fabs(RNorm*RNorm) < r0) break;    
            //if (rNorm < r0) break;
        }
    
    
    printf( "      (r_0, r_0) = %e\n", nom0 );
    printf( "      (r_N, r_N) = %e\n", RNorm*RNorm);
    printf( "      Number of GMRES restarts: %d\n", iter);
    
    if (solver_par->epsilon == RTOLERANCE) {
        magma_z_spmv( c_one, A, *x, c_zero, r, queue );                       // r = A x
        magma_zaxpy(dofs,  c_mone, b.dval, 1, r.dval, 1);                // r = r - b
        den = magma_dznrm2(dofs, r.dval, 1);                            // den = || r ||
        printf( "      || r_N ||   = %f\n", den);
        solver_par->residual = (double)(den);
    }
    solver_par->numiter = iter;

    magma_free(dy); 
/*
    magma_z_vfree(&r, queue );
    magma_z_vfree(&q, queue );
    magma_z_vfree(&q_t, queue );
    magma_z_vfree(&z, queue );
    magma_z_vfree(&z_t, queue );
    magma_c_vfree(&qs_t, queue );
    magma_c_vfree(&zs_t, queue );

    magma_c_mfree(&AS, queue );
*/
    magmablasSetKernelStream( orig_queue );
    return MAGMA_SUCCESS;
}   /* magma_zcpgmres */

