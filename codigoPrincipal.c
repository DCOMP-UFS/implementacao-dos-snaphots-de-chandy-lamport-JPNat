#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>
#include <mpi.h>

#define BUFFER_SIZE 3

typedef struct{
   int times[BUFFER_SIZE];
}Clock;

Clock processClock = {{0, 0, 0}};

Clock clockQueueEntry[BUFFER_SIZE]; // Primeira fila para chegar na thread relogio
Clock clockQueueExit[BUFFER_SIZE];  // Fila para sair do relogio

int clockCountEntry = 0;
int clockCountExit = 0;
int my_rank;

pthread_mutex_t mutex;

pthread_cond_t cond_receive, cond_send, cond_process;

void printClock
(
   int who,
   Clock showClock
);
// função responsável por mostrar o relógio

Clock getClock
(
   Clock *queue,
   int *queueCount
);
// função responsável por pegar os valores do relógio na primeira
//  posição e enfileirar a fila, apagando a primeira posição

void submitClock
(
   Clock clock,
   int *count,
   Clock *queue
);
// Funcao responsável por colocar um relógio em uma das filas

void* receiveClock
(
   void *whoSent
);
// função da thread que recebe relógio de outros processos

void* sendClock
(
   void *clock
);
// função da thread que envia relógio de outros processos

void* updateClock
(
   void *clock
);
// função responsável pelo controle da thread central
// a ideia é que seja uma lista com comando e processo alvo

void event
();
// função que aumenta o valor do relógio a depender do processo

void process0();

void process1();

void process2();

int main
(
   int argc,
   char *argv[]
){
   MPI_Init(NULL, NULL);
   MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

   switch (my_rank)
   {
   case 0:
      process0();
      break;
   case 1:
      process1();
      break;
   case 2:
      process2();
      break;
   default:
      break;
   }

   for (int i = 0; i < 3; i++)
   {
      if (my_rank == i){
         printClock(my_rank, processClock);
      }
      wait(3);
   }

   MPI_Finalize();
   return 0;
} /* main */

void printClock
(
   int who,
   Clock showClock
){
   printf("Process: %d, Clock: (%d, %d, %d)\n", who, showClock.times[0], showClock.times[1], showClock.times[2]);
}

Clock getClock
(
   Clock *queue,
   int *queueCount
){
   Clock clock = queue[0];
   int i;
   for (i = 0; i < *queueCount - 1; i++)
   {
      queue[i] = queue[i + 1];
   }

   (*queueCount)--;

   return clock;
}

void submitClock
(
   Clock clock,
   int *count,
   Clock *queue
){
   queue[*count] = clock;
   (*count)++;
}

void* receiveClock
(
   void *whoSent
){
   int source = (int)(intptr_t)whoSent;
   int received[BUFFER_SIZE];

   MPI_Recv(received, BUFFER_SIZE, MPI_INT, source, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE );

   pthread_mutex_lock(&mutex);
   if (clockCountEntry == BUFFER_SIZE)
   {
      pthread_cond_wait(&cond_receive, &mutex);
   }

   Clock newClock = {{received[0], received[1], received[2]}};

   printf("%d received \nFrom:", my_rank);
   printClock(source, newClock);

   submitClock(newClock, &clockCountEntry, clockQueueEntry);
   pthread_mutex_unlock(&mutex);
   pthread_cond_signal(&cond_process);
   return NULL;
}

void* sendClock(
   void *toWho
){
   int destination = (int)(intptr_t)toWho;
   pthread_mutex_lock(&mutex);

   if (clockCountExit == 0)
   {
      pthread_cond_wait(&cond_send, &mutex);
   }

   Clock newClock = getClock(clockQueueExit, &clockCountExit);

   printf("sending from %d\nTo ", my_rank);
   printClock(destination, newClock);

   pthread_mutex_unlock(&mutex);
   pthread_cond_signal(&cond_process);

   MPI_Send(newClock.times, BUFFER_SIZE, MPI_INT, destination, 0, MPI_COMM_WORLD);
   return NULL;
}

