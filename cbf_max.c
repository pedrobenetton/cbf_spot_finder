#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <glob.h>
#include <mpi.h>
#include <omp.h>
#include "cbf.h"

#define HEADER_END_MARK "\x0c\x1a\x04\xd5"

int32_t find_max_pixel(int32_t *pixels, unsigned int num_elements) {
    if (!pixels || num_elements == 0)
        return 0;

    int32_t max_pixel = pixels[0];
    // OpenMP is used here to split the for loop
    // iterations across multiple threads
    // reduction(max:max_pixel) obtains the maximum value
    // found by any thread and assigns it to max_pixel
    #pragma omp parallel for reduction(max:max_pixel)
    for (unsigned int j = 1; j < num_elements; j++) {
        if (pixels[j] > max_pixel)
            max_pixel = pixels[j];
    }

    return max_pixel;
}

int32_t* read_cbf_pixels(const char *filename, unsigned int *num_elements) {

    FILE *fp = fopen(filename, "rb");
    // fp will be NULL if file opneing fails
    if (!fp) {
        perror("Failed to open file");
        return NULL;
    }

    // moves the file pointer to the end of the file
    fseek(fp, 0, SEEK_END);

    // get current file pointer position, which gives the total file size in bytes
    long filesize = ftell(fp);

    // moves the file pointer to the start of the file
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

    unsigned char *header_end = NULL;
    // Loops over the entire file content except the last 3 bytes (because HEADER_END_MARK is 4 bytes long)
    for (long i = 0; i < filesize - 4; i++) {
        // Compares 4 bytes of the file at position i with the predefined marker
        if (memcmp(file_content + i, HEADER_END_MARK, 4) == 0) {
            // The pointer is now the start of the binary pixel data
            header_end = file_content + i + 4;
            break;
        }
    }

    if (!header_end) {
        fprintf(stderr, "Failed to find binary section header in %s\n", filename);
        free(file_content);
        return NULL;
    }

    unsigned int binary_size = 0;
    char *saveptr = NULL;
    char *line = strtok_r((char *)file_content, "\n", &saveptr);
    while (line != NULL) {
        if (sscanf(line, "X-Binary-Number-of-Elements: %u", num_elements) == 1) {}
        if (sscanf(line, "X-Binary-Size: %u", &binary_size) == 1) {}
        line = strtok_r(NULL, "\n", &saveptr);
    }

    if (*num_elements == 0 || binary_size == 0) {
        fprintf(stderr, "Failed to parse number of elements or binary size in %s\n", filename);
        free(file_content);
        return NULL;
    }

    int32_t *pixels = (int32_t *)malloc((*num_elements) * sizeof(int32_t));
    if (!pixels) {
        perror("Failed to allocate memory for pixels");
        free(file_content);
        return NULL;
    }

    unsigned int bytes_written = decodeCBFuin32(header_end, binary_size, (unsigned char *)pixels);
    if (bytes_written == 0) {
        fprintf(stderr, "Decompression failed for %s\n", filename);
        free(pixels);
        free(file_content);
        return NULL;
    }

    free(file_content);
    return pixels;
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if (rank == 0)
            fprintf(stderr, "Usage: %s <pattern or file list>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    // typedef struct {
    //   size_t gl_pathc;   // number of matched paths
    //   char **gl_pathv;   // array of matched path strings
    //   size_t gl_offs;    // reserved slots at the start of gl_pathv
    // } glob_t;

    // glob_t is a Structure defined by glob.h (shown above)
    glob_t glob_result;

    // initializes all bytes in the structure to zero
    memset(&glob_result, 0, sizeof(glob_result));

    // at rank 0, perform glob
    if (rank == 0) {
        const char *pattern = argv[1];
        int ret = glob(pattern, GLOB_TILDE, NULL, &glob_result);
        if (ret != 0) {
            fprintf(stderr, "No files matched pattern: %s\n", pattern);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        printf("Found %zu files matching pattern '%s'\n", glob_result.gl_pathc, pattern);
    }

    // size_t comes from <stddef.h> (and <stdio.h> indirectly) and is used to
    // represent the size of objects in memory or the length of arrays
    size_t file_count = glob_result.gl_pathc;
    MPI_Bcast(
        &file_count,            // address of the variable to send/receive
        1,                      // number of elements being sent
        MPI_UNSIGNED_LONG,      // MPI type corresponding to unsigned long (matches size_t)
        0,                      // rank of the root process
        MPI_COMM_WORLD);        // communicator

    // pointer to an array of strings
    char **filenames = NULL;

    if (rank == 0) {
        // rank 0 doesn't need to allocate memory for 
        // filenames because it already has them from the glob
        filenames = glob_result.gl_pathv;
    } else {
        // allocate memory for filenames on all other ranks
        filenames = malloc(file_count * sizeof(char *));
    }

    for (size_t i = 0; i < file_count; i++) {
        int len = 0;

        if (rank == 0) {
            len = strlen(glob_result.gl_pathv[i]) + 1; // include null terminator
        }

        // Broadcast length to all other ranks
        MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD);

        // Allocate buffer on non-zero ranks
        char *buf = malloc(len);
        if (!buf) {
            fprintf(stderr, "Rank %d failed to allocate buffer\n", rank);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        if (rank == 0) {
            strcpy(buf, glob_result.gl_pathv[i]);
        }

        // Broadcast string
        MPI_Bcast(buf, len, MPI_CHAR, 0, MPI_COMM_WORLD);

        if (rank != 0) {
            filenames[i] = buf; // store received string
        } else {
            free(buf); // rank 0 doesn't need duplicate
        }
    }

    for (size_t i = rank; i < file_count; i += size) {
        const char *filename = filenames[i];
        unsigned int num_elements = 0;
        printf("Rank %d processing %s\n", rank, filename);

        int32_t *pixels = read_cbf_pixels(filename, &num_elements);
        if (!pixels) {
            fprintf(stderr, "Rank %d: Skipping %s due to error.\n", rank, filename);
            continue;
        }

        int32_t max_pixel = find_max_pixel(pixels, num_elements);
        printf("  Max pixel value for %s: %d\n", filename, max_pixel);
        free(pixels);
    }

    // Free allocated memory for glob results and filenames
    if (rank == 0)
        globfree(&glob_result);
    else {
        for (size_t i = 0; i < file_count; i++)
            free(filenames[i]);
        free(filenames);
    }

    MPI_Finalize();
    return 0;
}