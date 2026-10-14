#include "../include/Utente.h"

pthread_mutex_t mutex_stato;
state_t statoClient;

pthread_mutex_t mutex_client;
client_t* clientHead = NULL;

pthread_mutex_t mutex_card;
card_t card;

pthread_mutex_t mutex_timeout;
timeout_t* timeoutHead = NULL;

int main(int argc, char* argv[]){
    if(argc != 2){
        printf("[debug] Numero di parametri errato.\n");
        exit(EXIT_FAILURE);
    }

    statoClient.myPort = atoi(argv[1]);
    
    initUtente();

    // hello inviata di default all'inizio, un utente è inutile senza
    HELLO();

    pthread_t tid_listen, tid_terminal, tid_udp, tid_alarm;
    pthread_create(&tid_listen, NULL, listenHandler, NULL);
    pthread_create(&tid_terminal, NULL, terminalHandler, NULL);
    pthread_create(&tid_udp, NULL, udpHandler, NULL);
    pthread_create(&tid_alarm, NULL, alarmHandler, NULL);

    pthread_join(tid_listen, NULL);
    pthread_join(tid_udp, NULL);
    pthread_join(tid_terminal, NULL);
    pthread_join(tid_alarm, NULL);

    return 0;
}

// --- LISTE ---

void pushTimeout(timeout_t* newTimeout){
    pthread_mutex_lock(&mutex_timeout);
    newTimeout->nxtTimeout = NULL;

    if(timeoutHead == NULL){
        // Se è il primo timer attivo alarm
        timeoutHead = newTimeout;
        alarm(newTimeout->time);
        pthread_mutex_unlock(&mutex_timeout);
        return;
    }

    timeout_t* to = timeoutHead;
    timeout_t* prev = NULL;

    while(to != NULL && newTimeout->time >= to->time){
        newTimeout->time -= to->time;
        prev = to;
        to = to->nxtTimeout;
    }

    if(prev == NULL){
        // Se è il primo timer attivo alarm
        to->time -= newTimeout->time;
        newTimeout->nxtTimeout = to;
        timeoutHead = newTimeout;
        alarm(timeoutHead->time);
        pthread_mutex_unlock(&mutex_timeout);
        return;
    }

    if(to != NULL) to->time -= newTimeout->time;
    
    newTimeout->nxtTimeout = to;
    prev->nxtTimeout = newTimeout;
    pthread_mutex_unlock(&mutex_timeout);
}

int popTimeout(int port){
    pthread_mutex_lock(&mutex_timeout);
    if(timeoutHead == NULL){
        pthread_mutex_unlock(&mutex_timeout);
        return -1;
    }

    timeout_t* to = timeoutHead;
    timeout_t* to_prev = NULL;

    while(to != NULL && to->port != port){
        to_prev = to;
        to = to->nxtTimeout;
    }

    if(to == NULL){
        pthread_mutex_unlock(&mutex_timeout);
        return -1;
    }

    if(to->nxtTimeout != NULL) to->nxtTimeout->time += to->time;
    
    if(to_prev == NULL) {
        // Se non ci sono piu elementi nella lista resetto l'alarm
        timeoutHead = to->nxtTimeout;
        if(timeoutHead != NULL) alarm(timeoutHead->time);
        else alarm(0);
    }
    else to_prev->nxtTimeout = to->nxtTimeout; 

    free(to);
    pthread_mutex_unlock(&mutex_timeout);
    
    return 0;
}

int pushClient(client_t* newClient){
    newClient->nxtClient = NULL;
    pthread_mutex_lock(&mutex_client);
    if(clientHead == NULL){
        // primo client
        clientHead = newClient;
        pthread_mutex_unlock(&mutex_client);
        return 0;
    }
    
    client_t* cl = clientHead;
    while(cl->nxtClient != NULL){
        if(cl->port == newClient->port) {
            free(newClient);
            pthread_mutex_unlock(&mutex_client);
            return -1;
        }
        cl = cl->nxtClient;
    }
    
    if(cl->port == newClient->port) {
        free(newClient);
        pthread_mutex_unlock(&mutex_client);
        return -1;
    }

    cl->nxtClient = newClient;
    pthread_mutex_unlock(&mutex_client);
    return 0;
}