void* updateClock
(
   void* arg
){
   pthread_mutex_lock(&mutex);
   int action = (int)(intptr_t)arg;

   switch (action)
   {
   case 1:// evento
      event();
      printf("Event on ");
      printClock(my_rank, processClock);
      break;
   case 2:// pega relógio da fila de entrada
      processClock.times[my_rank]++;
      if (clockCountEntry == 0){
         pthread_cond_wait(&cond_process, &mutex);
      }
      Clock newClok = getClock(clockQueueEntry, &clockCountEntry);
      for (int i = 0; i < BUFFER_SIZE; i++) {
         processClock.times[i] = processClock.times[i] > newClok.times[i] ? processClock.times[i] : newClok.times[i];
      }
      printf("Update on ");
      printClock(my_rank, processClock);
      break;
   case BUFFER_SIZE:// envia relógio pra fila de envio
      processClock.times[my_rank]++;
      if (clockCountExit == BUFFER_SIZE){
         pthread_cond_wait(&cond_process, &mutex);
      }
      submitClock (processClock, &clockCountExit, clockQueueExit);
      break;
   default:
      break;
   }

   pthread_mutex_unlock(&mutex);
   if(action == 2)
   {
      pthread_cond_signal(&cond_receive);
   }
   if(action == BUFFER_SIZE)
   {
      pthread_cond_signal(&cond_send);
   }
   return NULL;
}

void event
(){
   processClock.times[my_rank]++;
}

void process0
(){
   pthread_t receiver;
   pthread_t body;// responsável pelo corpo do relógio
   pthread_t deliver;
   
   pthread_mutex_init(&mutex, NULL);

   pthread_cond_init(&cond_receive, NULL);
   pthread_cond_init(&cond_send, NULL);
   pthread_cond_init(&cond_process, NULL);
   // Programe cada processo dentro do bloco
   // utilizando as funções clockSend clockReceive e clockUpdate
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 1);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 3);
   pthread_create(&deliver, NULL, sendClock, (void*)(intptr_t) 1);
   pthread_create(&receiver, NULL, receiveClock, (void*)(intptr_t) 1);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 2);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 3);
   pthread_create(&deliver, NULL, sendClock, (void*)(intptr_t) 2);
   pthread_create(&receiver, NULL, receiveClock, (void*)(intptr_t) 2);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 2);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 3);
   pthread_create(&deliver, NULL, sendClock, (void*)(intptr_t) 1);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 1);
   // fim da area onde se deve programar
   pthread_join(receiver, NULL);
   pthread_join(body, NULL);
   pthread_join(deliver, NULL);

   pthread_mutex_destroy(&mutex);
   pthread_cond_destroy(&cond_receive);
   pthread_cond_destroy(&cond_process);
   pthread_cond_destroy(&cond_send);
}

void process1
(){
   pthread_t receiver;
   pthread_t body;// responsável pelo corpo do relógio
   pthread_t deliver;
   
   pthread_mutex_init(&mutex, NULL);

   pthread_cond_init(&cond_receive, NULL);
   pthread_cond_init(&cond_send, NULL);
   pthread_cond_init(&cond_process, NULL);
   // Programe cada processo dentro do bloco
   // utilizando as funções clockSend clockReceive e clockUpdate
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 3);
   pthread_create(&deliver, NULL, sendClock, (void*)(intptr_t) 0);
   pthread_create(&receiver, NULL, receiveClock, (void*)(intptr_t) 0);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 2);
   pthread_create(&receiver, NULL, receiveClock, (void*)(intptr_t) 0);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 2);
   // fim da area onde se deve programar
   pthread_join(receiver, NULL);
   pthread_join(body, NULL);
   pthread_join(deliver, NULL);

   pthread_mutex_destroy(&mutex);
   pthread_cond_destroy(&cond_receive);
   pthread_cond_destroy(&cond_process);
   pthread_cond_destroy(&cond_send);
}

void process2
(){
   pthread_t receiver;
   pthread_t body;// responsável pelo corpo do relógio
   pthread_t deliver;
   
   pthread_mutex_init(&mutex, NULL);

   pthread_cond_init(&cond_receive, NULL);
   pthread_cond_init(&cond_send, NULL);
   pthread_cond_init(&cond_process, NULL);
   // Programe cada processo dentro do bloco
   // utilizando as funções clockSend clockReceive e clockUpdate
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 1);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 3);
   pthread_create(&deliver, NULL, sendClock, (void*)(intptr_t) 0);
   pthread_create(&receiver, NULL, receiveClock, (void*)(intptr_t) 0);
   pthread_create(&body, NULL, updateClock, (void*)(intptr_t) 2);
   // fim da area onde se deve programar
   pthread_join(receiver, NULL);
   pthread_join(body, NULL);
   pthread_join(deliver, NULL);

   pthread_mutex_destroy(&mutex);
   pthread_cond_destroy(&cond_receive);
   pthread_cond_destroy(&cond_process);
   pthread_cond_destroy(&cond_send);
}