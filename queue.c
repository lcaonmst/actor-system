//
// Created by lcaonmst on 30.01.2021.
//
#include <stdlib.h>
#include "queue.h"
#include "err.h"

Queue* queue_create() {
    Queue* new_queue = malloc(sizeof(Queue));
    if (new_queue == NULL) {
        syserr("queue malloc");
    }
    new_queue->queue_size = BASE_SIZE;
    new_queue->array = malloc(BASE_SIZE * sizeof(void*));
    if (new_queue->array == NULL) {
        syserr("array malloc");
    }
    new_queue->is_empty = true;
    new_queue->is_full = false;
    new_queue->left = 0;
    new_queue->right = 0;
    new_queue->elements = 0;
    return new_queue;
}

void queue_destroy(Queue* queue) {
    if (queue == NULL) {
        return;
    }
    free(queue->array);
    free(queue);
}

static void increase(Queue* queue) {
    void* new_ptr = realloc(queue->array, 2 * queue->queue_size);
    if (new_ptr == NULL) {
        syserr("array realloc");
    }
    queue->queue_size *= 2;
    queue->array = new_ptr;
    queue->is_full = false;
}

void queue_push(Queue* queue, void* data) {
    if (queue->is_full) {
        increase(queue);
    }
    queue->is_empty = false;
    queue->array[queue->right] = data;
    queue->right = (queue->right + 1) % queue->queue_size;
    queue->elements++;
    if (queue->left == queue->right) {
        queue->is_full = true;
    }
}

void* queue_get(Queue* queue) {
    if (queue->is_empty) {
        return NULL;
    }
    void* res = queue->array[queue->left];
    queue->is_full = false;
    queue->left = (queue->left + 1) % queue->queue_size;
    queue->elements++;
    if (queue->left == queue->right) {
        queue->is_empty = true;
    }
    return res;
}

void* queue_get_kth(Queue* queue, size_t k) {
    if (k < 0 || k >= queue->elements) {
        return NULL;
    }
    size_t ind = (queue->left + k) % queue->elements;
    return queue->array[ind];
}

void queue_clear(Queue* queue, void (*deleter)(void*)) {
    size_t siz = queue->elements;
    while (siz--) {
        deleter(queue_get(queue));
    }
}