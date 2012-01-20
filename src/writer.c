#include "bplus.h"
#include "private/writer.h"

#include <fcntl.h> /* open */
#include <unistd.h> /* close, write, read */
#include <stdlib.h> /* malloc, free */
#include <string.h> /* memset */
#include <errno.h> /* memset */

#include <snappy-c.h>

int bp__writer_create(bp__writer_t* w, const char* filename) {
  off_t filesize;
  w->fd = open(filename,
               O_RDWR | O_APPEND | O_CREAT | O_EXLOCK,
               S_IWRITE | S_IREAD);
  if (w->fd == -1) return BP_EFILE;

  /* Determine filesize */
  filesize = lseek(w->fd, 0, SEEK_END);
  if (filesize == -1) return BP_EFILE;

  w->filesize = filesize;

  /* Nullify padding to shut up valgrind */
  memset(&w->padding, 0, sizeof(w->padding));

  return BP_OK;
}


int bp__writer_destroy(bp__writer_t* w) {
  if (close(w->fd)) return BP_EFILE;
  return BP_OK;
}


int bp__writer_read(bp__writer_t* w,
                    const uint32_t offset,
                    uint32_t* size,
                    void** data) {
  ssize_t read;
  char* cdata;

  if (w->filesize < offset + *size) return BP_EFILEREAD_OOB;

  /* Ignore empty reads */
  if (*size == 0) return BP_OK;

  cdata = malloc(*size);
  if (cdata == NULL) return BP_EALLOC;

  read = pread(w->fd, cdata, (size_t) *size, (off_t) offset);
  if (read != *size) {
    free(cdata);
    return BP_EFILEREAD;
  }

  /* no compression for small chunks */
  if (*size <= sizeof(w->padding)) {
    *data = cdata;
  } else {
    int ret = 0;

    char* uncompressed = NULL;
    size_t usize;

    if (snappy_uncompressed_length(cdata, *size, &usize) != SNAPPY_OK) {
      ret = BP_ESNAPPYD;
    } else {
      uncompressed = malloc(usize);
      if (uncompressed == NULL) {
        ret = BP_EALLOC;
      } else if (snappy_uncompress(cdata, *size, uncompressed, &usize) !=
                 SNAPPY_OK) {
        ret = BP_ESNAPPYD;
      } else {
        free(cdata);
        *data = uncompressed;
        *size = usize;
      }
    }

    if (ret) {
      free(cdata);
      free(uncompressed);
      return ret;
    }
  }

  return BP_OK;
}


int bp__writer_write(bp__writer_t* w,
                     const uint32_t size,
                     const void* data,
                     uint32_t* offset,
                     uint32_t* csize) {
  ssize_t written;
  uint32_t padding = sizeof(w->padding) - (w->filesize % sizeof(w->padding));

  /* Write padding */
  if (padding != sizeof(w->padding)) {
    written = write(w->fd, &w->padding, (size_t) padding);
    if (written != padding) return BP_EFILEWRITE;
    w->filesize += padding;
  }

  /* Ignore empty writes */
  if (size == 0) return BP_OK;

  /* head and smaller chunks shouldn't be compressed */
  if (size <= sizeof(w->padding)) {
    written = write(w->fd, data, (size_t) size);
    *csize = size;
  } else {
    int ret;
    size_t max_csize = snappy_max_compressed_length(size);
    size_t result_size;
    char* compressed = malloc(max_csize);
    if (compressed == NULL) return BP_EALLOC;

    result_size = max_csize;
    ret = snappy_compress(data, size, compressed, &result_size);
    if (ret != SNAPPY_OK) {
      free(compressed);
      return BP_ESNAPPYC;
    }

    *csize = (uint32_t) result_size;
    written = write(w->fd, compressed, (size_t) result_size);
    free(compressed);
  }

  if (written != *csize) return BP_EFILEWRITE;

  /* change offset */
  *offset = w->filesize;
  w->filesize += written;

  return BP_OK;
}


int bp__writer_find(bp__writer_t* w,
                    const uint32_t size,
                    void* data,
                    bp__writer_cb seek,
                    bp__writer_cb miss) {
  int ret = 0;
  uint32_t offset, size_tmp;

  /* Write padding first */
  ret = bp__writer_write(w, 0, NULL, NULL, NULL);
  if (ret) return ret;

  offset = w->filesize;
  size_tmp = size;

  /* Start seeking from bottom of file */
  while (offset >= size) {
    ret = bp__writer_read(w, offset - size, &size_tmp, &data);
    if (ret) break;

    /* Break if matched */
    if (seek(w, data) == 0) break;

    offset -= size;
  }

  /* Not found - invoke miss */
  if (offset < size) {
    ret = miss(w, data);
  }

  return ret;
}