void destroyClient(){
    // Una volta che la contrattazione termina si elimina la lista degli utenti a cui si è inviato n.
    pthread_mutex_lock(&mutex_client);
    client_t* client = clientHead;
    client_t* prev_cl = NULL;
    while(client != NULL){
        prev_cl = client;
        client = client->nxtClient;
        free(prev_cl);
    }

    clientHead = NULL;
    pthread_mutex_unlock(&mutex_client);
}

// --- THREAD HANDLER ---

void* udpHandler(void*){
    int new_n = 0;
    int portMin = -1, min_n = -1;
    int numACK = 0, numN = 0;

    int value, port;

    struct sockaddr_in sender_addr;
    socklen_t addrlen = sizeof(sender_addr);

    while(recvfrom(statoClient.sd_udp, &value, sizeof(int), 0, (struct sockaddr*)&sender_addr, &addrlen) != -1){
        pthread_mutex_lock(&mutex_card);
        if(card.ID == -1){
            // se ricevo degli n prima di aver ricevuto la card, aspetto un secondo
            pthread_mutex_unlock(&mutex_card);
            sleep(1);
        }
        else pthread_mutex_unlock(&mutex_card);

        port = ntohs(sender_addr.sin_port);
        value = ntohl(value);

        if(value == ACK){
            ++numACK;
            popTimeout(port);
        }
        else{
            new_n = value;

            client_t* newClient = malloc(sizeof(client_t));
            if(newClient == NULL){
                perror("[malloc]");
                exit(EXIT_FAILURE);
            }

            newClient->port = port;

            if(pushClient(newClient) == -1){
                // se ho già ricevuto n da questo utente, reinvio ack.
                sendN(port, ACK);
                continue;
            }

            // avendo ricevuto correttamente n invio l'ack
            ++numN;
            sendN(port, ACK);

            if(new_n < min_n || ((new_n == min_n) && portMin > port) || min_n == -1){
                // Un altro utente è il minimo
                portMin = port;
                min_n = new_n;
            }
        }

        pthread_mutex_lock(&mutex_stato);
        if(numN == statoClient.numClients && numACK == statoClient.numClients){
            pthread_mutex_unlock(&mutex_stato);
            printf("[udp] Ho letto n da tutti i client.\n");

            destroyClient();
            
            pthread_mutex_lock(&mutex_stato);
            if(statoClient.n < min_n || ((statoClient.n == min_n) && statoClient.myPort < portMin)){
                // Sono l'utente di costo minimo
                pthread_mutex_unlock(&mutex_stato);
                
                pthread_mutex_lock(&mutex_card);
                printf("[udp] Svolgo l'attività %i.\n", card.ID);
                pthread_mutex_unlock(&mutex_card);

                sendCMD("ACK_CARD");
                // Simulo uno svolgimento
                sleep((rand() % 15) + 10);
                printf("[udp] Attività terminata.\n");
                sendCMD("CARD_DONE");
            }
            else pthread_mutex_unlock(&mutex_stato);

            numN = 0;
            numACK = 0;
            min_n = -1;
            portMin = -1;

            pthread_mutex_lock(&mutex_card);
            card.ID = -1;
            pthread_mutex_unlock(&mutex_card);

        }
        else pthread_mutex_unlock(&mutex_stato);
    }

    exit(EXIT_FAILURE);
}

void* listenHandler(void*){
    char buf[CMDLEN];
    int ret;

    pthread_mutex_lock(&mutex_stato);
    int sd = statoClient.sd_lav;
    pthread_mutex_unlock(&mutex_stato);

    while((recv(sd, buf, CMDLEN, MSG_WAITALL)) == CMDLEN){
        buf[CMDLEN - 1] = '\0';
        ret = checkInput(buf, CMD_NUM);
        handleReq(ret, false);
    }

    printf("[debug] server disconnesso.\n");
    exit(EXIT_FAILURE);
}

