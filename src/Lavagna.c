#include "../include/Lavagna.h"

pthread_mutex_t mutex_timeout;
timeout_t* timeoutHead = NULL;

pthread_mutex_t mutex_stato;
state_t statoLavagna;

pthread_mutex_t mutex_lavagna;
lavagna_t lavagna;

pthread_mutex_t mutex_client;
client_t* clientHead = NULL;

int main(int argc, char* argv[]){
    initLavagna();

    SHOW_LAVAGNA();

    // Inizializzo i thread principali
    pthread_t tid_accept, tid_terminal, tid_alarm;
    pthread_create(&tid_accept, NULL, acceptHandler, NULL);
    pthread_create(&tid_terminal, NULL, terminalHandler, NULL);
    pthread_create(&tid_alarm, NULL, alarmHandler, NULL);

    pthread_join(tid_accept, NULL);
    pthread_join(tid_terminal, NULL);
    pthread_join(tid_alarm, NULL);
    
    return 0;
}

// --- LISTE ---

void pushTimeout(timeout_t* newTimeout){
    pthread_mutex_lock(&mutex_timeout);
    newTimeout->nxtTimeout = NULL;

    if(timeoutHead == NULL){
        // Se non c'è nessun timeout setto l'alarm
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
        // Se non c'è nessun timeout setto l'alarm
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
        timeoutHead = to->nxtTimeout;
        // Setto l'alarm, se timeoutHead diventa nullo non c'è nessun alarm
        if(timeoutHead != NULL) alarm(timeoutHead->time);
        else alarm(0);
    }
    else to_prev->nxtTimeout = to->nxtTimeout; 

    free(to);
    pthread_mutex_unlock(&mutex_timeout);
    
    return 0;
}

void pushClient(client_t* newClient){
    newClient->nxtClient = NULL;

    if(clientHead == NULL){
        clientHead = newClient;
        return;
    }
    
    client_t* cl = clientHead;
    while(cl->nxtClient != NULL) cl = cl->nxtClient;

    cl->nxtClient = newClient;
}

client_t* getClient(int port){
    client_t* client = clientHead;
    while(client != NULL && ntohs(client->cl_addr.sin_port) != port){
        client = client->nxtClient;
    }

    return client;
}

int popClient(int port){
    pthread_mutex_lock(&mutex_client);
    client_t* cl = clientHead;
    client_t* cl_prev = NULL;

    while(cl != NULL && ntohs(cl->cl_addr.sin_port) != port){
        cl_prev = cl;
        cl = cl->nxtClient;
    }
    
    if(cl == NULL) {
        pthread_mutex_unlock(&mutex_client);
        return -1;
    }

    if(cl_prev == NULL) clientHead = cl->nxtClient;
    else cl_prev->nxtClient = cl->nxtClient;
    pthread_mutex_unlock(&mutex_client);

    // se il client ha qualche carta in doing e non gli è scattato pong, ha un timeout
    pthread_mutex_lock(&mutex_client);
    if(!cl->hasPonged){
        pthread_mutex_unlock(&mutex_client);
        if(cleanDoing(port) != 0) popTimeout(port);
        else{
            pthread_mutex_lock(&mutex_stato);
            --statoLavagna.numFreeClients;
            pthread_mutex_unlock(&mutex_stato);
        }
    }
    else pthread_mutex_unlock(&mutex_client);
    
    
    pthread_mutex_lock(&mutex_stato);
    --statoLavagna.numClients;
    pthread_mutex_unlock(&mutex_stato);

    pthread_mutex_lock(&mutex_client);
    close(cl->sd);
    free(cl);
    pthread_mutex_unlock(&mutex_client);

    return 0;
}

void pushCard(card_t* newCard, card_t** card_list){
    newCard->nxtCard = NULL;

    if(*card_list == NULL){
        *card_list = newCard;
        return;
    }

    card_t* card = *card_list;
    while(card->nxtCard != NULL) card = card->nxtCard;

    card->nxtCard = newCard;
}

int popCard(int id, card_t** card_list){
    card_t* card = *card_list;
    card_t* c_prev = NULL;

    while(card != NULL && card->ID != id){
        c_prev = card;
        card = card->nxtCard;
    }

    if(card == NULL) return -1;

    if(c_prev == NULL) *card_list = card->nxtCard;
    else c_prev->nxtCard = card->nxtCard;

    // non si effettua la free, perché non si rimuove mai le card

    return 0;
}

