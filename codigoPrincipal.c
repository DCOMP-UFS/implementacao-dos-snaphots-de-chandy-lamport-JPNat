#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <mpi.h>

#define BUFFER_SIZE 3

// Estrutura para representar o relógio lógico
typedef struct {
    int times[BUFFER_SIZE];
} Clock;

// Variáveis globais
typedef struct {
    Clock clockQueueEntry[BUFFER_SIZE]; // Fila de entrada do relógio
    Clock clockQueueExit[BUFFER_SIZE];  // Fila de saída do relógio
    int clockCountEntry;
    int clockCountExit;
    Clock processClock;
    int recording; // Flag para marcar se estamos registrando o snapshot
} ProcessData;

ProcessData processData = { .clockCountEntry = 0, .clockCountExit = 0, .processClock = {{0, 0, 0}}, .recording = 0 };

int my_rank;
pthread_mutex_t mutexEntry, mutexExit;
pthread_cond_t cond_receive, cond_send, cond_process, condEntry, condExit;

// Funções auxiliares para manipulação do relógio
void printClock(int who, Clock showClock);
Clock getClock(Clock *queue, int *queueCount);
void submitClock(Clock clock, int *count, Clock *queue);

// Funções das threads
void* receiveClock(void *whoSent);
void* sendClock(void *clock);
void* updateClock(void *action);
void event();

// Funções dos processos
void process0();
void process1();
void process2();
void snapshot();

// Função de marcação (indica que o processo iniciou a captura do estado)
void* marker(void *arg);

int main(int argc, char *argv[]) {
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    switch (my_rank) {
        case 0: process0(); break;
        case 1: process1(); break;
        case 2: process2(); break;
    }

    for (int i = 0; i < 3; i++) {
        if (my_rank == i) {
            printClock(my_rank, processData.processClock);
        }
        sleep(3);
    }

    MPI_Finalize();
    return 0;
}

void printClock(int who, Clock showClock) {
    printf("Process: %d, Clock: (%d, %d, %d)\n", who, showClock.times[0], showClock.times[1], showClock.times[2]);
}

Clock getClock(Clock *queue, int *queueCount) {
    Clock clock = queue[0];
    for (int i = 0; i < *queueCount - 1; i++) {
        queue[i] = queue[i + 1];
    }
    (*queueCount)--; 
    return clock;
}

void submitClock(Clock clock, int *count, Clock *queue) {
    queue[*count] = clock;
    (*count)++;
}

