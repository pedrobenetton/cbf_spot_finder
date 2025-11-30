#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <glob.h>
#include <mpi.h>
#include <omp.h>
#include <math.h>
#include "cbf.h"

#define HEADER_END_MARK "\x0c\x1a\x04\xd5"

typedef struct {
    int x, y;
    int32_t intensity;
} Spot;

static inline int is_strong_spot(const int32_t *pixels, int width, int height, int x, int y, int32_t threshold) {
    int idx = y * width + x;
    int32_t val = pixels[idx];
    if (val < threshold) return 0;

    // Check 5x5 neighbor local maximum
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            if (pixels[ny * width + nx] > val)
                return 0;
        }
    }
    return 1;
}

Spot *find_strong_spots(const int32_t *pixels, int width, int height, int *out_spot_count, int32_t threshold) {
    int nthreads = omp_get_max_threads();
    Spot **local_spots = (Spot **)calloc(nthreads, sizeof(Spot *));
    int *local_counts = (int *)calloc(nthreads, sizeof(int));
    int *local_caps   = (int *)calloc(nthreads, sizeof(int));

    if (!local_spots || !local_counts || !local_caps) {
        fprintf(stderr, "Memory allocation failed in find_strong_spots\n");
        *out_spot_count = 0;
        return NULL;
    }

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        int cap = 1024;
        Spot *buf = (Spot *)malloc(cap * sizeof(Spot));
        int count = 0;

        #pragma omp for schedule(dynamic)
        for (int y = 1; y < height - 1; y++) {
            for (int x = 1; x < width - 1; x++) {
                if (is_strong_spot(pixels, width, height, x, y, threshold)) {
                    if (count >= cap) {
                        cap *= 2;
                        buf = (Spot *)realloc(buf, cap * sizeof(Spot));
                    }
                    buf[count++] = (Spot){x, y, pixels[y * width + x]};
                }
            }
        }

        local_spots[tid]  = buf;
        local_counts[tid] = count;
        local_caps[tid]   = cap;
    }

    int total = 0;
    for (int t = 0; t < nthreads; t++)
        total += local_counts[t];

    Spot *spots = (Spot *)malloc(total * sizeof(Spot));
    int offset = 0;
    for (int t = 0; t < nthreads; t++) {
        memcpy(spots + offset, local_spots[t], local_counts[t] * sizeof(Spot));
        offset += local_counts[t];
        free(local_spots[t]);
    }

    free(local_spots);
    free(local_counts);
    free(local_caps);

    *out_spot_count = total;
    return spots;
}

static unsigned char *find_header_end(unsigned char *data, long filesize) {
    for (long i = 0; i < filesize - 4; i++) {
        if (memcmp(data + i, HEADER_END_MARK, 4) == 0)
            return data + i + 4;
    }
    return NULL;
}