// --- THREAD HANDLER ---

void* alarmHandler(void*){
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);

    int sig, port;
    for(;;){
        sigwait(&set, &sig);

        pthread_mutex_lock(&mutex_timeout);

        port = timeoutHead->port;

        if(timeoutHead->tipo == PING){
            pthread_mutex_unlock(&mutex_timeout);

            if(popTimeout(port) == -1){
                printf("[alarm] Errore nella popTimeout.\n");
                exit(EXIT_FAILURE);
            }
            
            PING_USER(port);
        }
        else {
            pthread_mutex_unlock(&mutex_timeout);

            if(popClient(port) == -1){
                printf("[alarm] Errore nella popClient.\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    pthread_exit(NULL);
}

void* acceptHandler(void*){
    int port;

    for(;;){
        client_t *newClient = malloc(sizeof(client_t));
        if(newClient == NULL){
            perror("[accept]");
            exit(EXIT_FAILURE);
        }

        newClient->len = sizeof(newClient->cl_addr);
        newClient->isPinged = false;
        newClient->hasPonged = false;
        newClient->isDoing = false;
        newClient->sd = accept(statoLavagna.sd_listen, (struct sockaddr *)& newClient->cl_addr, &newClient->len);
        
        pthread_mutex_lock(&mutex_stato);
        if(statoLavagna.numClients >= MAX_CLIENTS){
            close(newClient->sd);
            free(newClient);
            pthread_mutex_unlock(&mutex_stato);
            printf("[accept] MAX_CLIENTS raggiunto, connessione rifiutata.\n");
            continue;
        }
        else pthread_mutex_unlock(&mutex_stato);

        if(newClient->sd == -1){
            perror("[accept]");
            free(newClient);
            continue;
        }

        port = ntohs(newClient->cl_addr.sin_port);

        pthread_mutex_lock(&mutex_client);
        pushClient(newClient);
        printf("[accept] client %d connesso.\n", port);
        pthread_mutex_unlock(&mutex_client);

        pthread_mutex_lock(&mutex_stato);
        ++statoLavagna.numClients;
        ++statoLavagna.numFreeClients;

        printf("[accept] numero client connessi: %d.\n", statoLavagna.numClients);
        pthread_mutex_unlock(&mutex_stato);
    
        // si è connesso un nuovo client, si verifica se ci sono le condizioni per un asta
        AVAILABLE_CARD();

        // associo al nuovo client un thread
        pthread_t new_tid;
        int* p_port = malloc(sizeof(int));
        if(p_port == NULL){
            perror("[malloc]");
            exit(EXIT_FAILURE);
        }

        *p_port = port;
        pthread_create(&new_tid, NULL, recvHandler, p_port);
        pthread_detach(new_tid);
    }

    pthread_exit(NULL);
}

void* recvHandler(void* arg){
    int port;
    memcpy(&port, arg, sizeof(int));
    free(arg);

    pthread_mutex_lock(&mutex_client);
    client_t* client = getClient(port);
    int sd = client->sd;
    pthread_mutex_unlock(&mutex_client);
    
    char buf[CMDLEN];
    int ret;

    while((ret = recv(sd, buf, CMDLEN, MSG_WAITALL)) > 0){
        buf[ret] = '\0';
        printf("[client %d] %s\n", port, buf);

        ret = checkInput(buf, CMD_NUM);
        handleReq(ret, port);

        if(ret == QUIT_CASE) break;
    }

    popClient(port);

    pthread_exit(NULL);
}

void* terminalHandler(void*){
    char buf[CMDLEN];

    for(;;){
        if(fgets(buf, sizeof(buf), stdin) != NULL){
            buf[strlen(buf) - 1] = '\0';
            int ret = checkInput(buf, CMD_NUM);
            handleReq(ret, false);
        }
    }

    pthread_exit(NULL);
}

// --- VARIE ---

int checkID(int id){
    // verifico se l'id della carta inviata è o meno presente
    pthread_mutex_lock(&mutex_lavagna);
    card_t *curCard = lavagna.todo_h;
    int numCol = 0;

    for(;;){
        if(curCard == NULL){
            if(numCol == 0) curCard = lavagna.doing_h;
            if(numCol == 1) curCard = lavagna.done_h;
            if(numCol == 2) {
                pthread_mutex_unlock(&mutex_lavagna);
                return 0;
            }
            ++numCol;
            continue;
        }
        
        if(id == curCard->ID) {
            pthread_mutex_unlock(&mutex_lavagna);
            return -1;
        }
        curCard = curCard->nxtCard;
    }

    pthread_mutex_unlock(&mutex_lavagna);
    return -1;
}

void initLavagna(){;
    // inizializzo le strutture dati statiche della lavagna
    lavagna.ID = 0;
    lavagna.numCards = 0;
    lavagna.todo_h = NULL;
    lavagna.doing_h = NULL;
    lavagna.done_h = NULL;

    statoLavagna.numClients = 0;
    statoLavagna.numFreeClients = 0;
    statoLavagna.inContrattazione = false;

    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));

    statoLavagna.sd_listen = setupSocket(&my_addr, LAV_PORT, SOCK_STREAM);
    if(statoLavagna.sd_listen == -1) exit(EXIT_FAILURE);
	
    if(listen(statoLavagna.sd_listen, MAX_BUFFER_REQ) == -1){
		perror("[listen]");
        exit(EXIT_FAILURE);
	}

    pthread_mutex_init(&mutex_client, NULL);
    pthread_mutex_init(&mutex_lavagna, NULL);
    pthread_mutex_init(&mutex_stato, NULL);
    pthread_mutex_init(&mutex_timeout, NULL);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void handleReq(int ret, int port){
    if(port == false){
        // terminale
        switch(ret){
            case 4:
                SHOW_LAVAGNA();
            break;
            default:
                printf("[debug] input scorretto.\n");
            break;
        }
    }
    else{
        // utente
        switch(ret){
            case 0:
                CREATE_CARD(port);
            break;
            case 2:
                QUIT(port);
            break;
            case 7:
                PONG_LAVAGNA(port);
            break;
            case 10:
                ACK_CARD(port);
            break;
            case 11:
                CARD_DONE(port);
            break;
            default:
                printf("[debug] input scorretto.\n");
            break;
       }

    }
}

int cleanDoing(int port){
    pthread_mutex_lock(&mutex_lavagna);
    card_t *card = lavagna.doing_h;

    while(card != NULL){
        if(card->userPort == port){
            MOVE_CARD(card, TO_DO);
            // unlock fatta dentro MOVE_CARD
            AVAILABLE_CARD();
            return 1;
        }
        card = card->nxtCard;
    }

    pthread_mutex_unlock(&mutex_lavagna);
    return 0;
}

// --- COMANDI ---

void PING_USER(int port){
    pthread_mutex_lock(&mutex_client);

    client_t* client = getClient(port);

    if(client == NULL){
        printf("Utente disconnesso?.\n");
        exit(EXIT_FAILURE);
    }
    
    char buf[CMDLEN];
    strcpy(buf, "PING_USER");

    if(send(client->sd, buf, sizeof(buf), 0) == -1){
        perror("[send]");
        exit(EXIT_FAILURE);
    }

    client->isPinged = true;

    timeout_t* to = malloc(sizeof(timeout_t));
    if(to == NULL){
        perror("[malloc]");
        exit(EXIT_FAILURE);
    }

    to->port = ntohs(client->cl_addr.sin_port);
    to->time = TO_PONG;
    to->tipo = PONG;
    pthread_mutex_unlock(&mutex_client);

    pushTimeout(to);
}

void PONG_LAVAGNA(int port){
    pthread_mutex_lock(&mutex_client);
    client_t* client = getClient(port);
    if(!client->isPinged){
        pthread_mutex_unlock(&mutex_client);    
        return;
    }

    client->hasPonged = true;

    pthread_mutex_unlock(&mutex_client);

    popTimeout(port);
}

void QUIT(int port){
    // la pop si occupa di chiudere il socket
    if(popClient(port) == -1){
        printf("[debug] errore nella pop.\n");
        exit(EXIT_FAILURE);
    }
}

void ACK_CARD(int port){
    pthread_mutex_lock(&mutex_lavagna);
    card_t* card = lavagna.todo_h;
    if(card == NULL) {
        pthread_mutex_unlock(&mutex_lavagna);
        return;
    }

    card->timestamp = time(NULL);
    card->userPort = port;
    pthread_mutex_unlock(&mutex_lavagna);
    
    pthread_mutex_lock(&mutex_client);
    client_t* client = getClient(port);
    client->isDoing = true;
    pthread_mutex_unlock(&mutex_client);

    pthread_mutex_lock(&mutex_lavagna);
    statoLavagna.inContrattazione = false;
    --statoLavagna.numFreeClients;
    MOVE_CARD(card, DOING);
    // la unlock la fa MOVE_CARD

    timeout_t* to = malloc(sizeof(timeout_t));
    if(to == NULL){
        perror("[malloc]");
        exit(EXIT_FAILURE);
    }

    to->port = port;
    to->time = TO_PING;
    to->tipo = PING;

    pushTimeout(to);
}

void CARD_DONE(int port){
    pthread_mutex_lock(&mutex_client);
    client_t* client = getClient(port);

    client->isDoing = false;
    client->isPinged = false;

    // se non ha fatto pong c'è il timeout
    if(!client->hasPonged){
        pthread_mutex_unlock(&mutex_client);
        popTimeout(port);
    }
    else pthread_mutex_unlock(&mutex_client);

    pthread_mutex_lock(&mutex_stato);
    ++statoLavagna.numFreeClients;
    pthread_mutex_unlock(&mutex_stato);

    pthread_mutex_lock(&mutex_lavagna);
    card_t* card = lavagna.doing_h;
    while(card != NULL){
        if(card->userPort == port){
            card->timestamp = time(NULL);
            MOVE_CARD(card, DONE);
            // unlock fatta da move_card
            AVAILABLE_CARD();
            break;
        }
        card = card->nxtCard;
    }
    // unlock fatta da move_card
}

// Richiede lock(mutex_lavagna) - fa la unlock internamente
void MOVE_CARD(card_t* card, col_t newCol){
    if(card == NULL) return;
    
    if(card->Colonna == TO_DO) popCard(card->ID, &lavagna.todo_h);
    if(card->Colonna == DOING) popCard(card->ID, &lavagna.doing_h);
    if(card->Colonna == DONE) popCard(card->ID, &lavagna.done_h);
    pthread_mutex_unlock(&mutex_lavagna);

    // la AVAILABLE_CARD è eseguita esternamente

    switch(newCol){
        case TO_DO:
            pthread_mutex_lock(&mutex_lavagna);
            card->Colonna = TO_DO;
            pushCard(card, &lavagna.todo_h);
            pthread_mutex_unlock(&mutex_lavagna);
        break;
        case DOING:
            pthread_mutex_lock(&mutex_lavagna);
            card->Colonna = DOING;
            pushCard(card, &lavagna.doing_h);
            pthread_mutex_unlock(&mutex_lavagna);
        break;
        case DONE:
            pthread_mutex_lock(&mutex_lavagna);
            card->Colonna = DONE;
            pushCard(card, &lavagna.done_h);
            pthread_mutex_unlock(&mutex_lavagna);
        break;
    }

    // ogni volta che si cambia la posizione di una card si ristampa la lavagna
    SHOW_LAVAGNA();
}

int CREATE_CARD(int port){
    int ret, id;
    int len = DES_LEN + sizeof(int) + 1;
    char buf[len];
    card_t* newCard = malloc(sizeof(card_t));
    if(newCard == NULL){
        perror("[malloc]");
        exit(EXIT_FAILURE);
    }
    
    newCard->Colonna = TO_DO;
    newCard->timestamp = time(NULL);
    newCard->userPort = port;

    pthread_mutex_lock(&mutex_client);
    client_t* client = getClient(port);
    int sd = client->sd;
    pthread_mutex_unlock(&mutex_client);

    ret = recv(sd, buf, len, MSG_WAITALL);
    if(ret == -1) return ret;
    
    memcpy(&id, buf, sizeof(int));
    id = ntohl(id);
    memcpy(newCard->TestoAttivita, buf + sizeof(int) + 1, DES_LEN);

    if(checkID(id) == -1) {
        printf("[create_card] id %i già in uso.\n", id);
        free(newCard);
        return -1;
    }

    newCard->ID = id;
    newCard->TestoAttivita[DES_LEN - 1] = '\0';

    pthread_mutex_lock(&mutex_lavagna);
    ++lavagna.numCards;
    pushCard(newCard, &lavagna.todo_h);
    printf("[create_card] ricevuta card %i, %s.\n", newCard->ID, newCard->TestoAttivita);
    pthread_mutex_unlock(&mutex_lavagna);

    SHOW_LAVAGNA();
    AVAILABLE_CARD();
    
    return 0;
}

void AVAILABLE_CARD(){
    pthread_mutex_lock(&mutex_lavagna);
    card_t* card = lavagna.todo_h;
    if(card == NULL){
        pthread_mutex_unlock(&mutex_lavagna);
        return;
    }
    pthread_mutex_unlock(&mutex_lavagna);

    // Se gli utenti sono in contrattazione si abortisce la AVAILABLE_CARD

    pthread_mutex_lock(&mutex_stato);
    if(statoLavagna.numFreeClients < 2  || statoLavagna.inContrattazione) {
        pthread_mutex_unlock(&mutex_stato);
        return;
    }

    statoLavagna.inContrattazione = true;
    int numClients = statoLavagna.numFreeClients - 1;
    pthread_mutex_unlock(&mutex_stato);

    char buf[BUFSIZ + sizeof(int)];
    char bufCmd[CMDLEN] = {0};
    strcpy(bufCmd, "AVAILABLE_CARD");
    int intNet = htonl(card->ID);
    memcpy(buf, &intNet, sizeof(int));
    memcpy(buf + sizeof(int), card->TestoAttivita, sizeof(card->TestoAttivita));
    int ret;

    pthread_mutex_lock(&mutex_client);
    client_t* client = clientHead;
    int port;
    while(client != NULL){
        if(client->isDoing){
            client = client->nxtClient;
            continue;
        }

        ret = send(client->sd, bufCmd, CMDLEN, 0);
        if(ret == -1) perror("[send]");

        ret = send(client->sd, buf, sizeof(buf), 0);
        if(ret == -1) perror("[send]");

        intNet = htonl(numClients);
        ret = send(client->sd, &intNet, sizeof(int), 0);
        if(ret == -1) perror("[send]");

        port = ntohs(client->cl_addr.sin_port);

        pthread_mutex_unlock(&mutex_client);
        // Inviata a tutti coloro che non stanno eseguendo una carta
        SEND_USER_LIST(port);
        pthread_mutex_lock(&mutex_client);

        client = client->nxtClient;
    }
    pthread_mutex_unlock(&mutex_client);
}

void SEND_USER_LIST(int myPort){
    // Invio agli utenti la lista delle porti di tutti gli altri
    char bufList[BUFSIZ];
    int ret, port;
    int i = 0;

    pthread_mutex_lock(&mutex_client);
    client_t* cl = clientHead;
    client_t* client = getClient(myPort);
    myPort = htons(myPort);

    while(cl != NULL){
        port = cl->cl_addr.sin_port;
        if(port != myPort && !cl->isDoing){
            memcpy(bufList + sizeof(int) * i, &cl->cl_addr.sin_port, sizeof(int));
            i++;
        }
        cl = cl->nxtClient;
    }

    ret = send(client->sd, bufList, sizeof(int) * i, 0);
    pthread_mutex_unlock(&mutex_client);
    if(ret == -1) perror("[send]");
}

void SHOW_LAVAGNA() {
    pthread_mutex_lock(&mutex_lavagna);
    printf("\n--- Lavagna | %d ---\n", lavagna.ID);
    printf("%-25s | %-25s | %-25s\n", "TODO", "DOING", "DONE");
    printf("--------------------------------------------------------------------------\n");

    card_t *todoTMP  = lavagna.todo_h;
    card_t *doingTMP = lavagna.doing_h;
    card_t *doneTMP  = lavagna.done_h;

    if(todoTMP == NULL && doingTMP == NULL && doneTMP == NULL) {
        printf("%-25s | %-25s | %-25s\n", "*", "*", "*");
        pthread_mutex_unlock(&mutex_lavagna);
        return;
    }

    while(todoTMP || doingTMP || doneTMP) {
        if(todoTMP){
            printf("%-3d %-20s", todoTMP->ID, todoTMP->TestoAttivita);
            todoTMP = todoTMP->nxtCard;
        }
        else printf("%-25s", "*");

        printf("  | ");

        if(doingTMP){
            printf("%-3d %-20s", doingTMP->ID, doingTMP->TestoAttivita);
            doingTMP = doingTMP->nxtCard;
        }
        else printf("%-25s", "*");

        printf(" | ");

        if(doneTMP){
            printf("%-3d %-20s", doneTMP->ID, doneTMP->TestoAttivita);
            doneTMP = doneTMP->nxtCard;
        }
        else printf("%-25s", "*");

        printf("\n");
    }
    printf("\n");
    pthread_mutex_unlock(&mutex_lavagna);
}