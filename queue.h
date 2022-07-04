//
// Created by lcaonmst on 30.01.2021.
//

#ifndef CACTI_QUEUE_H
#define CACTI_QUEUE_H

#include <stdbool.h>

#define BASE_SIZE 1024

typedef struct Queue {
    size_t queue_size;
    void** array;
    bool is_empty;
    bool is_full;
    size_t left;
    size_t right;
    size_t elements;
} Queue;

Queue* queue_create();

void queue_destroy(Queue*);

void queue_push(Queue*, void*);

void* queue_get(Queue*);

void* queue_get_kth(Queue*, size_t);

void queue_clear(Queue* queue, void (*deleter)(void*));

#endif //CACTI_QUEUE_H
