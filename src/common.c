#include "../include/common.h"

const char* cmdList[] = {
    "CREATE_CARD", "HELLO", "QUIT", "MOVE_CARD", "SHOW_LAVAGNA",
    "SEND_USER_LIST", "PING_USER", "PONG_LAVAGNA", "AVAILABLE_CARD",
    "CHOOSE_USER", "ACK_CARD", "CARD_DONE"
};

int checkInput(char* buf, int cmdNum){
    for(int i = 0; i < cmdNum; ++i){
        if(strcmp(cmdList[i], buf) == 0) 
            return i;
    }
    return -1;
}

int setupSocket(struct sockaddr_in *my_addr, int portNum, int tipo){
    int sd = socket(AF_INET, tipo, 0);
    if(sd == -1){
        perror("[socket]");
        return sd;
    }

    memset(my_addr, 0, sizeof(*my_addr));
    my_addr->sin_family = AF_INET;
    my_addr->sin_port = htons(portNum);
    my_addr->sin_addr.s_addr = INADDR_ANY;

    if(bind(sd, (struct sockaddr*) my_addr, sizeof(*my_addr)) == -1){
        perror("[bind]");
        close(sd);
        return sd;
    }

    return sd;
}