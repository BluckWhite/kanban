#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

#include "../include/common.h"

#define LAV_PORT 5678
#define ADDRESS "127.0.0.1"
#define CMD_NUM 12
#define CMDLEN 32
#define TO_ACK 15
#define ACK 0
#define true 1
#define false 0
#define QUIT_CASE 2
#define DES_LEN 64
#define ID_MAXCHAR 8

typedef struct card {
    int ID;
    char TestoAttivita[BUFSIZ];
} card_t;

typedef struct timeout {
    int time;
    int port;

    struct timeout *nxtTimeout;
} timeout_t;

typedef struct client {
    int port;

    struct client *nxtClient;
} client_t;

typedef struct state{
    int sd_lav;
    int sd_udp;

    int myPort;
    int n;

    struct sockaddr_in my_addr;
    struct sockaddr_in sv_addr;
    int numClients;

} state_t;

typedef enum col_enum {TO_DO, DOING, DONE} col_t;

extern state_t statoClient;
extern client_t* clientHead;
extern card_t card;

void* terminalHandler(void*);
void* listenHandler(void*);
void* udpHandler(void*);
void* alarmHandler(void*);

int pushClient(client_t*);
int popClient(int);
void pushTimeout(timeout_t*);
int popTimeout(int);

void sendN(int, int);
void sendCMD(char*);
void destroyClient();
void handleReq(int, int);
void initUtente();

void HELLO();
void QUIT();
int CREATE_CARD();
void AVAILABLE_CARD();
void CHOOSE_USER(char*);