#ifndef FSERV_H
#define FSERV_H

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <sys/file.h>


#include "config.h"
#include "threadpool.h"
#include "tokenize.h"
#include "tcp-utils.h"
#include "config.h"

struct ClientArgs {
    int client_fd;
    struct Config *config;
    ThreadPool *pool;
};

extern int max_message_number;
extern pthread_mutex_t file_mutex;

void *handle_client(void *args);
bool is_valid_username(const char *name);
bool is_valid_text(const char *text);
bool handle_distributed_commit(struct Config *config, char *poster, char *cmd, char *response);

////get the last message number
int get_last_message_number(struct Config *config);

#endif // FSERV_H
