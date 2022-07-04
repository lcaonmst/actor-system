#include <stdlib.h>
#include <pthread.h>
#include "cacti.h"
#include "queue.h"
#include "err.h"

typedef struct Thread_pool {
    pthread_mutex_t mutex;
    pthread_cond_t is_work;
    size_t working_threads;
    size_t threads;
    Queue* queue;
    bool enabled;
    pthread_t threads_arr[POOL_SIZE];
} Thread_pool;

typedef struct Actor_data {
    actor_id_t actor_id;
    role_t *role;
    pthread_mutex_t mutex_kill;
    pthread_mutex_t mutex_state;
    bool killed;
    void* stateptr;
} Actor_data;

typedef struct Func_data {
    actor_id_t actor_id;
    message_t message;
} Func_data;

Thread_pool* thread_pool = NULL;
Queue* actors_queue = NULL;
size_t alive_actors;
__thread actor_id_t self_actor;

Actor_data* find(actor_id_t actor_id) {
    for (size_t i = 0; i < actors_queue->elements; i++) {
        Actor_data* ptr = queue_get_kth(actors_queue, i);
        if (ptr != NULL && ptr->actor_id == actor_id) {
            return ptr;
        }
    }
    return NULL;
}

void thread_pool_destroy() {
    if (pthread_mutex_unlock(&thread_pool->mutex) != 0) {
        syserr("mutex unlock");
    }
    if (pthread_mutex_destroy(&thread_pool->mutex) != 0) {
        syserr("mutex destroy");
    }
    if (pthread_cond_destroy(&thread_pool->is_work) != 0) {
        syserr("cond destroy");
    }
    queue_destroy(thread_pool->queue);
}

void actor_delete(void* data) {
    Actor_data* actor_data = (Actor_data*)data;
    if (pthread_mutex_destroy(&actor_data->mutex_kill) != 0) {
        syserr("mutex destroy");
    }
    if (pthread_mutex_destroy(&actor_data->mutex_state) != 0) {
        syserr("mutex destroy");
    }
    free(actor_data);
}

void execute(Func_data* func_data, Actor_data* my_actor) {
    if (pthread_mutex_lock(&my_actor->mutex_state) != 0) {
        syserr("mutex lock");
    }
    switch (func_data->message.message_type) {
        case MSG_SPAWN:
            if (pthread_mutex_lock(&thread_pool->mutex) != 0) {
                syserr("mutex lock");
            }
            Actor_data *new_actor = malloc(sizeof(Actor_data));
            new_actor->actor_id = actors_queue->queue_size + 1;
            new_actor->role = (role_t *const) func_data->message.data;
            new_actor->killed = false;
            new_actor->stateptr = NULL;
            if (pthread_mutex_init(&new_actor->mutex_state, NULL) != 0) {
                syserr("mutex init");
            }
            if (pthread_mutex_init(&new_actor->mutex_kill, NULL) != 0) {
                syserr("mutex init");
            }
            alive_actors++;
            queue_push(actors_queue, new_actor);
            message_t mess;
            mess.data = (void *) my_actor->actor_id;
            mess.message_type = MSG_HELLO;
            mess.nbytes = 1;

            if (send_message(new_actor->actor_id, mess) != 0) {
                syserr("message hello");
            }
            if (pthread_mutex_unlock(&thread_pool->mutex) != 0) {
                syserr("mutex unlock");
            }
            break;
        case MSG_GODIE:
            if (pthread_mutex_lock(&my_actor->mutex_kill) != 0) {
                syserr("mutex lock");
            }
            my_actor->killed = true;
            if (pthread_mutex_unlock(&my_actor->mutex_kill) != 0) {
                syserr("mutex unlock");
            }

            if (pthread_mutex_lock(&thread_pool->mutex) != 0) {
                syserr("mutex lock");
            }
            alive_actors--;
            if (pthread_mutex_unlock(&thread_pool->mutex) != 0) {
                syserr("mutex unlock");
            }
            break;
        default:
            if (0 <= func_data->message.message_type &&
                func_data->message.message_type < (message_type_t) my_actor->role->nprompts) {
                my_actor->role->prompts[func_data->message.message_type](&my_actor->stateptr, func_data->message.nbytes,
                                                                         func_data->message.data);
                free(func_data);
            }
            break;
    }
    if (pthread_mutex_unlock(&my_actor->mutex_state) != 0) {
        syserr("mutex unlock");
    }

}

