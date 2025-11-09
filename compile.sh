module load mpi
mpicc -fopenmp -O2 -o cbf_max_mpi cbf_max.c cbf.cpp -I. -lm