static int parse_cbf_header(const unsigned char *file_content, unsigned int *num_elements, unsigned int *binary_size) {
    char *saveptr = NULL;
    char *copy = strdup((const char *)file_content);
    if (!copy) return 0;

    char *line = strtok_r(copy, "\n", &saveptr);
    while (line != NULL) {
        sscanf(line, "X-Binary-Number-of-Elements: %u", num_elements);
        sscanf(line, "X-Binary-Size: %u", binary_size);
        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(copy);
    return (*num_elements && *binary_size);
}

static int32_t *decode_pixel_data(unsigned char *header_end, unsigned int binary_size, unsigned int num_elements) {
    int32_t *pixels = (int32_t *)malloc(num_elements * sizeof(int32_t));
    if (!pixels) return NULL;

    unsigned int bytes_written = decodeCBFuin32(header_end, binary_size, (unsigned char *)pixels);
    if (bytes_written == 0) {
        free(pixels);
        return NULL;
    }
    return pixels;
}

int32_t* read_cbf_pixels(const char *filename, unsigned int *num_elements) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open file");
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *file_content = (unsigned char *)malloc(filesize);
    if (!file_content) {
        perror("Failed to allocate memory");
        fclose(fp);
        return NULL;
    }

    if (fread(file_content, 1, filesize, fp) != filesize) {
        perror("Failed to read file");
        free(file_content);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    unsigned char *header_end = find_header_end(file_content, filesize);
    if (!header_end) {
        fprintf(stderr, "Failed to find binary section header in %s\n", filename);
        free(file_content);
        return NULL;
    }

    unsigned int binary_size = 0;
    if (!parse_cbf_header(file_content, num_elements, &binary_size)) {
        fprintf(stderr, "Failed to parse header in %s\n", filename);
        free(file_content);
        return NULL;
    }

    int32_t *pixels = decode_pixel_data(header_end, binary_size, *num_elements);
    free(file_content);
    return pixels;
}

static int32_t calculate_threshold(const int32_t *pixels, unsigned int num_elements) {
    double sum = 0.0, sumsq = 0.0;
    #pragma omp parallel for reduction(+: sum, sumsq)
    for (int64_t i = 0; i < num_elements; i++) {
        double v = (double)pixels[i];
        sum += v;
        sumsq += v * v;
    }

    double mean = sum / num_elements;
    double var = sumsq / num_elements - mean * mean;
    double stddev = var > 0 ? sqrt(var) : 0.0;
    return (int32_t)round(mean + 5.0 * stddev);
}

static void process_file(const char *filename, int rank) {
    unsigned int num_elements = 0;
    //printf("Rank %d processing %s\n", rank, filename);

    int32_t *pixels = read_cbf_pixels(filename, &num_elements);
    if (!pixels) {
        fprintf(stderr, "Rank %d: Skipping %s due to error.\n", rank, filename);
        return;
    }

    int32_t threshold = calculate_threshold(pixels, num_elements);
    int width = 1475, height = 1679;

    int spot_count = 0;
    Spot *spots = find_strong_spots(pixels, width, height, &spot_count, threshold);

    //printf("  Found %d strong spots in %s\n", spot_count, filename);
    // Uncomment for detailed per-spot output
    for (int s = 0; s < spot_count; s++){
        printf("%s spot %4d: (x=%4d, y=%4d)  intensity=%d\n",
            filename, s + 1, spots[s].x, spots[s].y, spots[s].intensity);
    }

    free(spots);
    free(pixels);
}

static char **distribute_filenames(glob_t *glob_result, int rank, int size, size_t *file_count) {
    if (rank == 0)
        *file_count = glob_result->gl_pathc;

    MPI_Bcast(file_count, 1, MPI_UNSIGNED_LONG, 0, MPI_COMM_WORLD);
    char **filenames = malloc(*file_count * sizeof(char *));

    for (size_t i = 0; i < *file_count; i++) {
        int len = 0;
        if (rank == 0)
            len = strlen(glob_result->gl_pathv[i]) + 1;
        MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);

        filenames[i] = malloc(len);
        if (rank == 0)
            strcpy(filenames[i], glob_result->gl_pathv[i]);
        MPI_Bcast(filenames[i], len, MPI_CHAR, 0, MPI_COMM_WORLD);
    }

    return filenames;
}

int main(int argc, char *argv[]) {

    MPI_Init(&argc, &argv);
    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if (rank == 0)
            fprintf(stderr, "Usage: %s <pattern or file list>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    glob_t glob_result;
    memset(&glob_result, 0, sizeof(glob_result));

    if (rank == 0) {
        const char *pattern = argv[1];
        int ret = glob(pattern, GLOB_TILDE, NULL, &glob_result);
        if (ret != 0) {
            fprintf(stderr, "No files matched pattern: %s\n", pattern);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        //printf("Found %zu files matching pattern '%s'\n", glob_result.gl_pathc, pattern);
    }

    size_t file_count;
    char **filenames = distribute_filenames(&glob_result, rank, size, &file_count);

    for (size_t i = rank; i < file_count; i += size)
        process_file(filenames[i], rank);

    if (rank == 0) {
        globfree(&glob_result);
    }

    for (size_t i = 0; i < file_count; i++)
        free(filenames[i]);
    free(filenames);

    MPI_Barrier(MPI_COMM_WORLD);
    double end = MPI_Wtime();
    MPI_Finalize();
    double elapsed = end - start;

    if (rank == 0) {
        printf("Total execution time: %.6f seconds\n", elapsed);
    }

    return 0;
}
