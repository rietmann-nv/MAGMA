/*
    -- MAGMA (version 1.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2011
*/

#ifndef MAGMA_H
#define MAGMA_H

/* ------------------------------------------------------------
 * MAGMA BLAS Functions
 * --------------------------------------------------------- */
#include "magmablas.h"

/* ------------------------------------------------------------
 * MAGMA functions
 * --------------------------------------------------------- */
#include "magma_z.h"
#include "magma_c.h"
#include "magma_d.h"
#include "magma_s.h"
#include "magma_zc.h"
#include "magma_ds.h"

#ifdef __cplusplus
extern "C" {
#endif

// ========================================
// initialization
magma_err_t
magma_init( void );

magma_err_t
magma_finalize( void );

void magma_version( int* major, int* minor, int* micro );


// ========================================
// memory allocation
magma_err_t
magma_malloc( magma_ptr *ptrPtr, size_t bytes );

magma_err_t
magma_malloc_cpu( void **ptrPtr, size_t bytes );

magma_err_t
magma_malloc_pinned( void **ptrPtr, size_t bytes );

magma_err_t
magma_free_cpu( magma_ptr ptr );

#define magma_free( ptr ) \
        magma_free_internal( ptr, __func__, __FILE__, __LINE__ )

#define magma_free_pinned( ptr ) \
        magma_free_pinned_internal( ptr, __func__, __FILE__, __LINE__ )

magma_err_t
magma_free_internal(
    magma_ptr ptr,
    const char* func, const char* file, int line );

magma_err_t
magma_free_pinned_internal(
    magma_ptr ptr,
    const char* func, const char* file, int line );


// type-safe convenience functions to avoid using (void**) cast and sizeof(...)
// here n is the number of elements (floats, doubles, etc.) not the number of bytes.
static inline magma_err_t magma_imalloc( magma_int_t        **ptrPtr, size_t n ) { return magma_malloc( (void**) ptrPtr, n*sizeof(magma_int_t)        ); }
static inline magma_err_t magma_smalloc( float              **ptrPtr, size_t n ) { return magma_malloc( (void**) ptrPtr, n*sizeof(float)              ); }
static inline magma_err_t magma_dmalloc( double             **ptrPtr, size_t n ) { return magma_malloc( (void**) ptrPtr, n*sizeof(double)             ); }
static inline magma_err_t magma_cmalloc( magmaFloatComplex  **ptrPtr, size_t n ) { return magma_malloc( (void**) ptrPtr, n*sizeof(magmaFloatComplex)  ); }
static inline magma_err_t magma_zmalloc( magmaDoubleComplex **ptrPtr, size_t n ) { return magma_malloc( (void**) ptrPtr, n*sizeof(magmaDoubleComplex) ); }

static inline magma_err_t magma_imalloc_cpu( magma_int_t        **ptrPtr, size_t n ) { return magma_malloc_cpu( (void**) ptrPtr, n*sizeof(magma_int_t)        ); }
static inline magma_err_t magma_smalloc_cpu( float              **ptrPtr, size_t n ) { return magma_malloc_cpu( (void**) ptrPtr, n*sizeof(float)              ); }
static inline magma_err_t magma_dmalloc_cpu( double             **ptrPtr, size_t n ) { return magma_malloc_cpu( (void**) ptrPtr, n*sizeof(double)             ); }
static inline magma_err_t magma_cmalloc_cpu( magmaFloatComplex  **ptrPtr, size_t n ) { return magma_malloc_cpu( (void**) ptrPtr, n*sizeof(magmaFloatComplex)  ); }
static inline magma_err_t magma_zmalloc_cpu( magmaDoubleComplex **ptrPtr, size_t n ) { return magma_malloc_cpu( (void**) ptrPtr, n*sizeof(magmaDoubleComplex) ); }

static inline magma_err_t magma_imalloc_pinned( magma_int_t        **ptrPtr, size_t n ) { return magma_malloc_pinned( (void**) ptrPtr, n*sizeof(magma_int_t)        ); }
static inline magma_err_t magma_smalloc_pinned( float              **ptrPtr, size_t n ) { return magma_malloc_pinned( (void**) ptrPtr, n*sizeof(float)              ); }
static inline magma_err_t magma_dmalloc_pinned( double             **ptrPtr, size_t n ) { return magma_malloc_pinned( (void**) ptrPtr, n*sizeof(double)             ); }
static inline magma_err_t magma_cmalloc_pinned( magmaFloatComplex  **ptrPtr, size_t n ) { return magma_malloc_pinned( (void**) ptrPtr, n*sizeof(magmaFloatComplex)  ); }
static inline magma_err_t magma_zmalloc_pinned( magmaDoubleComplex **ptrPtr, size_t n ) { return magma_malloc_pinned( (void**) ptrPtr, n*sizeof(magmaDoubleComplex) ); }


// ========================================
// device support
magma_int_t magma_getdevice_arch();

void magma_getdevices(
    magma_device_t* devices,
    magma_int_t     size,
    magma_int_t*    numPtr );

void magma_getdevice( magma_device_t* dev );

void magma_setdevice( magma_device_t dev );

void magma_device_sync();


// ========================================
// queue support
#define magma_queue_create( /*device,*/ queuePtr ) \
        magma_queue_create_internal( queuePtr, __func__, __FILE__, __LINE__ )

#define magma_queue_destroy( queue ) \
        magma_queue_destroy_internal( queue, __func__, __FILE__, __LINE__ )

#define magma_queue_sync( queue ) \
        magma_queue_sync_internal( queue, __func__, __FILE__, __LINE__ )

void magma_queue_create_internal(
    /*magma_device_t device,*/ magma_queue_t* queuePtr,
    const char* func, const char* file, int line );

void magma_queue_destroy_internal(
    magma_queue_t queue,
    const char* func, const char* file, int line );

void magma_queue_sync_internal(
    magma_queue_t queue,
    const char* func, const char* file, int line );


// ========================================
// event support
void magma_event_create( magma_event_t* eventPtr );

void magma_event_destroy( magma_event_t event );

void magma_event_record( magma_event_t event, magma_queue_t queue );

// blocks CPU until event occurs
void magma_event_sync( magma_event_t event );

// blocks queue (but not CPU) until event occurs
void magma_queue_wait_event( magma_queue_t queue, magma_event_t event );


// ========================================
// error handler
void magma_xerbla( const char *name, magma_int_t info );

const char* magma_strerror( magma_err_t error );


/* ------------------------------------------------------------
 *   -- MAGMA Auxiliary structures and functions
 * --------------------------------------------------------- */
typedef struct magma_timestr_s
{
  unsigned int sec;
  unsigned int usec;
} magma_timestr_t;

magma_timestr_t get_current_time(void);
double GetTimerValue(magma_timestr_t time_1, magma_timestr_t time_2);
void printout_devices();
void swp2pswp(char trans, magma_int_t n, magma_int_t *ipiv, magma_int_t *newipiv);

double magma_wtime( void );
double magma_sync_wtime( magma_queue_t queue );
size_t magma_strlcpy(char *dst, const char *src, size_t siz);
int magma_num_gpus( void );
int magma_is_devptr( const void* A );

// magma GPU-complex PCIe connection
magma_int_t magma_buildconnection_mgpu(  magma_int_t gnode[MagmaMaxGPUs+2][MagmaMaxGPUs+2], magma_int_t *nbcmplx, magma_int_t ngpu);

void magma_indices_1D_bcyclic( magma_int_t nb, magma_int_t ngpu, magma_int_t dev,
                               magma_int_t j0, magma_int_t j1,
                               magma_int_t* dj0, magma_int_t* dj1 );

#ifdef __cplusplus
}
#endif

#endif /* MAGMA_H */
