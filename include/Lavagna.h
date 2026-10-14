#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "../include/common.h"

#define LAV_PORT 5678
#define MAX_BUFFER_REQ 10
#define CMD_NUM 12
#define CMDLEN 32
#define TO_PING 60
#define TO_PONG 30

#define MAX_CLIENTS 100
#define QUIT_CASE 2
#define DES_LEN 64

#define true 1
#define false 0

#define PING 0
#define PONG 1

typedef struct client {
    int sd;
    struct sockaddr_in cl_addr;
    socklen_t len;
    int isPinged;
    int hasPonged;
    int isDoing;

    struct client *nxtClient;
} client_t;

typedef struct timeout {
    int time;
    int tipo;

    int port;

    struct timeout *nxtTimeout;
} timeout_t;

typedef enum col_enum {TO_DO, DOING, DONE} col_t;

typedef struct card {
    int ID;
    col_t Colonna;
    char TestoAttivita[DES_LEN];
    int userPort;
    time_t timestamp;

    struct card *nxtCard;
} card_t;

typedef struct lavagna {
    int ID;
    int numCards;
    card_t *todo_h;
    card_t *doing_h;
    card_t *done_h;

} lavagna_t;

typedef struct {
    int numClients;
    int numFreeClients;

    int sd_listen;
    int inContrattazione;

} state_t;

extern timeout_t* timeoutHead;
extern state_t statoLavagna;
extern client_t* clientHead;


void pushTmeout(timeout_t*);
int popTimeout(int);
void pushClient(client_t*);
int popClient(int);
void pushCard(card_t*, card_t**);
int popCard(int, card_t**);

void* alarmHandler(void*);
void* recvHandler(void*);
void* acceptHandler(void*);
void* terminalHandler(void*);

int checkID(int);
void handleReq(int, int);
void initLavagna();
int cleanDoing(int);
client_t* getClient(int);

void SHOW_LAVAGNA();
void QUIT(int);
void AVAILABLE_CARD();
void MOVE_CARD(card_t*, col_t);
int CREATE_CARD(int);
void ACK_CARD(int);
void CARD_DONE(int);
void SEND_USER_LIST(int);

void PING_USER(int);
void PONG_LAVAGNA(int);