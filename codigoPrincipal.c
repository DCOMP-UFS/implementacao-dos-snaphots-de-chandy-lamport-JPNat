#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mpi.h>

#define BUFFER_SIZE 3

typedef struct
{
    int times[BUFFER_SIZE];
} Clock;

typedef struct
{
    int marcador; // representa 1 se for apenas um marcador e 0 se for um relógio
    int adress; // representa o canal
    Clock clock;
} Message;

typedef struct
{
    int from;
    int messages[BUFFER_SIZE];
} PathState;

Clock *processClock;

Message messageReceiveQueue[BUFFER_SIZE]; // Primeira fila para chegar na thread relogio
Message messageSendQueue[BUFFER_SIZE];    // Fila para sair do relogio

int messageCountReceive = 0;
int messageCountSend = 0;
int my_rank;

pthread_mutex_t receive_mutex, send_mutex;

pthread_cond_t cond_receive_full, cond_receive_empty;
pthread_cond_t cond_send_full, cond_send_empty;

pthread_cond_t updatedSource, updatedDestination;

// Variáveis para o snapshot
int size;
int snapshotState = 0;
int countSnapshot = 0;

//
void printClock
(
    char name,
    int who,
    Clock *showClock
);
// função responsável por mostrar o relógio

Message getMessage
(
    Message *queue,
    int *queueCount
);
// função responsável por pegar os valores do relógio na primeira
//  posição e enfileirar a fila, apagando a primeira posição

void submitMessage
(
    Message message,
    int *count,
    Message *queue
);
// Funcao responsável por colocar um relógio em uma das filas

void receiveMPI
(
    int from
);
// Função responsável por fazer a recepção da mensagem do MPI

void *receiverFuncion
();
// Função que roda a thread receptora

void sendMPI
(
    int to
);
// Função responsável por fazer o envio da mensagem do MPI

void *senderFunction
();
// Função que roda a thread de envio 

void event
();
// função que aumenta o valor do relógio a depender do processo

void updateClock
();
// Função que captura um relógio do buffer de entrada e calcula o novo relógio

void imageClock
();
// Função que é responsável por passar 

int main( int argc, char *argv[])
{
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

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
        printClock('a' ,my_rank, processClock);
        imageClock();
        printClock('b', my_rank, processClock);
        updateClock();
        printClock('c', my_rank, processClock);
        imageClock();
        printClock('d', my_rank, processClock);
        updateClock();
        printClock('e', my_rank, processClock);
        imageClock();
        printClock('f', my_rank, processClock);
        event();
        printClock('g', my_rank, processClock);
        break;
    case 1:
        imageClock();
        printClock('h', my_rank, processClock);
        updateClock();
        printClock('i', my_rank, processClock);
        updateClock();
        printClock('j', my_rank, processClock);
        break;
    case 2:
        event();
        printClock('k', my_rank, processClock);
        imageClock();
        printClock('l', my_rank, processClock);
        updateClock();
        printClock('m', my_rank, processClock);
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

void printClock( char name, int who, Clock *showClock)
{
    printf("%cProcess: %d, Clock: (%d, %d, %d)\n", name, who, showClock->times[0], showClock->times[1], showClock->times[2]);
}

Message getMessage( Message *queue,int *queueCount)
{
    Message message = queue[0];
    int i;
    for (i = 0; i < *queueCount - 1; i++)
    {
        queue[i] = queue[i + 1];
    }

    (*queueCount)--;

    return message;
}

void submitMessage( Message message, int *count, Message *queue)
{
    queue[*count] = message;
    (*count)++;
}

void receiveMPI(int from)
{

    int message[5];
    MPI_Recv(message, 5, MPI_INT, from, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    pthread_mutex_lock(&receive_mutex);

    if (messageCountReceive == BUFFER_SIZE)
    {
        pthread_cond_wait(&cond_receive_empty, &send_mutex);
    }

    Clock newClock = {{0, 0, 0}};

    for (int i = 0; i < 3; i++)
    {
        newClock.times[i] = message[i];
    }

    Message recieved = {
        message[4],
        message[5],
        newClock
    };

    submitMessage(recieved, &messageCountReceive, messageReceiveQueue);

    pthread_mutex_unlock(&receive_mutex);
    pthread_cond_signal(&cond_receive_full);
}

void *receiverFuncion() // Modificar para tratar o algoritmo de snapshot
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

    if (messageCountSend == 0)
    {
        pthread_cond_wait(&cond_send_full, &send_mutex);
    }

    Message newMessage = getMessage(messageSendQueue, &messageCountSend);
    pthread_mutex_unlock(&send_mutex);
    pthread_cond_signal(&cond_send_empty);

    int sending[5] = {
        newMessage.clock.times[0],  
        newMessage.clock.times[1],  
        newMessage.clock.times[2],
        newMessage.marcador,
        newMessage.adress
        };

    MPI_Send(sending, 5, MPI_INT, to, 0, MPI_COMM_WORLD);

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
    pthread_mutex_lock(&receive_mutex);
    if (messageCountReceive == 0)
    {
        pthread_cond_wait(&cond_receive_full, &receive_mutex);
    }

    Message newMessage = getMessage(messageReceiveQueue, &messageCountReceive);

    pthread_mutex_unlock(&receive_mutex);
    pthread_cond_signal(&cond_receive_empty);

    for (int i = 0; i < 3; i++)
    {
        processClock->times[i] = processClock->times[i] < newMessage.clock.times[i] ? newMessage.clock.times[i] : processClock->times[i];
    }
}

void imageClock()
{
    processClock->times[my_rank]++;

    Message newMessage = {
        countSnapshot,
        my_rank,
        *processClock
    };

    pthread_mutex_lock(&send_mutex);
    if (messageCountSend == BUFFER_SIZE)
    {
        pthread_cond_wait(&cond_send_empty, &send_mutex);
    }

    submitMessage(newMessage, &messageCountSend, messageSendQueue);

    pthread_mutex_unlock(&send_mutex);
    pthread_cond_signal(&cond_send_full);
}