#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "cbf.h"

#define HEADER_END_MARK "\x0c\x1a\x04\xd5"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.cbf>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    // Open file
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("Failed to open file");
        return 1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *file_content = (unsigned char *)malloc(filesize);
    if (!file_content) {
        perror("Failed to allocate memory for file");
        fclose(fp);
        return 1;
    }

    fread(file_content, 1, filesize, fp);
    fclose(fp);

    // Find header end
    unsigned char *header_end = NULL;
    for (long i = 0; i < filesize - 4; i++) {
        if (memcmp(file_content + i, HEADER_END_MARK, 4) == 0) {
            header_end = file_content + i + 4;
            break;
        }
    }

    if (!header_end) {
        fprintf(stderr, "Failed to find binary section header\n");
        free(file_content);
        return 1;
    }

    // Parse number of elements and binary size
    unsigned int num_elements = 0;
    unsigned int binary_size = 0;

    char *line = strtok((char *)file_content, "\n");
    while (line != NULL) {
        if (sscanf(line, "X-Binary-Number-of-Elements: %u", &num_elements) == 1) {}
        if (sscanf(line, "X-Binary-Size: %u", &binary_size) == 1) {}
        line = strtok(NULL, "\n");
    }

    if (num_elements == 0 || binary_size == 0) {
        fprintf(stderr, "Failed to parse number of elements or binary size\n");
        free(file_content);
        return 1;
    }

    // Allocate output buffer as int32_t
    int32_t *pixels = (int32_t *)malloc(num_elements * sizeof(int32_t));
    if (!pixels) {
        perror("Failed to allocate memory for pixels");
        free(file_content);
        return 1;
    }

    // Decompress
    unsigned int bytes_written = decodeCBFuin32(header_end, binary_size, (unsigned char *)pixels);
    if (bytes_written == 0) {
        fprintf(stderr, "Decompression failed\n");
        free(pixels);
        free(file_content);
        return 1;
    }

    // Find max pixel
    int32_t max_pixel = 0;
    for (unsigned int i = 0; i < num_elements; i++) {
        if (pixels[i] > max_pixel) max_pixel = pixels[i];
    }

    printf("Max pixel value: %d\n", max_pixel);

    free(pixels);
    free(file_content);

    return 0;
}
