#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <mpi.h>

/* row-major order */
#define ind(i,j) ((j)*(bx+2)+(i))

int ind_f(int i, int j, int bx)
{
    return ind(i, j);
}

void setup(int rank, int proc, int argc, char **argv,
           int *n_ptr, int *energy_ptr, int *niters_ptr, int *px_ptr, int *py_ptr, int *final_flag);

void init_sources(int bx, int by, int offx, int offy, int n,
                  const int nsources, int sources[][2], int *locnsources_ptr, int locsources[][2]);

void alloc_bufs(int bx, int by, double **aold_ptr, double **anew_ptr,
                double **sendBuffer_ptr, double **recvBuffer_ptr);

void pack_data(int bx, int by, double *aold, double *sendBuffer);

void unpack_data(int bx, int by, double *aold, double *recvBuffer);

void update_grid(int bx, int by, double *aold, double *anew, double *heat_ptr);

void free_bufs(double *aold, double *anew,
               double *sendBuffer, double *recvBuffer);


void setup(int rank, int proc, int argc, char **argv,
           int *n_ptr, int *energy_ptr, int *niters_ptr, int *px_ptr, int *py_ptr, int *final_flag)
{
    int n, energy, niters, px, py;

    (*final_flag) = 0;

    if (argc < 6) {
        if (!rank)
            printf("usage: stencil_mpi <n> <energy> <niters> <px> <py>\n");
        (*final_flag) = 1;
        return;
    }

    n = atoi(argv[1]);  /* nxn grid */
    energy = atoi(argv[2]);     /* energy to be injected per iteration */
    niters = atoi(argv[3]);     /* number of iterations */
    px = atoi(argv[4]); /* 1st dim processes */
    py = atoi(argv[5]); /* 2nd dim processes */

    if (px * py != proc)
        MPI_Abort(MPI_COMM_WORLD, 1);   /* abort if px or py are wrong */
    if (n % px != 0)
        MPI_Abort(MPI_COMM_WORLD, 2);   /* abort px needs to divide n */
    if (n % py != 0)
        MPI_Abort(MPI_COMM_WORLD, 3);   /* abort py needs to divide n */

    (*n_ptr) = n;
    (*energy_ptr) = energy;
    (*niters_ptr) = niters;
    (*px_ptr) = px;
    (*py_ptr) = py;
}



void init_sources(int bx, int by, int offx, int offy, int n,
                  const int nsources, int sources[][2], int *locnsources_ptr, int locsources[][2])
{
    int i, locnsources = 0;

    sources[0][0] = n / 2;
    sources[0][1] = n / 2;
    sources[1][0] = n / 3;
    sources[1][1] = n / 3;
    sources[2][0] = n * 4 / 5;
    sources[2][1] = n * 8 / 9;

    for (i = 0; i < nsources; ++i) {    /* determine which sources are in my patch */
        int locx = sources[i][0] - offx;
        int locy = sources[i][1] - offy;
        if (locx >= 0 && locx < bx && locy >= 0 && locy < by) {
            locsources[locnsources][0] = locx + 1;      /* offset by halo zone */
            locsources[locnsources][1] = locy + 1;      /* offset by halo zone */
            locnsources++;
        }
    }

    (*locnsources_ptr) = locnsources;
}


void alloc_bufs(int bx, int by, double **aold_ptr, double **anew_ptr,
                double **sendBuffer_ptr, double **recvBuffer_ptr) {
    double *aold, *anew;
    double *sendBuffer, *recvBuffer;

    /* allocate two working arrays with halo zones */
    anew = (double *) malloc((bx + 2) * (by + 2) * sizeof(double));
    aold = (double *) malloc((bx + 2) * (by + 2) * sizeof(double));
    memset(aold, 0, (bx + 2) * (by + 2) * sizeof(double));
    memset(anew, 0, (bx + 2) * (by + 2) * sizeof(double));

    /* allocate combined send and receive buffers */
    // The size might need to be adjusted depending on the exact communication pattern
    sendBuffer = (double *) malloc(4 * bx * by * sizeof(double)); 
    recvBuffer = (double *) malloc(4 * bx * by * sizeof(double)); 
    memset(sendBuffer, 0, 4 * bx * by * sizeof(double));
    memset(recvBuffer, 0, 4 * bx * by * sizeof(double));

    (*aold_ptr) = aold;
    (*anew_ptr) = anew;
    (*sendBuffer_ptr) = sendBuffer;
    (*recvBuffer_ptr) = recvBuffer;
}

void free_bufs(double *aold, double *anew,
               double *sendBuffer, double *recvBuffer) {
    free(aold);
    free(anew);
    free(sendBuffer);
    free(recvBuffer);
}

void pack_data(int bx, int by, double *aold, double *sendBuffer) {
    int i, j, idx;

    // Assuming the sendBuffer is divided into four segments for north, south, east, and west

    // Pack north edge
    for (i = 0; i < bx; ++i) {
        sendBuffer[i] = aold[ind(i + 1, 1)]; // #1 row
    }

    // Pack south edge
    idx = bx; // Start index for south edge
    for (i = 0; i < bx; ++i) {
        sendBuffer[idx + i] = aold[ind(i + 1, by)]; // #(by) row
    }

    // Pack east edge
    idx = 2 * bx; // Start index for east edge
    for (i = 0; i < by; ++i) {
        sendBuffer[idx + i] = aold[ind(bx, i + 1)]; // #(bx) col
    }

    // Pack west edge
    idx = 2 * bx + by; // Start index for west edge
    for (i = 0; i < by; ++i) {
        sendBuffer[idx + i] = aold[ind(1, i + 1)]; // #1 col
    }
}

