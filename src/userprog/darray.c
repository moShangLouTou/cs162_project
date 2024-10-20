#include "threads/malloc.h"
#include <string.h>
#include "userprog/darray.h"

bool darray_init(struct darray* darray) {
  darray->array_ = calloc(sizeof(struct file*), 16); // todo
  if (!darray->array_) {
    darray->size = 0;
    return false;
  }
  darray->size = 16;
  return true;
}

bool double_size(struct darray* darray) {
  void *new = realloc(darray->array_, darray->size * 2);
  if (!new) {
    return false;
  }
  darray->array_ = new;
  memset(darray->array_ + darray->size, 0, darray->size*4);
  darray->size *= 2;
  return true;
}

int free_index(struct darray* darray) {
  size_t i;
  for (i = 0; i < darray->size; i++) {
    if (darray->array_[i] == NULL) {
      break;
    }
  }
  if (i == darray->size && !double_size(darray)) {
    return -1;
  } 
  return i;
}

int add_a_file(struct darray* darray, struct file* file) {
  int index;
  if ((index = free_index(darray)) == -1) {
    return -1;
  }
  if (!file) {
    return -1;
  }
  darray->array_[index] = file;
  return index + 2;

}

struct file *get_file(struct darray* darray, int fd) {
  int index = fd - 2;
  if (index < 0 || index >= darray->size) {
    return NULL;
  }
  return darray->array_[index];
}

bool remove_a_file(struct darray* darray, int fd) {
  int index = fd - 2;
  if (index < 0 || index >= darray->size) {
    return false;
  }
  darray->array_[index] = NULL;
  return true;
}

void darray_destory(struct darray* darray) {
  if (!darray || !darray->array_) {
    return;
  }
  for (size_t i = 0; i < darray->size; i++) {
    file_close(darray->array_[i]);
  }
  free(darray->array_);
}