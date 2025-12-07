#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

struct Payload {
    int value;
};

struct Monitor {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int ready;             
    struct Payload payload; 
};

void* producer(void* arg) {
    struct Monitor* m = (struct Monitor*)arg;
    int counter = 0;

    for (;;) {
        sleep(1); 

        pthread_mutex_lock(&m->mutex);

        if (m->ready) {
            pthread_mutex_unlock(&m->mutex);
            continue;
        }

        m->payload.value = counter++; 
        m->ready = 1;

        printf("producer: sent event, value=%d, ptr=%p\n",
               m->payload.value, (void*)&m->payload);

        pthread_cond_signal(&m->cond);
        pthread_mutex_unlock(&m->mutex);
    }

    return NULL;
}

void* consumer(void* arg) {
    struct Monitor* m = (struct Monitor*)arg;

    for (;;) {
        pthread_mutex_lock(&m->mutex);

        while (!m->ready) {
            pthread_cond_wait(&m->cond, &m->mutex);
        }

        struct Payload* p = &m->payload;
        printf("consumer: got event, value=%d, ptr=%p\n",
               p->value, (void*)p);

        m->ready = 0; 

        pthread_mutex_unlock(&m->mutex);
    }

    return NULL;
}

int main(void) {
    struct Monitor monitor = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond  = PTHREAD_COND_INITIALIZER,
        .ready = 0
    };

    pthread_t prod_thread;
    pthread_t cons_thread;

    pthread_create(&prod_thread, NULL, producer, &monitor);
    pthread_create(&cons_thread, NULL, consumer, &monitor);

    pthread_join(prod_thread, NULL);
    pthread_join(cons_thread, NULL);

    return 0;
}