void* thread_work(void* data) {
    while (true) {

        if (pthread_mutex_lock(&thread_pool->mutex) != 0) {
            syserr("mutex lock");
        }

        while (thread_pool->queue->is_empty && thread_pool->enabled) {

            if (pthread_cond_wait(&thread_pool->is_work, &thread_pool->mutex) != 0) {
                syserr("cond wait");
            }

        }
        if (!thread_pool->enabled) {
            break;
        }

        Func_data* func = (Func_data*)queue_get(thread_pool->queue);
        thread_pool->working_threads++;
        Actor_data* my_actor = NULL;
        if (func != NULL) {
            my_actor = find(func->actor_id);
        }

        if (pthread_mutex_unlock(&thread_pool->mutex) != 0) {
            syserr("mutex unlock");
        }
        self_actor = func->actor_id;
        execute(func, my_actor);
        if (pthread_mutex_lock(&thread_pool->mutex) != 0) {
            syserr("mutex lock");
        }
        thread_pool->working_threads--;
        if (alive_actors == 0) {
            thread_pool->enabled = false;
            if (pthread_cond_broadcast(&thread_pool->is_work) != 0) {
                syserr("cond broadcast");
            }
            break;
        }
        if (pthread_mutex_unlock(&thread_pool->mutex) != 0) {
            syserr("mutex unlock");
        }
    }
    thread_pool->threads--;
    if (thread_pool->threads == 0) {
        thread_pool_destroy();
        queue_clear(actors_queue, actor_delete);
        queue_destroy(actors_queue);
    }
    else if (pthread_mutex_unlock(&thread_pool->mutex) != 0) {
        syserr("mutex unlock");
    }
    return NULL;

}

void thread_pool_create() {
    alive_actors = 0;
    actors_queue = queue_create();
    thread_pool = malloc(sizeof(Thread_pool));
    if (thread_pool == NULL) {
        syserr("thread pool alloc");
    }
    if (pthread_mutex_init(&thread_pool->mutex, NULL) != 0) {
        syserr("mutex init");
    }
    if (pthread_cond_init(&thread_pool->is_work, NULL) != 0) {
        syserr("cond init");
    }
    thread_pool->working_threads = 0;
    thread_pool->threads = POOL_SIZE;
    thread_pool->queue = queue_create();
    thread_pool->enabled = true;
    for (int i = 0; i < POOL_SIZE; i++) {

        if (pthread_create(&thread_pool->threads_arr[i], NULL, thread_work, NULL) != 0) {
            syserr("thread create");
        }
    }
}

//void thread_pool_destroy(Thread_pool*);

//void thread_pool_add(Thread_pool*, void*);

int actor_system_create(actor_id_t *actor, role_t *const role) {
    thread_pool_create();
    if (pthread_mutex_lock(&thread_pool->mutex) != 0) {
        syserr("mutex lock");
    }
    Actor_data* my_actor = malloc(sizeof(Actor_data));
    if (my_actor == NULL) {
        syserr("actor malloc");
    }
    if (pthread_mutex_init(&my_actor->mutex_kill, NULL) != 0) {
        syserr("mutex init");
    }
    if (pthread_mutex_init(&my_actor->mutex_state, NULL) != 0) {
        syserr("mutex init");
    }
    my_actor->stateptr = NULL;
    my_actor->role = role;
    my_actor->killed = false;
    my_actor->actor_id = *actor;
    queue_push(actors_queue, my_actor);
    if (pthread_mutex_unlock(&thread_pool->mutex) != 0) {
        syserr("mutex unlock");
    }
    message_t mess;
    mess.data = NULL;
    mess.nbytes = 0;
    mess.message_type = MSG_HELLO;
    send_message(*actor, mess);
    return 0;
}

void actor_system_join(actor_id_t actor) {
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pthread_join(thread_pool->threads_arr[i], NULL) != 0) {
            syserr("thread join");
        }
    }
}

int send_message(actor_id_t actor, message_t message) {
    size_t siz = actors_queue->elements;
    Actor_data* my_actor = NULL;
    for (size_t i = 0; i < siz; i++) {
        if (((Actor_data*)queue_get_kth(actors_queue, i))->actor_id == actor) {
            my_actor = (Actor_data*)queue_get_kth(actors_queue, i);
            break;

        }
    }
    if (my_actor == NULL) {
        return -1;
    }
    if (pthread_mutex_lock(&my_actor->mutex_kill) != 0) {
        syserr("mutex lock");
    }
    int res = my_actor->killed ? -2 : 0;
    if (pthread_mutex_unlock(&my_actor->mutex_kill) != 0) {
        syserr("mutex unlock");
    }
    if (res == 0) {
        Func_data* func = malloc(sizeof(Func_data));
        func->actor_id = actor;
        func->message = message;
        queue_push(thread_pool->queue, func);
    }

    if (pthread_cond_signal(&thread_pool->is_work) != 0) {
        syserr("cond signal");
    }
    return res;
}

actor_id_t actor_id_self() {
    return self_actor;
}