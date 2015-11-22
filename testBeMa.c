#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <limits.h>

#include "bench-timing.h"
#include "bema.h"

typedef int bool;
#define true  1
#define false 0

/* Benchmark duration in seconds.  */
#define BENCHMARK_DURATION	60
#define RAND_SEED		88

#define MIN_ALLOCATION_SIZE	8
#define MAX_ALLOCATION_SIZE	256


#define NUM_BLOCK_SIZES	8000

static volatile bool timeout;

static unsigned int random_block_sizes[NUM_BLOCK_SIZES];

/* Get a random block size with a uniform distribution.  */
static unsigned int
get_block_size_uniform(unsigned int min, unsigned int max)
{
    unsigned int range = max - min;
    unsigned int scale = RAND_MAX/range;
    unsigned int val = rand();
    return (unsigned int) (val/scale+min);
}
/* Alternating between small blocks and large blocks*/
static  unsigned int
get_block_size_alternate(int index)
{
    if (index == 0)

        return 64;
    if (random_block_sizes[index -1 ] == 64)
        return 64*1024;
    return 64;
}
/* Blocks with size power of two! DANGER !*/
static unsigned int
get_block_size_power2(int index)
{
    return 1U << index;
}
/* Initialize the array with the desired sizes: uniform, alternate or power of two */
static void
init_random_values (unsigned int num_blocks, unsigned int test)
{
    unsigned int i;
    if (test == 0)
    {
        for (i = 0; i < num_blocks; i++)
            random_block_sizes[i] = get_block_size_uniform(MIN_ALLOCATION_SIZE,MAX_ALLOCATION_SIZE);
    }
    else if (test == 1)
    {
        for (i = 0; i < num_blocks; i++)
            random_block_sizes[i] =  get_block_size_alternate(i);
    }
    else
    {
        for (i = 0; i < num_blocks; i++)
            random_block_sizes[i] = get_block_size_power2(i);
    }
}

/* Allocate the blocks with the sizes stored in the array random_block_sizes */
static void
malloc_loop(unsigned int num_blocks, void **ptr_arr, size_t *errors)
{

    unsigned int i=0;
    unsigned err = 0;
    for (i = 0; i < num_blocks; i++)
    {
        unsigned int next_block = random_block_sizes[i];
        ptr_arr[i] = Mem_Alloc(next_block);
        if (ptr_arr[i] == NULL)
        {
            err++;
        }
    }
    *errors = err;
}
/* Free the a block according with the exact or any pointer */
static void free_memory(unsigned int test, void *ptr, unsigned int block_size)
{
    if (test == 0)
    {
        Mem_Free(ptr);
    }
    else
    {
        int offset = rand() % block_size;
        Mem_Free(ptr+offset);
    }

}
/* Free the array of blocks with N pointers available in ptr_arr or N/2 pointers */
static size_t
do_free_benchmark(unsigned int num_blocks, unsigned int testOrder,
                  unsigned int testFree,void **ptr_arr)
{
    size_t iters = 0;
    unsigned int i;
    if (testOrder == 0)
    {
        for (i=0; i < num_blocks; i++)
        {
            free_memory(testFree,ptr_arr[i],random_block_sizes[i]);
            iters++;
        }
    }
    else
    {
        int indf = num_blocks;
        int start = 0;
        int im = 0;
        while ((indf - start) > 1)
        {
            im = (start+indf)/2;
            for (i = start; i < im; i++)
            {
                free_memory(testFree,ptr_arr[i],random_block_sizes[i]);
                iters++;
            }
            start = im;
        }
        // free the last block
        free_memory(testFree,ptr_arr[start],random_block_sizes[start]);
        iters++;
    }
    return iters;
}

static timing_t
do_benchmark (size_t num_blocks, int test_alloc, unsigned int test_order, unsigned int test_free, size_t *iters, size_t *errors)
{
    timing_t elapsed = 0;

    timing_t start, stop;
    void *working_set[num_blocks];
    memset (working_set, 0, sizeof (working_set));

    TIMING_NOW (start);
    init_random_values(num_blocks,test_alloc);
    malloc_loop(num_blocks,working_set, errors);
    *iters = do_free_benchmark(num_blocks,test_order,test_free,working_set);
    TIMING_NOW (stop);
    TIMING_DIFF (elapsed, start, stop);

    return elapsed;
}

static void
alarm_handler (int signum)
{
    timeout = true;
}

