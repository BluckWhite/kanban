#ifndef COMMON_H
#define COMMON_H


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

int checkInput(char*, int);
int setupSocket(struct sockaddr_in*, int, int);

#endif