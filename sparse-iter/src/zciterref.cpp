/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date

       @author Hartwig Anzt 

       @precisions mixed zc -> ds
*/
#include <sys/time.h>
#include <time.h>

#include "common_magma.h"
#include "../include/magmasparse.h"

#include <assert.h>

#define RTOLERANCE     1e-16
#define ATOLERANCE     1e-16


/*  -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date

    Purpose
    =======

    Solves a system of linear equations
       A * X = B
    where A is a complex Hermitian N-by-N positive definite matrix A.
    This is a GPU implementation of the mixed precision
    Iterative Refinement method.

    Arguments
    =========

    magma_z_sparse_matrix A                   input matrix A
    magma_z_vector b                          RHS b
    magma_z_vector *x                         solution approximation
    magma_solver_parameters *solver_par       solver parameters
    magma_precond_parameters *precond_par     parameters for inner solver

    ========================================================================  */


magma_int_t
magma_zcir( magma_z_sparse_matrix A, magma_z_vector b, magma_z_vector *x,  
            magma_solver_parameters *solver_par, 
            magma_precond_parameters *precond_par ){

    //Chronometry
    struct timeval inicio, fim;
    double tempo1, tempo2;

    // some useful variables
    magmaDoubleComplex c_zero = MAGMA_Z_ZERO, c_one = MAGMA_Z_ONE, 
                                                    c_mone = MAGMA_Z_NEG_ONE;
    
    magma_int_t dofs = A.num_rows;

    // workspace on GPU
    magma_z_vector r,z;
    magma_z_vinit( &r, Magma_DEV, dofs, c_zero );
    magma_z_vinit( &z, Magma_DEV, dofs, c_zero );

    // for mixed precision on GPU
    magma_c_vector rs, zs;
    magma_c_sparse_matrix AS;
    magma_sparse_matrix_zlag2c( A, &AS );
    magma_c_vinit( &rs, Magma_DEV, dofs, MAGMA_C_ZERO );
    magma_c_vinit( &zs, Magma_DEV, dofs, MAGMA_C_ZERO );


    // solver variables
    double nom, nom0, r0, den;
    magma_int_t i;


    // solver setup
    magma_zscal( dofs, c_zero, x->val, 1) ;                    // x = 0

    magma_z_spmv( c_mone, A, *x, c_zero, r );                  // r = - A x
    magma_zaxpy(dofs,  c_one, b.val, 1, r.val, 1);             // r = r + b
    nom = nom0 = magma_dznrm2(dofs, r.val, 1);                 // nom0 = || r ||
    
    if ( (r0 = nom * solver_par->epsilon) < ATOLERANCE ) 
        r0 = ATOLERANCE;
    if ( nom < r0 )
        return MAGMA_SUCCESS;

    

    //Chronometry 
    gettimeofday(&inicio, NULL);
    tempo1=inicio.tv_sec+(inicio.tv_usec/1000000.0);

    printf("Iteration: %4d  Norm: %e  Time: %e\n", 0, nom/nom0, 0.0);

    // start iteration
    for( i= 1; i<solver_par->maxiter; i++ ) {

        magma_zscal( dofs, MAGMA_Z_MAKE(1./nom, 0.), r.val, 1) ;// scale
        magma_vector_zlag2c(r, &rs);                            // conversion 
        magma_c_precond( AS, rs, &zs, *precond_par );           // inner solve
        magma_vector_clag2z(zs, &z);                            // conversion 
        magma_zscal( dofs, MAGMA_Z_MAKE(nom, 0.), z.val, 1) ;   // scale
        magma_zaxpy(dofs,  c_one, z.val, 1, x->val, 1);         // x = x + z
        magma_z_spmv( c_mone, A, *x, c_zero, r );               // r = - A x
        magma_zaxpy(dofs,  c_one, b.val, 1, r.val, 1);          // r = r + b
        nom = magma_dznrm2(dofs, r.val, 1);                     // nom = || r ||

        //Chronometry  
        gettimeofday(&fim, NULL);
        tempo2=fim.tv_sec+(fim.tv_usec/1000000.0);

        printf("Iteration: %4d  Norm: %e  Time: %e\n", i, nom/nom0, tempo2-tempo1);
        if ( nom < r0 ) {
            solver_par->numiter = i;
            break;
        }
    } 


    
    printf( "      (r_0, r_0) = %e\n", nom0);
    printf( "      (r_N, r_N) = %e\n", nom);
    printf( "      Number of Iterative Refinement iterations: %d\n", i);
    
    if (solver_par->epsilon == RTOLERANCE) {
        magma_z_spmv( c_one, A, *x, c_zero, r );                // r = A x
        magma_zaxpy(dofs,  c_mone, b.val, 1, r.val, 1);         // r = r - b
        den = magma_dznrm2(dofs, r.val, 1);                     // den = || r ||
        printf( "      || r_N ||   = %e\n", den);
        solver_par->residual = (double)(den);
    }
    
    magma_z_vfree(&r);
    magma_z_vfree(&z);
    magma_c_vfree(&rs);
    magma_c_vfree(&zs);
    cudaFree( &(AS.val) );

    return MAGMA_SUCCESS;
}   /* magma_zcir */






