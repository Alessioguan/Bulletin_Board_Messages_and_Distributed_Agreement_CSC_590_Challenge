/*
 * Part of the solution for Final project Challenge 1, by Guan Wang, Dan Luo and Hind Djebien.

 * Create or read the config file, copy the content of config file
 * to the config struct and show on server interface
 */


#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "tokenize.h"
#include "tcp-utils.h"


// Error handling function declaration
void error_handling(const char* message);


void load_config(struct Config *config) {
    char config_command[129];   // buffer for commands
    config_command[128] = '\0';
    char* config_com_tok[129];  // buffer for the tokenized commands

    int confd = open(CONFIG, O_RDONLY);
    //If config file is not created, create the config file and initiate the default value
    if (confd < 0){
        perror("config");
        sleep(0.1);
        printf("config: cannot open the configuration file, we will now attempt to create one.\n");
        confd = open(CONFIG, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        if (confd < 0){
            perror("config");
            error_handling("cannot open or create the file, giving up ...\n");
        }
        write(confd, "THMAX 6\n", strlen("THMAX=6\n"));
        write(confd, "BBPORT 9000\n", strlen("BBPORT=9000\n"));
        write(confd, "SYNCPORT 10000\n", strlen("SYNCPORT=10000\n"));
        write(confd, "BBFILE bbfile\n", strlen("BBFILE=bbfile\n"));
        write(confd, "DAEMON true\n", strlen("DAEMON=true\n"));
        write(confd, "DEBUG false\n", strlen("DEBUG=false\n"));
        write(confd, "DELAY false\n", strlen("DELAY=false\n"));
        close(confd);
        confd = open(CONFIG, O_RDONLY);
        if (confd < 0) {
            perror(CONFIG);
            fprintf(stderr, "cannot open the file, giving up ...\n");
            return;
        }
    }

    bool set_T = false;
    bool set_bp = false;
    bool set_sp = false;
    bool set_bbfile = false;
    bool set_d = false;
    bool set_D = false;
    bool set_gamma = false;

    //Read each line of config file and record the value to config struct
    //If the value is wrong, record the default value to config struct
    while (1) {
        int n = readline(confd, config_command, 128);
        if (n == recv_nodata)
            break;
        if (n < 0) {
            snprintf(config_command, sizeof(config_command), "config: %s", strerror(errno));
            perror(config_command);
            break;
        }
        str_tokenize(config_command, config_com_tok, strlen(config_command));
        if (strcmp(config_com_tok[0], "THMAX") == 0) {
            if (atol(config_com_tok[1]) <= 0) {
                perror("THMAX value is not correct! Set the default value 6\n");
                config->T = 6;
            } else {
                config->T = atol(config_com_tok[1]);
            }
            printf("THMAX = %d\n", config->T);
            set_T = true;
        } else if (strcmp(config_com_tok[0], "BBPORT") == 0) {
            if (atol(config_com_tok[1]) <= 0) {
                perror("BBPORT value is not correct! Set the default value 9000\n");
                config->bp = 9000;
            } else {
                config->bp = atol(config_com_tok[1]);
            }
            printf("BBPORT = %d\n", config->bp);
            set_bp = true;
        } else if (strcmp(config_com_tok[0], "SYNCPORT") == 0) {
            if (atol(config_com_tok[1]) <= 0) {
                perror("SYNCPORT value is not correct! Set the default value 10000\n");
                config->sp = 10000;
            } else {
                config->sp = atol(config_com_tok[1]);
            }
            printf("SYNCPORT = %d\n", config->sp);
            set_sp = true;
        } else if (strcmp(config_com_tok[0], "BBFILE") == 0) {
            if (strlen(config_com_tok[1]) <= 0) {
                // If we didn't find the name of bbfile, server will not start
                error_handling("name of BBFILE is not correct! please give the bbfile name, server are fail to start\n");
            } else {
                strcpy(config->bbfile, config_com_tok[1]);
                printf("BB_FILE = %s\n", config->bbfile);
            }
            set_bbfile = true;
        } else if (strcmp(config_com_tok[0], "DAEMON") == 0) {
            if (strcmp(config_com_tok[1], "true") != 0 && strcmp(config_com_tok[1], "false") != 0) {
                perror("DAEMON value is not correct! Set the default value true\n");
                config->d = true;
            } else {
                config->d = str_to_bool(config_com_tok[1]);
            }
            printf("DAEMON = %d\n", config->d);
            set_d = true;
        } else if (strcmp(config_com_tok[0], "DEBUG") == 0) {
            if (strcmp(config_com_tok[1], "true") != 0 && strcmp(config_com_tok[1], "false") != 0) {
                perror("DEBUG value is not correct! Set the default value false\n");
                config->D = false;
            } else {
                config->D = str_to_bool(config_com_tok[1]);
            }
            printf("DEBUG = %d\n", config->D);
            set_D = true;
        } else if (strcmp(config_com_tok[0], "DELAY") == 0) {
            if (strcmp(config_com_tok[1], "true") != 0 && strcmp(config_com_tok[1], "false") != 0) {
                perror("DELAY value is not correct!  Set the default value false\n");
                config->gamma = false;
            } else {
                config->gamma = str_to_bool(config_com_tok[1]);
            }
            printf("DELAY = %d\n", config->gamma);
            set_gamma = true;
        }
        else if (strcmp(config_com_tok[0], "PEER") == 0) {
            char *peer_info = config_com_tok[1];
            char *colon_pos = strchr(peer_info, ':');
            if (colon_pos != NULL) {
                *colon_pos = '\0'; // Split host and port
                char *host = peer_info;
                int port = atoi(colon_pos + 1);
                //add host and port to config struct, but not validate now
                add_peer(config, host, port);
            } else {
                perror("PEER value is not correct!");
            }
        }
    }
    close(confd);

    // Missing line causing the respective variable to take a default value
    if(!set_T){
        printf("THMAX value is not define in config file! Set the default value 6\n");
        config->T = 6;
        printf("THMAX = %d\n", config->T);
    }
    if(!set_bp){
        printf("BBPORT value is not define in config file! Set the default value 9000\n");
        config->bp = 9000;
        printf("BBPORT = %d\n", config->bp);
    }
    if(!set_sp){
        printf("SYNCPORT value is not define in config file! Set the default value 10000\n");
        config->sp = 10000;
        printf("SYNCPORT = %d\n", config->sp);
    }
    if(!set_bbfile){
        //Missing bbfile information will cause server cannot start
        error_handling("BBFILE name is not define in config file!\n Please write 'BBFILE bbfile_name' on config file, server are fail to start\n");
    }
    if(!set_d){
        printf("DAEMON value is not define in config file! Set the default value true\n");
        config->d = true;
        printf("DAEMON = %d\n", config->d);
    }
    if(!set_D){
        printf("DEBUG value is not define in config file! Set the default value false\n");
        config->D = false;
        printf("DEBUG = %d\n", config->D);
    }
    if(!set_gamma){
        printf("DELAY value is not define in config file! Set the default value false\n");
        config->gamma = false;
        printf("DELAY = %d\n", config->gamma);
    }
}

//During reading the configuration file, we need to covert string to bool
bool str_to_bool(const char* str) {
    if(strcmp(str,"true") == 0) {
        return true;
    } else {
        return false;
    }
}

void add_peer(struct Config *config, const char *host, int port) {
    if (config->peer_count < MAX_PEERS) {
        //load peer information into struct config
        strcpy(config->peers[config->peer_count].host, host);
        config->peers[config->peer_count].port = port;
        printf("peer to connect %s:%d\n",config->peers[config->peer_count].host,config->peers[config->peer_count].port);
        config->peer_count++;
    } else {
        printf("Max peers reached, cannot add more.\n");
    }
}