#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <mpi.h>

#define BUFFER_SIZE 3

typedef struct
{
    int times[BUFFER_SIZE];
} Clock;

Clock *processClock;

Clock clockReceiveQueue[BUFFER_SIZE]; // Primeira fila para chegar na thread relogio
Clock clockSendQueue[BUFFER_SIZE];    // Fila para sair do relogio

int clockCountReceive = 0;
int clockCountSend = 0;
int my_rank;

int source, destination;

pthread_mutex_t receive_mutex, send_mutex;

pthread_cond_t cond_receive_full, cond_receive_empty;
pthread_cond_t cond_send_full, cond_send_empty;

pthread_cond_t updatedSource, updatedDestination;

void printClock(
    int who,
    Clock *showClock);
// função responsável por mostrar o relógio

Clock getClock(
    Clock *queue,
    int *queueCount);
// função responsável por pegar os valores do relógio na primeira
//  posição e enfileirar a fila, apagando a primeira posição

void submitClock(
    Clock clock,
    int *count,
    Clock *queue);
// Funcao responsável por colocar um relógio em uma das filas

void receiveMPI(
    int from);

void *receiverFuncion();

void sendMPI(
    int to);

void *senderFunction();

void event();
// função que aumenta o valor do relógio a depender do processo

void updateClock();

void imageClock();

int main(
    int argc,
    char *argv[])
{
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    pthread_t receiver;
    pthread_t sender;

    pthread_mutex_init(&receive_mutex, NULL);
    pthread_mutex_init(&send_mutex, NULL);

    pthread_cond_init(&cond_receive_full, NULL);
    pthread_cond_init(&cond_receive_empty, NULL);
    pthread_cond_init(&cond_send_empty, NULL);
    pthread_cond_init(&cond_send_full, NULL);

    pthread_create(&receiver, NULL, &receiverFuncion, NULL);
    pthread_create(&sender, NULL, &senderFunction, NULL);

    processClock = malloc(sizeof(Clock));
    for (int i = 0; i < 3; i++)
    {
        processClock->times[i] = 0;
    }

    switch (my_rank)
    {
    case 0:
        event();
        printf("a");
        printClock(my_rank, processClock);
        imageClock();
        printf("b");
        printClock(my_rank, processClock);
        updateClock();
        printf("c");
        printClock(my_rank, processClock);
        imageClock();
        printf("d");
        printClock(my_rank, processClock);
        updateClock();
        printf("e");
        printClock(my_rank, processClock);
        imageClock();
        printf("f");
        printClock(my_rank, processClock);
        event();
        printf("g");
        printClock(my_rank, processClock);
        break;
    case 1:
        imageClock();
        printf("h");
        printClock(my_rank, processClock);
        updateClock();
        printf("i");
        printClock(my_rank, processClock);
        updateClock();
        printf("j");
        printClock(my_rank, processClock);
        break;
    case 2:
        event();
        printf("k");
        printClock(my_rank, processClock);
        imageClock();
        printf("l");
        printClock(my_rank, processClock);
        updateClock();
        printf("m");
        printClock(my_rank, processClock);
        break;
    default:
        break;
    }

    pthread_join(receiver, NULL);
    pthread_join(sender, NULL);

    pthread_mutex_destroy(&send_mutex);
    pthread_mutex_destroy(&receive_mutex);

    pthread_cond_destroy(&cond_receive_empty);
    pthread_cond_destroy(&cond_receive_full);
    pthread_cond_destroy(&cond_send_empty);
    pthread_cond_destroy(&cond_send_full);
    MPI_Finalize();

    free(processClock);
    return 0;
} /* main */

void printClock(
    int who,
    Clock *showClock)
{
    printf("Process: %d, Clock: (%d, %d, %d)\n", who, showClock->times[0], showClock->times[1], showClock->times[2]);
}

Clock getClock(
    Clock *queue,
    int *queueCount)
{
    Clock clock = queue[0];
    int i;
    for (i = 0; i < *queueCount - 1; i++)
    {
        queue[i] = queue[i + 1];
    }

    (*queueCount)--;

    return clock;
}

void submitClock(
    Clock clock,
    int *count,
    Clock *queue)
{
    queue[*count] = clock;
    (*count)++;
}

void receiveMPI(int from)
{

    int message[3];
    MPI_Recv(message, 3, MPI_INT, from, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    pthread_mutex_lock(&receive_mutex);

    if (clockCountReceive == BUFFER_SIZE)
    {
        pthread_cond_wait(&cond_receive_empty, &send_mutex);
    }

    Clock newClock = {{0, 0, 0}};

    for (int i = 0; i < 3; i++)
    {
        newClock.times[i] = message[i];
    }

    submitClock(newClock, &clockCountReceive, clockReceiveQueue);

    pthread_mutex_unlock(&receive_mutex);
    pthread_cond_signal(&cond_receive_full);
}

void *receiverFuncion()
{
    switch (my_rank)
    {
    case 0:
        receiveMPI(1);
        receiveMPI(2);
        break;
    case 1:
        receiveMPI(0);
        receiveMPI(0);
        break;
    case 2:
        receiveMPI(0);
        break;
    default:
        break;
    }
    return NULL;
}

void sendMPI(int to)
{

    pthread_mutex_lock(&send_mutex);

    if (clockCountSend == 0)
    {
        pthread_cond_wait(&cond_send_full, &send_mutex);
    }

    Clock newClock = getClock(clockSendQueue, &clockCountSend);
    pthread_mutex_unlock(&send_mutex);
    pthread_cond_signal(&cond_send_empty);

    MPI_Send(newClock.times, 3, MPI_INT, to, 0, MPI_COMM_WORLD);
}

void *senderFunction()
{
    switch (my_rank)
    {
    case 0:
        sendMPI(1);
        sendMPI(2);
        sendMPI(1);
        break;
    case 1:
        sendMPI(0);
        break;
    case 2:
        sendMPI(0);
        break;
    default:
        break;
    }
    return NULL;
}

void event()
{
    processClock->times[my_rank]++;
}

void updateClock()
{
    processClock->times[my_rank]++;

    pthread_mutex_lock(&receive_mutex);
    if (clockCountReceive == 0)
    {
        pthread_cond_wait(&cond_receive_full, &receive_mutex);
    }

    Clock newClock = getClock(clockReceiveQueue, &clockCountReceive);

    pthread_mutex_unlock(&receive_mutex);
    pthread_cond_signal(&cond_receive_empty);

    for (int i = 0; i < 3; i++)
    {
        processClock->times[i] = processClock->times[i] < newClock.times[i] ? newClock.times[i] : processClock->times[i];
    }
}

void imageClock()
{
    processClock->times[my_rank]++;

    Clock newClock = *processClock;

    pthread_mutex_lock(&send_mutex);
    if (clockCountSend == BUFFER_SIZE)
    {
        pthread_cond_wait(&cond_send_empty, &send_mutex);
    }

    submitClock(newClock, &clockCountSend, clockSendQueue);

    pthread_mutex_unlock(&send_mutex);
    pthread_cond_signal(&cond_send_full);
}