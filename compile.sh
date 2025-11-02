mpicc -fopenmp -O2 -o cbf_max_mpi cbf_max.c cbf.cpp -I. -lm

# To run:
# export OMP_NUM_THREADS=8
# mpirun -np 8 ./cbf_max_mpi 'cbfs/lys_0001_000*.cbf'