void* terminalHandler(void*){
    char buf[BUFSIZ];

    for(;;){
        if(fgets(buf, sizeof(buf), stdin) != NULL){
            buf[strlen(buf) - 1] = '\0';
            int ret = checkInput(buf, CMD_NUM);
            handleReq(ret, true);

            if(ret == QUIT_CASE) break;
        }
    }

    exit(EXIT_SUCCESS);
}

void* alarmHandler(void*){
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);

    int sig, port, n;
    for(;;){
        sigwait(&set, &sig);

        pthread_mutex_lock(&mutex_timeout);
        port = timeoutHead->port;
        pthread_mutex_unlock(&mutex_timeout);
        
        popTimeout(port);
        
        pthread_mutex_lock(&mutex_stato);
        n = statoClient.n;
        pthread_mutex_unlock(&mutex_stato);

        // Invio il mio N
        sendN(port, n);

        timeout_t* to = malloc(sizeof(timeout_t));
        if(to == NULL){
            perror("[malloc]");
            exit(EXIT_FAILURE);
        }
        to->port = port;
        to->time = TO_ACK;

        // Setto il timeout per la ritrasmissione
        pushTimeout(to);
    }
}

// --- VARIE ---

void initUtente(){
    // Simulo un istante di connessione randomico per avere valori di n diversi fra utenti in questo caso sulla stessa macchina
    srand(time(NULL));

	memset(&statoClient.sv_addr, 0, sizeof(statoClient.sv_addr));
	statoClient.sv_addr.sin_family = AF_INET;
	statoClient.sv_addr.sin_port = htons(LAV_PORT);
	inet_pton(AF_INET, ADDRESS, &statoClient.sv_addr.sin_addr);

    statoClient.sd_lav = setupSocket(&statoClient.my_addr, statoClient.myPort, SOCK_STREAM);

    statoClient.sd_udp = setupSocket(&statoClient.my_addr, statoClient.myPort, SOCK_DGRAM);

    pthread_mutex_init(&mutex_client, NULL);
    pthread_mutex_init(&mutex_card, NULL);
    pthread_mutex_init(&mutex_stato, NULL);
    pthread_mutex_init(&mutex_timeout, NULL);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void handleReq(int ret, int isTerminal){
    if(isTerminal){
        switch(ret){
            // terminale
            case 0:
                CREATE_CARD();
            break;
            case 1:
                HELLO();
            break;
            case 2:
                QUIT();
            break;
            default:
                printf("[debug] input scorretto.\n");
            break;
        }
    }
    else{
        switch(ret){
            // lavagna
            case 6:
                sendCMD("PONG_LAVAGNA");
            break;
            case 8:
                AVAILABLE_CARD();
            break;
            default:
                printf("[debug] input scorretto.\n");
            break;
        }
    }
}

void sendN(int port, int n){
    int nNet = htonl(n);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;

    pthread_mutex_lock(&mutex_stato);
    int ret = sendto(statoClient.sd_udp, &nNet, sizeof(int), 0, (struct sockaddr*)&addr, sizeof(addr));
    pthread_mutex_unlock(&mutex_stato);

    if(ret == -1) perror("[sendto]");
    else if(n != 0) printf("[send_N] Inviato n a %i\n", port);
}

// -- COMANDI ---

void sendCMD(char* msg){
    // invio un comando generico
    char buf[CMDLEN];
    strcpy(buf, msg);

    pthread_mutex_lock(&mutex_stato);
    if(send(statoClient.sd_lav, buf, CMDLEN, 0) == -1){
        perror("[send]");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&mutex_stato);
}

int CREATE_CARD(){
    char buf[CMDLEN];
    char bufID[ID_MAXCHAR];
    char bufDes[DES_LEN];
    char pipe = '|';

    char bufSend[sizeof(int) + DES_LEN + 1];

    strcpy(buf, "CREATE_CARD");

    pthread_mutex_lock(&mutex_stato);
    int sd = statoClient.sd_lav;
    pthread_mutex_unlock(&mutex_stato);

    if(send(sd, buf, CMDLEN, 0) == -1){
        perror("[send]");
        exit(EXIT_FAILURE);
    }
    
    printf("[create_card] Inserisci id: ");
    fgets(bufID, ID_MAXCHAR, stdin);
    int id = atoi(bufID);
    int idNet = htonl(id);

    printf("[create_card] Inserisci descrizione: ");
    fgets(bufDes, DES_LEN, stdin);
    bufDes[strlen(bufDes) - 1] = '\0';

    int len = 0;

    memcpy(bufSend, &idNet, sizeof(int));
    len += sizeof(int);
    memcpy(bufSend + len, &pipe, sizeof(char));
    len += sizeof(char);
    memcpy(bufSend + sizeof(int) + sizeof(char), bufDes, DES_LEN);
    len += DES_LEN;

    if(send(sd, bufSend, len, 0) == -1){
        perror("[send]");
        exit(EXIT_FAILURE);
    }

    printf("[create_card] Card inviata.\n");
    
    return 0;
}

void QUIT(){
    char buf[CMDLEN];
    strcpy(buf, "QUIT");

    pthread_mutex_lock(&mutex_stato);
    if(send(statoClient.sd_lav, buf, CMDLEN, 0) == -1){
        perror("[send]");
        close(statoClient.sd_lav);
        exit(EXIT_FAILURE);
    }

    if(close(statoClient.sd_lav) == -1){
        perror("[close]");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&mutex_stato);

    exit(EXIT_SUCCESS);
}

void HELLO(){
    if(connect(statoClient.sd_lav, (struct sockaddr*)& statoClient.sv_addr, sizeof(statoClient.sv_addr)) == -1){
		perror("[connect]");
        close(statoClient.sd_lav);
		exit(EXIT_FAILURE);
	}

	printf("[hello] Registrazione alla lavagna effettuata.\n");
}

void CHOOSE_USER(char* bufClient){
    int netPort;

    pthread_mutex_lock(&mutex_stato);
    int numClients = statoClient.numClients;
    // n = 0 usato per inviare l'ack con lo stesso formato di messaggio
    statoClient.n = rand() + 1;
    int n = statoClient.n;
    pthread_mutex_unlock(&mutex_stato);

    for(int i = 0; i < numClients; i++){
        memcpy(&netPort, bufClient + i * sizeof(int), sizeof(int));

        // invio il mio n
        sendN(ntohs(netPort), n);

        timeout_t* to = malloc(sizeof(timeout_t));
        if(to == NULL){
            perror("[malloc]");
            exit(EXIT_FAILURE);
        }
        to->port = ntohs(netPort);
        to->time = TO_ACK;
        pushTimeout(to);
    }
}

void AVAILABLE_CARD(){
    printf("---------------------------------------------------\n");

    int ret, numClients;
    char buf[BUFSIZ + sizeof(int)];

    pthread_mutex_lock(&mutex_stato);
    int sd = statoClient.sd_lav;
    pthread_mutex_unlock(&mutex_stato);

    ret = recv(sd, buf, sizeof(buf), 0);
    if(ret == -1) perror("[recv]");

    pthread_mutex_lock(&mutex_card);
    memcpy(&card.ID, buf, sizeof(int));
    memcpy(&card.TestoAttivita, buf + sizeof(int), BUFSIZ);
    card.ID = ntohl(card.ID);
    pthread_mutex_unlock(&mutex_card);

    ret = recv(sd, &numClients, sizeof(int), 0);
    if(ret == -1) perror("[recv]");
    numClients = ntohl(numClients);

    pthread_mutex_lock(&mutex_stato);
    statoClient.numClients = numClients;
    pthread_mutex_unlock(&mutex_stato);

    char bufClient[numClients * sizeof(int)];

    ret = recv(sd, bufClient, sizeof(bufClient), MSG_WAITALL);
    if(ret == -1) perror("[recv]");

    pthread_mutex_lock(&mutex_card);
    printf("[available_card] Ricevuta card %i, desc: %s.\n", card.ID, card.TestoAttivita);
    pthread_mutex_unlock(&mutex_card);

    CHOOSE_USER(bufClient);
}