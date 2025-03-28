 #include <stdio.h>
 #include <pthread.h>
 #include <mpi.h>
 
 #define NUM_OF_PROCESS 3 
 #define BUFFER_SIZE 5 
 
 // Struct do relógio de cada processo
 typedef struct Clock { 
     int p[NUM_OF_PROCESS];
 } Clock;
 
 // Struct das mensagens entre processos
 typedef struct Message {
     Clock clock;        // Relógio associado à mensagem
     int destination;
     int isMarker;      // Flag que indica se a mensagem é um marcador (para snapshot)
 } Message;
 
 // Estruturas para armazenar mensagens em "trânsito" durante o snapshot
 typedef struct InTransitMessage {
     int sender;
     Message message;
 } InTransitMessage;
 
 // Buffers para mensagens serem recebidas e enviadas
 Message BUFFERin[BUFFER_SIZE];
 int bufferCountIn = 0;
 
 Message BUFFERout[BUFFER_SIZE];
 int bufferCountOut = 0;
 
 // Buffers para mensagens em trânsito 
 InTransitMessage messagesInTransit[NUM_OF_PROCESS][BUFFER_SIZE];
 int inTransitCount[NUM_OF_PROCESS] = {0};
 
 // Variáveis para controle do snapshot
 int realizandoSnapshot = 0;                     // Indica se o snapshot está em andamento
 int receivedMarkerControll[NUM_OF_PROCESS] = {0}; // Indica se o marcador foi recebido em cada "canal"
 Clock snapshotClock;                            // Armazena o estado registrado durante o snapshot
 Clock processClockGlobal;                     // Relógio lógico global do processo
 
 // Mutexes e condições
 pthread_mutex_t mutexIn;
 pthread_cond_t condFullIn;
 pthread_cond_t condEmptyIn;
 
 pthread_mutex_t mutexOut;
 pthread_cond_t condFullOut;
 pthread_cond_t condEmptyOut;
 
 pthread_mutex_t clockMutex;
 
 pthread_mutex_t snapshotMutex;
 pthread_cond_t snapshotCond;
 int snapshotComplete = 0;
 
 // Funções de Manipulação dos Buffers
 /* 
  * Função: producerIn
  * Descrição: Consome uma mensagem do buffer de entrada (BUFFERin)
  *            Se o buffer estiver vazio, aguarda até que chegue mensagens
  */
 Message producerIn(int id) {
     pthread_mutex_lock(&mutexIn);
 
     while (bufferCountIn == 0) {
         pthread_cond_wait(&condEmptyIn, &mutexIn);
     }
 
     // Retira a primeira mensagem do buffer
     Message message = BUFFERin[0];
     // Realoca os elementos para manter a ordem
     for (int i = 0; i < bufferCountIn - 1; i++) {
         BUFFERin[i] = BUFFERin[i + 1];
     }
     bufferCountIn--;
 
     pthread_mutex_unlock(&mutexIn);
     pthread_cond_signal(&condFullIn);
     return message;
 }
 
 /* 
  * Função: consumerCLock
  * Descrição: Insere uma mensagem no buffer de entrada (BUFFERin)
  *            Se o buffer estiver cheio, aguarda até que haja espaço
  */
 void consumerCLock(Message message, int id) {
     pthread_mutex_lock(&mutexIn);
     
     while (bufferCountIn == BUFFER_SIZE) {
         pthread_cond_wait(&condFullIn, &mutexIn);
     }
     
     BUFFERin[bufferCountIn++] = message;
     
     pthread_mutex_unlock(&mutexIn);
     pthread_cond_signal(&condEmptyIn);
 }
 
 /* 
  * Função: producerClock
  * Descrição: Consome uma mensagem do buffer de saída (BUFFERout)
  *            Se o buffer estiver vazio, aguarda até que haja mensagens.
  */
 Message producerClock(int id) {
     pthread_mutex_lock(&mutexOut);
     
     while (bufferCountOut == 0) {
         pthread_cond_wait(&condEmptyOut, &mutexOut);
     }
     
     // Retira a primeira mensagem do buffer de saída
     Message message = BUFFERout[0];
     // Realoca os elementos para manter a ordem
     for (int i = 0; i < bufferCountOut - 1; i++) {
         BUFFERout[i] = BUFFERout[i + 1];
     }
     bufferCountOut--;
     
     pthread_mutex_unlock(&mutexOut);
     pthread_cond_signal(&condFullOut);
     return message;
 }
 
 /* 
  * Função: consumerOut
  * Descrição: Insere uma mensagem no buffer de saída (BUFFERout)
  *            Se o buffer estiver cheio, aguarda até que haja espaç
  */
 void consumerOut(Message message, int id) {
     pthread_mutex_lock(&mutexOut);
     
     while (bufferCountOut == BUFFER_SIZE) {
         pthread_cond_wait(&condFullOut, &mutexOut);
     }
     
     BUFFERout[bufferCountOut++] = message;
     
     pthread_mutex_unlock(&mutexOut);
     pthread_cond_signal(&condEmptyOut);
 }
 
 // Funções para Gerenciamento do Relógio Lógico
 
 /* 
  * Função: UpdateClock
  * Descrição: Atualiza o relógio local "old" comparando com o relógio "new" recebido
  *            Para cada posição, mantém o valor máximo
  */
 void UpdateClock(Clock *old, Clock new) {
     for (int i = 0; i < NUM_OF_PROCESS; i++) {
         old->p[i] = (old->p[i] > new.p[i]) ? old->p[i] : new.p[i];
     }
 }
 
 /* 
  * Função: Event
  * Descrição: Simula um evento interno no processo, incrementando seu relógio
  *            Em seguida, imprime o estado do relógio global
  */
 void Event(int pid) {
     int myRank;
     MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
 
     pthread_mutex_lock(&clockMutex);
     processClockGlobal.p[pid]++;
     printf("Processo: %d (EVENTO) :: (%d, %d, %d)\n", myRank,
            processClockGlobal.p[0], processClockGlobal.p[1], processClockGlobal.p[2]);
     pthread_mutex_unlock(&clockMutex);
 }
 
 /* 
  * Função: Send
  * Descrição: Realiza um envio de mensagem para o processo destino
  *            Antes de enviar, registra um evento (atualiza o relógio)
  *            A mensagem enviada contém o relógio atual e a indicação se é marcador
  */
 void Send(int destPid) {
     int myRank;
     MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
 
     // Registra um evento no processo
     Event(myRank);
 
     // Prepara a mensagem a ser enviada
     Message message;
     pthread_mutex_lock(&clockMutex);
     message.clock = processClockGlobal;
     pthread_mutex_unlock(&clockMutex);
     message.destination = destPid;
     message.isMarker = 0;  // Mensagem normal
 
     // Insere a mensagem no buffer de saída
     consumerOut(message, myRank);
     
     printf("Processo %d (ENVIA para %d) :: (%d, %d, %d)\n", myRank, destPid,
            processClockGlobal.p[0], processClockGlobal.p[1], processClockGlobal.p[2]);
 }
 
 /* 
  * Função: Receive
  * Descrição: Consome uma mensagem do buffer de entrada e atualiza o relógio
  *            local de acordo com o relógio contido na mensagem
  */
 void Receive(int sourcePid) {
     Message message;
     int myRank;
     MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
 
     message = producerIn(myRank);
 
     // Atualiza o relógio: mescla o relógio recebido e incrementa o relógio local
     pthread_mutex_lock(&clockMutex);
     UpdateClock(&processClockGlobal, message.clock);
     processClockGlobal.p[myRank]++;
     pthread_mutex_unlock(&clockMutex);
 
     printf("Processo: %d (RECEBE de %d) :: (%d, %d, %d)\n", myRank, sourcePid,
            processClockGlobal.p[0], processClockGlobal.p[1], processClockGlobal.p[2]);
 }
 
 /* 
  * Função: initiateSnapshot
  * Descrição: Inicia o protocolo de snapshot do processo:
  *            - Registra o estado local (relógio)
  *            - Marca o canal próprio como tendo recebido o marcador
  *            - Envia mensagens marcadoras para todos os demais processos
  */
 void initiateSnapshot(int myRank) {
     printf("Processo %d iniciando snapshot\n", myRank);
 
     pthread_mutex_lock(&clockMutex);
     realizandoSnapshot = 1;
     snapshotClock = processClockGlobal;  // Registra o estado atual
     printf("Processo %d registrou seu estado: [%d, %d, %d]\n", myRank,
            snapshotClock.p[0], snapshotClock.p[1], snapshotClock.p[2]);
     pthread_mutex_unlock(&clockMutex);
 
     // Marca que o próprio processo já recebeu o marcador (canal interno)
     receivedMarkerControll[myRank] = 1;
 
     // Envia mensagens marcador para todos os outros processos
     for (int i = 0; i < NUM_OF_PROCESS; i++) {
         if (i != myRank) {
             Message markerMessage;
             // Usamos -1 para indicar o marcador no relógio
             for (int j = 0; j < NUM_OF_PROCESS; j++) {
                 markerMessage.clock.p[j] = -1;
             }
             markerMessage.destination = i;
             markerMessage.isMarker = 1;
             consumerOut(markerMessage, myRank);
         }
     }
 
     // Sinaliza o início do processo de snapshot
     pthread_mutex_lock(&snapshotMutex);
     snapshotComplete = 0;
     pthread_mutex_unlock(&snapshotMutex);
 }
 
 // Threads: Envio e Recepção de Mensagens
 
 /* 
  * Thread: startReceiver
  * Descrição: Loop de recepção de mensagens via MPI
  *            Se a mensagem recebida for um marcador, inicia ou continua o snapshot
  *            se for uma mensagem normal, atualiza o relógio e armazena mensagens em trânsito
  *            caso o snapshot esteja em andamento
  */
 void *startReceiver(void* args) {
     int myRank, p[NUM_OF_PROCESS];
     MPI_Status status;
     MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
 
     while (1) {
         // Recebe uma mensagem via MPI
         MPI_Recv(p, NUM_OF_PROCESS, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
         int sender = status.MPI_SOURCE;
 
         // Verifica se a mensagem é um marcador (identificado por -1)
         if (p[0] == -1) {
             printf("Processo %d recebeu marcador de %d\n", myRank, sender);
             
             pthread_mutex_lock(&clockMutex);
             if (!realizandoSnapshot) {
                 // Se é a primeira vez que recebe marcador, registra o estado
                 realizandoSnapshot = 1;
                 snapshotClock = processClockGlobal;
                 printf("Processo %d registrou seu estado: [%d, %d, %d]\n", myRank,
                        snapshotClock.p[0], snapshotClock.p[1], snapshotClock.p[2]);
                 pthread_mutex_unlock(&clockMutex);
 
                 // Marca que o marcador foi recebido deste canal
                 receivedMarkerControll[sender] = 1;
 
                 // Envia marcadores para todos os canais de saída
                 for (int i = 0; i < NUM_OF_PROCESS; i++) {
                     if (i != myRank) {
                         Message markerMessage;
                         for (int j = 0; j < NUM_OF_PROCESS; j++) {
                             markerMessage.clock.p[j] = -1;
                         }
                         markerMessage.destination = i;
                         markerMessage.isMarker = 1;
                         consumerOut(markerMessage, myRank);
                     }
                 }
             } else {
                 // Se já iniciou o snapshot, apenas marca a recepção do marcador
                 pthread_mutex_unlock(&clockMutex);
                 receivedMarkerControll[sender] = 1;
             }
 
             // Verifica se recebeu marcadores de todos os canais (exceto o próprio)
             int markersReceived = 1;
             for (int i = 0; i < NUM_OF_PROCESS; i++) {
                 if (i != myRank && receivedMarkerControll[i] == 0) {
                     markersReceived = 0;
                     break;
                 }
             }
             if (markersReceived) {
                 // Conclui o snapshot, imprime estado registrado e mensagens em trânsito
                 pthread_mutex_lock(&clockMutex);
                 realizandoSnapshot = 0;
                 printf("Processo %d snapshot completo. Clock: [%d, %d, %d]\n", myRank,
                        snapshotClock.p[0], snapshotClock.p[1], snapshotClock.p[2]);
                 
                 // Exibe as mensagens em trânsito de cada canal
                 for (int i = 0; i < NUM_OF_PROCESS; i++) {
                     if (i != myRank) {
                         printf("Mensagens em trânsito do canal %d -> %d:\n", i, myRank);
                         for (int j = 0; j < inTransitCount[i]; j++) {
                             Message msg = messagesInTransit[i][j].message;
                             printf("Mensagem de %d: Clock [%d, %d, %d]\n", i,
                                    msg.clock.p[0], msg.clock.p[1], msg.clock.p[2]);
                         }
                     }
                 }
                 // Reinicia os contadores e marcações do snapshot
                 for (int i = 0; i < NUM_OF_PROCESS; i++) {
                     inTransitCount[i] = 0;
                     receivedMarkerControll[i] = 0;
                 }
                 pthread_mutex_unlock(&clockMutex);
                 
                 // Sinaliza que o snapshot foi concluído
                 pthread_mutex_lock(&snapshotMutex);
                 snapshotComplete = 1;
                 pthread_cond_signal(&snapshotCond);
                 pthread_mutex_unlock(&snapshotMutex);
             }
         }
         else {
             // Trata mensagens normais
             Message message;
             for (int i = 0; i < NUM_OF_PROCESS; i++) {
                 message.clock.p[i] = p[i];
             }
             message.destination = myRank;
             message.isMarker = 0;
 
             pthread_mutex_lock(&clockMutex);
             // Se estiver em snapshot e não recebeu marcador deste canal, registra a mensagem em trânsito
             if (realizandoSnapshot && receivedMarkerControll[sender] == 0) {
                 messagesInTransit[sender][inTransitCount[sender]++] = (InTransitMessage){sender, message};
                 printf("Processo %d registrando mensagem de %d durante o snapshot\n", myRank, sender);
             }
             pthread_mutex_unlock(&clockMutex);
 
             // Insere a mensagem no buffer de entrada para processamento normal
             consumerCLock(message, myRank);
         }
     }
     return NULL;
 }
 
 /* 
  * Thread: startSender
  * Descrição: Loop de envio de mensagens. Consome mensagens do buffer de saída
  *            e as envia via MPI. Se a mensagem for um marcador, envia o vetor marcador
  */
 void *startSender(void* args) {
     int myRank;
     MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
 
     while (1) { 
         Message message = producerClock(myRank);
         int isMarker = message.isMarker;
         int dest = message.destination;
 
         if (isMarker) {
             // Prepara o vetor marcador com -1
             int marker[NUM_OF_PROCESS];
             for (int i = 0; i < NUM_OF_PROCESS; i++) {
                 marker[i] = -1;
             }
             MPI_Send(marker, NUM_OF_PROCESS, MPI_INT, dest, 0, MPI_COMM_WORLD);
             printf("Processo %d enviou marcador para %d\n", myRank, dest);
         } else {
             MPI_Send(message.clock.p, NUM_OF_PROCESS, MPI_INT, dest, 0, MPI_COMM_WORLD);
         }
     }
     return NULL;
 }
 
 // Threads principais ou processos
 
 /* 
  * Thread: process0
  * Descrição: Sequência de operações para o processo com rank 0:
  *            - Realiza um evento
  *            - Envia mensagem para processo 1 e recebe resposta
  *            - Inicia o snapshot
  *            - Aguarda conclusão do snapshot
  *            - Continua com envio e recepção de mensagens e eventos
  */
 void *process0(void* args) {
     int myRank;
     MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
 
     Event(myRank);
     Send(1);
     Receive(1);
 
     // Inicia o snapshot
     initiateSnapshot(myRank);
 
     // Aguarda até que o snapshot seja completado
     pthread_mutex_lock(&snapshotMutex);
     while (!snapshotComplete) {
         pthread_cond_wait(&snapshotCond, &snapshotMutex);
     }
     pthread_mutex_unlock(&snapshotMutex);
 
     // Continua com outras operações após o snapshot
     Send(2);
     Receive(2);
     Send(1);
     Event(myRank);
 
     return NULL;
 }
 
 /* 
  * Thread: process1
  * Descrição: Sequência de operações para o processo com rank 1:
  *            - Envia mensagem para processo 0
  *            - Recebe mensagens de processo 0
  */
 void *process1(void* args) {
     int myRank;
     MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
 
     Send(0);
     Receive(0);
     Receive(0);
 
     return NULL;
 }
 
 /* 
  * Thread: process2
  * Descrição: Sequência de operações para o processo com rank 2:
  *            - Realiza um evento
  *            - Envia mensagem para processo 0
  *            - Recebe mensagem de processo 0
  */
 void *process2(void* args) {
     int myRank;
     MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
 
     Event(myRank);
     Send(0);
     Receive(0);
 
     return NULL;
 }
 
 int main(void) {
     int myRank;
     MPI_Init(NULL, NULL);
     MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
 
     // Inicialização dos mutexes e variáveis de condição para os buffers e snapshot
     pthread_mutex_init(&mutexIn, NULL);
     pthread_cond_init(&condEmptyIn, NULL);
     pthread_cond_init(&condFullIn, NULL);
     
     pthread_mutex_init(&mutexOut, NULL);
     pthread_cond_init(&condEmptyOut, NULL);
     pthread_cond_init(&condFullOut, NULL);
     
     pthread_mutex_init(&clockMutex, NULL);
     
     pthread_mutex_init(&snapshotMutex, NULL);
     pthread_cond_init(&snapshotCond, NULL);
     
     // Inicializa o relógio global
     pthread_mutex_lock(&clockMutex);
     for (int i = 0; i < NUM_OF_PROCESS; i++) {
         processClockGlobal.p[i] = 0;
     }
     pthread_mutex_unlock(&clockMutex);
     
     // Inicializa o vetor de controle dos marcadores 
     for (int i = 0; i < NUM_OF_PROCESS; i++) {
         receivedMarkerControll[i] = 0;
     }
     
     // Criação das threads de recepção, envio e execução dos eventos 
     pthread_t threadReceiver, threadSender, threadClock;
     
     pthread_create(&threadReceiver, NULL, startReceiver, NULL);
     pthread_create(&threadSender, NULL, startSender, NULL);
     
     // Cria a thread correspondente ao rank do processo
     if (myRank == 0) {
         pthread_create(&threadClock, NULL, process0, NULL);
     } else if (myRank == 1) {
         pthread_create(&threadClock, NULL, process1, NULL);
     } else if (myRank == 2) {
         pthread_create(&threadClock, NULL, process2, NULL);
     }
     
     // Aguarda a finalização da thread de execução principal
     pthread_join(threadClock, NULL);
     
     // Liberação dos recursos e destruição dos mutexes e condições
     pthread_mutex_destroy(&mutexIn);
     pthread_cond_destroy(&condEmptyIn);
     pthread_cond_destroy(&condFullIn);
     
     pthread_mutex_destroy(&mutexOut);
     pthread_cond_destroy(&condEmptyOut);
     pthread_cond_destroy(&condFullOut);
     
     pthread_mutex_destroy(&clockMutex);
     
     pthread_mutex_destroy(&snapshotMutex);
     pthread_cond_destroy(&snapshotCond);
     
     MPI_Finalize();
     return 0;
 }
 