/* A single test benchmarkin defined by the values of variables:
test_alloc, test_order and test_free

test_alloc: 0 small blocks uniformally distributed
             1 alternates between small and large blocks
             2 blocks with size power of two
 test_order:
             0 allocates N blocks and frees all N of them
             1 repeatedly allocates N blocks and frees N/2 until the end
 test_free:
             0 free with the exact pointer
             1 free with any pointer
*/
static void mem_bench(size_t num_blocks, int test_alloc, unsigned int test_order, unsigned int test_free)
{
    timing_t cur;
    size_t iters, errors;
    unsigned long res;
    struct sigaction act;
    double total_i,total_s,total_err;

    srand(time(NULL));

    TIMING_INIT (res);

    (void) res;
    iters = 0;
    errors = 0;
    cur = 0;

    memset (&act, 0, sizeof (act));
    act.sa_handler = &alarm_handler;

    sigaction (SIGALRM, &act, NULL);
    alarm (BENCHMARK_DURATION);

    cur = do_benchmark (num_blocks, test_alloc,test_order, test_free, &iters,&errors);

    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    total_i = iters;
    total_s = cur;
    total_err = errors;
    printf("Number of blocks %lu\n", num_blocks);
    printf("duration %.3f nano seconds\n", total_s);
    printf("iterations %.3f\n", total_i);
    printf("errors %.3f\n",total_err);
    printf("Mean time per iteration %.3f nano seconds\n", total_s / total_i);
    printf("max_rss %lu Kb\n", usage.ru_maxrss);

}


static void usage(const char *name)
{
    fprintf (stderr, "%s: <num_blocks> [<test allocation:0,1,2> <test order:0,1> <test free:0,1>]\n", name);
    exit (1);
}
/*
You can start the program with a single argument which denotes the number of blocks
to be allocated and in such case it will execute the 12 tests.
Otherwise you can run a single test by providing as arguments three values for the
allocation type, order and free as described in the project document.
For example: testmem 10 0 0 0 will execute the program to allocate 10 small blocks,
then free all of them and the free function uses the exact pointer returned
by the allocation function.
*/

int
main (int argc, char **argv)
{

    unsigned int k=0;
    unsigned int test_alloc, test_order, test_free;
    size_t num_blocks;
    bool mode_single=false;

    if (argc == 1)
        num_blocks = 1;
    else if (argc == 2)
    {
        long ret;
        errno = 0;
        ret = strtol(argv[1], NULL, 10);
        if (errno || ret == 0)
            usage(argv[0]);
        num_blocks = ret;
    }
    else if (argc == 5)
    {
        long ret;
        errno = 0;
        ret = strtol(argv[1], NULL, 10);
        if (errno || ret == 0)
            usage(argv[0]);
        num_blocks = ret;
        if (num_blocks > NUM_BLOCK_SIZES)
            usage(argv[0]);
        ret = strtol(argv[2], NULL, 10);
        if (errno)
            usage(argv[0]);
        test_alloc = ret;
        if (test_alloc > 2 || test_alloc < 0)
            usage(argv[0]);
        ret = strtol(argv[3], NULL, 10);
        if (errno )
            usage(argv[0]);
        test_order = ret;
        if (test_order > 1 || test_order < 0)
            usage(argv[0]);
        ret = strtol(argv[4], NULL, 10);
        if (errno )
            usage(argv[0]);
        test_free = ret;
        if (test_free > 1 || test_free < 0)
            usage(argv[0]);
        mode_single = true;
    }
    else
        usage(argv[0]);

    printf("----------- Start benchmarking memory allocator ----------------------\n");
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    printf("Number of blocks %lu\n", num_blocks);
    printf("process memory usage %lu Kb\n",usage.ru_maxrss);
    /* Make a single test with the values provides as arguments */
    if (mode_single == true)
    {
        printf("-------------------- Test [%d,%d,%d] ------------------------\n",test_alloc,test_order,test_free);
        mem_bench(num_blocks, test_alloc,test_order,test_free);

    }
    else
    {
        /* Make all the 12 tests */
        for (test_alloc=0; test_alloc <= 2; test_alloc++)
        {
            for (test_order = 0; test_order <= 1; test_order ++)
            {
                for (test_free = 0; test_free <= 1; test_free ++)
                {
                    k++;
                    printf("-------------------- Test %d [%d,%d,%d] ------------------------\n",k,test_alloc,test_order,test_free);
                    mem_bench(num_blocks, test_alloc,test_order,test_free);

                }
            }
        }
    }
    return 0;
}