void* receiveClock(void *whoSent) {
    int source = (int)(intptr_t)whoSent;
    int received[BUFFER_SIZE];

    MPI_Recv(received, BUFFER_SIZE, MPI_INT, source, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    pthread_mutex_lock(&mutexEntry);
    while (processData.clockCountEntry == BUFFER_SIZE) {
        pthread_cond_wait(&condEntry, &mutexEntry);
    }

    Clock newClock = {{received[0], received[1], received[2]}};
    submitClock(newClock, &processData.clockCountEntry, processData.clockQueueEntry);
    pthread_mutex_unlock(&mutexEntry);
    pthread_cond_signal(&cond_process);

    return NULL;
}

void* sendClock(void *toWho) {
    int destination = (int)(intptr_t)toWho;
    pthread_mutex_lock(&mutexExit);
    while (processData.clockCountExit == 0) {
        pthread_cond_wait(&condExit, &mutexExit);
    }

    Clock newClock = getClock(processData.clockQueueExit, &processData.clockCountExit);
    pthread_mutex_unlock(&mutexExit);
    pthread_cond_signal(&cond_process);

    MPI_Send(newClock.times, BUFFER_SIZE, MPI_INT, destination, 0, MPI_COMM_WORLD);
    return NULL;
}

void* updateClock(void* arg) {
    int action = (int)(intptr_t)arg;
    pthread_mutex_lock(&mutexEntry);

    switch (action) {
        case 1:
            event();
            break;
        case 2:
            if (processData.clockCountEntry > 0) {
                Clock newClock = getClock(processData.clockQueueEntry, &processData.clockCountEntry);
                for (int i = 0; i < BUFFER_SIZE; i++) {
                    processData.processClock.times[i] = processData.processClock.times[i] > newClock.times[i] ? processData.processClock.times[i] : newClock.times[i];
                }
            }
            break;
        case 3:
            if (processData.clockCountExit < BUFFER_SIZE) {
                submitClock(processData.processClock, &processData.clockCountExit, processData.clockQueueExit);
            }
            break;
    }

    pthread_mutex_unlock(&mutexEntry);
    return NULL;
}

void event() {
    processData.processClock.times[my_rank]++;
}

// Função para capturar o snapshot de Chandy-Lamport
void snapshot() {
    if (!processData.recording) {
        processData.recording = 1;

        // Registrar o estado local
        printf("Process %d: Capturando o estado local\n", my_rank);
        printClock(my_rank, processData.processClock);

        // Enviar uma mensagem de marcação para os outros processos
        for (int i = 0; i < 3; i++) {
            if (i != my_rank) {
                pthread_t markerThread;
                pthread_create(&markerThread, NULL, marker, (void*)(intptr_t)i);
                pthread_join(markerThread, NULL);
            }
        }

        // Registrar os canais (mensagens em trânsito) após a marcação
        printf("Process %d: Registrando os canais\n", my_rank);
    }
}

// Função de marcação (inicia o registro de mensagens)
void* marker(void *arg) {
    int dest = (int)(intptr_t)arg;

    pthread_mutex_lock(&mutexEntry);
    while (processData.clockCountExit > 0) {
        // Registra as mensagens enviadas, não as recebidas após o snapshot
        Clock markedClock = getClock(processData.clockQueueExit, &processData.clockCountExit);
        printf("Process %d: Registrando mensagem para %d\n", my_rank, dest);
        submitClock(markedClock, &processData.clockCountEntry, processData.clockQueueEntry);
    }
    pthread_mutex_unlock(&mutexEntry);

    return NULL;
}

// Função para process0 iniciar o snapshot
void process0() {
    pthread_t receiver, body, deliver;
    pthread_mutex_init(&mutexEntry, NULL);
    pthread_mutex_init(&mutexExit, NULL);
    pthread_cond_init(&cond_receive, NULL);
    pthread_cond_init(&cond_send, NULL);
    pthread_cond_init(&cond_process, NULL);
    pthread_cond_init(&condEntry, NULL);
    pthread_cond_init(&condExit, NULL);

    snapshot();  // Iniciar o snapshot

    pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 1);
    pthread_create(&deliver, NULL, sendClock, (void*)(intptr_t) 1);
    pthread_create(&receiver, NULL, receiveClock, (void*)(intptr_t) 1);

    pthread_join(receiver, NULL);
    pthread_join(body, NULL);
    pthread_join(deliver, NULL);

    pthread_mutex_destroy(&mutexEntry);
    pthread_mutex_destroy(&mutexExit);
    pthread_cond_destroy(&cond_receive);
    pthread_cond_destroy(&cond_process);
    pthread_cond_destroy(&cond_send);
    pthread_cond_destroy(&condEntry);
    pthread_cond_destroy(&condExit);
}

void process1() {
    pthread_t receiver, body, deliver;
    pthread_mutex_init(&mutexEntry, NULL);
    pthread_mutex_init(&mutexExit, NULL);
    pthread_cond_init(&cond_receive, NULL);
    pthread_cond_init(&cond_send, NULL);
    pthread_cond_init(&cond_process, NULL);
    pthread_cond_init(&condEntry, NULL);
    pthread_cond_init(&condExit, NULL);

    snapshot();  // Iniciar o snapshot

    pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 3);
    pthread_create(&deliver, NULL, sendClock, (void*)(intptr_t) 0);
    pthread_create(&receiver, NULL, receiveClock, (void*)(intptr_t) 0);

    pthread_join(receiver, NULL);
    pthread_join(body, NULL);
    pthread_join(deliver, NULL);

    pthread_mutex_destroy(&mutexEntry);
    pthread_mutex_destroy(&mutexExit);
    pthread_cond_destroy(&cond_receive);
    pthread_cond_destroy(&cond_process);
    pthread_cond_destroy(&cond_send);
    pthread_cond_destroy(&condEntry);
    pthread_cond_destroy(&condExit);
}

void process2() {
    pthread_t receiver, body, deliver;
    pthread_mutex_init(&mutexEntry, NULL);
    pthread_mutex_init(&mutexExit, NULL);
    pthread_cond_init(&cond_receive, NULL);
    pthread_cond_init(&cond_send, NULL);
    pthread_cond_init(&cond_process, NULL);
    pthread_cond_init(&condEntry, NULL);
    pthread_cond_init(&condExit, NULL);

    snapshot();

    pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 1);
    pthread_create(&deliver, NULL, sendClock, (void*)(intptr_t) 0);
    pthread_create(&receiver, NULL, receiveClock, (void*)(intptr_t) 0);

    pthread_join(receiver, NULL);
    pthread_join(body, NULL);
    pthread_join(deliver, NULL);

    pthread_mutex_destroy(&mutexEntry);
    pthread_mutex_destroy(&mutexExit);
    pthread_cond_destroy(&cond_receive);
    pthread_cond_destroy(&cond_process);
    pthread_cond_destroy(&cond_send);
    pthread_cond_destroy(&condEntry);
    pthread_cond_destroy(&condExit);
}