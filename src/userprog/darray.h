#ifndef USERPROG_DARRAY_H
#define USERPROG_DARRAY_H
#include "filesys/file.h"
#include <stddef.h>
#include <stdbool.h>


struct darray {
    struct file **array_;
    size_t size;
};

bool darray_init(struct darray*);

bool double_size(struct darray*);

int free_index(struct darray*);

int add_a_file(struct darray*, struct file*);

struct file *get_file(struct darray* darray, int );

bool remove_a_file(struct darray*, int);

void darray_destory(struct darray*);


#endif