void unpack_data(int bx, int by, double *aold, double *recvBuffer) {
    int i, idx;

    // Unpack north edge
    for (i = 0; i < bx; ++i) {
        aold[ind(i + 1, 0)] = recvBuffer[i]; // #0 row
    }

    // Unpack south edge
    idx = bx; // Start index for south edge
    for (i = 0; i < bx; ++i) {
        aold[ind(i + 1, by + 1)] = recvBuffer[idx + i]; // #(by+1) row
    }

    // Unpack east edge
    idx = 2 * bx; // Start index for east edge
    for (i = 0; i < by; ++i) {
        aold[ind(bx + 1, i + 1)] = recvBuffer[idx + i]; // #(bx+1) col
    }

    // Unpack west edge
    idx = 2 * bx + by; // Start index for west edge
    for (i = 0; i < by; ++i) {
        aold[ind(0, i + 1)] = recvBuffer[idx + i]; // #0 col
    }
}

void update_grid(int bx, int by, double *aold, double *anew, double *heat_ptr) {
    int i, j;
    double heat = 0.0;

    for (i = 1; i < bx + 1; ++i) {
        for (j = 1; j < by + 1; ++j) {
            anew[ind(i, j)] =
                anew[ind(i, j)] / 2.0 + (aold[ind(i - 1, j)] + aold[ind(i + 1, j)] +
                                         aold[ind(i, j - 1)] + aold[ind(i, j + 1)]) / 4.0 / 2.0;
            heat += anew[ind(i, j)];
        }
    }

    (*heat_ptr) = heat;
}



int main(int argc, char **argv) {
    int rank, size;
    int n, energy, niters, px, py;

    int rx, ry;
    int bx, by, offx, offy;

    /* three heat sources */
    const int nsources = 3;
    int sources[nsources][2];
    int locnsources; /* number of sources in my area */
    int locsources[nsources][2]; /* sources local to my rank */

    double t1, t2;

    int iter, i;

    double *sendBuffer, *recvBuffer;
    double *aold, *anew, *tmp;

    double heat, rheat;

    int final_flag;

    /* initialize MPI environment */
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* argument checking and setting */
    setup(rank, size, argc, argv, &n, &energy, &niters, &px, &py, &final_flag);

    if (final_flag == 1) {
        MPI_Finalize();
        exit(0);
    }

    /* determine my coordinates (x,y) -- rank=x*a+y in the 2d processor array */
    rx = rank % px;
    ry = rank / px;

    /* decompose the domain */
    bx = n / px; /* block size in x */
    by = n / py; /* block size in y */
    offx = rx * bx; /* offset in x */
    offy = ry * by; /* offset in y */

    /* initialize three heat sources */
    init_sources(bx, by, offx, offy, n, nsources, sources, &locnsources, locsources);

    /* allocate working arrays & combined communication buffers */
    alloc_bufs(bx, by, &aold, &anew, &sendBuffer, &recvBuffer);

    /* Prepare counts and displacements for MPI_Alltoallv */
    int sendCounts[size], recvCounts[size];
    int sdispls[size], rdispls[size];

    // Assuming each process sends and receives an equal amount of data
    int dataSize = 4 * bx * by / size; // Adjust based on your data distribution

    for (int i = 0; i < size; ++i) {
        sendCounts[i] = dataSize;
        recvCounts[i] = dataSize;
        sdispls[i] = i * dataSize;
        rdispls[i] = i * dataSize;
    }

    t1 = MPI_Wtime(); /* take time */

    for (iter = 0; iter < niters; ++iter) {
        /* refresh heat sources */
        for (i = 0; i < locnsources; ++i) {
            aold[ind(locsources[i][0], locsources[i][1])] += energy; /* heat source */
        }

        /* pack data into the send buffer */
        pack_data(bx, by, aold, sendBuffer);

        /* exchange data with neighbors using MPI_Alltoallv */
        MPI_Alltoallv(sendBuffer, sendCounts, sdispls, MPI_DOUBLE,
                      recvBuffer, recvCounts, rdispls, MPI_DOUBLE,
                      MPI_COMM_WORLD);

        /* unpack data from the receive buffer */
        unpack_data(bx, by, aold, recvBuffer);

        /* update grid points */
        update_grid(bx, by, aold, anew, &heat);

        /* swap working arrays */
        tmp = anew;
        anew = aold;
        aold = tmp;
    }

    t2 = MPI_Wtime();

    /* free working arrays and combined communication buffers */
    free_bufs(aold, anew, sendBuffer, recvBuffer);

    /* get final heat in the system */
    MPI_Allreduce(&heat, &rheat, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    if (!rank) {
        printf("[%i] last heat: %f time: %f\n", rank, rheat, t2 - t1);
    }

    MPI_Finalize();
    return 0;
}





