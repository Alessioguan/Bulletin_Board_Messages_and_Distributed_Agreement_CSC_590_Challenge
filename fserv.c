/*
 * Part of the solution for Final project Challenge 1, by Guan Wang, Dan Luo and Hind Djebien.

 * handle every communication of master server to client and master server to client
 * For read, user, quit command, operation adn respond directly to client
 * For write/replace command, synchronize with slave servers if it needs then reply to client
 */


#include "fserv.h"


#define MAX_LEN 1024
#define MAXLINE 1024
#define BUF_SIZE 1024

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
int max_message_number;

void error_handling(const char *message);

bool is_valid_username(const char *name);

bool is_valid_text(const char *text);

int get_last_message_number(struct Config *config);

bool handle_distributed_commit(struct Config *config, char *poster, char *cmd, char *response);

void *handle_client(void *args) {



    struct ClientArgs *client_args = (struct ClientArgs *) args;
    int client_fd = client_args->client_fd;
    struct Config *config = client_args->config;
    ThreadPool *pool = client_args->pool;
//    free(client_args);

    if(config->D){
        printf("Current connected clients: %d\n", client_args->pool->connected_clients);
    }

    char cmd[MAXLINE], cmd_copy[MAXLINE], cmd_copy2[MAXLINE - 135];
    char *com_tok[128];
    size_t num_tok;
    int str_len;
    char greeting[MAX_LEN];
    char response[MAX_LEN];

    char bbfile_command[MAXLINE];   // buffer for commands
    bbfile_command[MAXLINE - 1] = '\0';
    char *bbfile_com_tok[MAXLINE];  // buffer for the tokenized commands
    char *new_message[MAXLINE];


    char poster[128];
    strcpy(poster, "nobody");

    //set timeout is 3 second
    int timeout = 3000;


    // Greeting message to client
    max_message_number = get_last_message_number(config);
    write(client_fd, "0.0 Welcome to the Bulletin Board Server\n",
          sizeof("0.0 Welcome to the Bulletin Board Server\n"));
    snprintf(greeting, sizeof(greeting), "0.0 Welcome to the Bulletin Board Server\n"
                                         "Input 'USER name' to set your user name, user name only accept alphabet letter and numbers\n"
                                         "Input 'READ messagenumber' to read the message, the message number are from 1 to %d\n"
                                         "Input 'WRITE message' to add new message\n"
                                         "Input 'REPLACE message-number/message' to replace the message\n"
                                         "Input 'QUIT' to exit\n", max_message_number);
    write(client_fd, greeting, sizeof(greeting));
    fflush(stdout);


    while (1) {
        //Important, every 3 second, check whether shutdown the thread pool in case of thread blocking
        str_len = recv_nonblock(client_fd, cmd, sizeof(cmd) - 1, timeout);

        if (str_len == -1) {
            perror("receive message failed!");
            continue;
        }

        //if
        if (str_len == recv_nodata) {
            if (pool->shutdown) {
                break;
            } else {
                continue;
            }//Avoid client block, end the loop and close the client socket if pool thread shutdown
        }

        //if connection break, end the loop and close the client socket
        if (str_len == 0) {
            printf("Client closed the connection.\n");
            break;
        }

        strcpy(cmd_copy2, cmd);

        //filter the character "\n" and "\r" of client command
        if (cmd[str_len - 1] == '\n')
            cmd[str_len - 1] = '\0';
        if (cmd[str_len - 2] == '\r')
            cmd[str_len - 2] = '\0';

        //read client command
        printf("Received from client: %s\n", cmd);
        strcpy(cmd_copy, cmd);


        //tokenize the command, see explaining on "tokenize.h"
        num_tok = str_tokenize(cmd, com_tok, strlen(cmd));

        //receive quit command, send message and close the server
        if (strcmp(com_tok[0], "QUIT") == 0) {
            send(client_fd, "4.0 BYE welcome to Bulletin Board Server again!!\n",
                 strlen("4.0 BYE welcome to Bulletin Board Server again!!\n"), 0);
            memset(cmd, 0, sizeof(cmd));
            sleep(1);
            break;
        }

        //Respond to client
        if (num_tok == 1) {
            snprintf(response, sizeof(response), "ERROR Unknown command\n");
        } else if (strcmp(com_tok[0], "USER") == 0) {
            if (num_tok != 2 || !is_valid_username(com_tok[1])) {
                snprintf(response, sizeof(response), "1.2 ERROR USER Invalid username\n");
            } else {
                //record the username to poster
                strcpy(poster, com_tok[1]);
                snprintf(response, sizeof(response), "1.0 HELLO %s Your user name is established\n", poster);
            }
        } else if (strcmp(com_tok[0], "READ") == 0) {
            if (atol(com_tok[1]) <= 0) {
                snprintf(response, sizeof(response), "2.1 UNKNOWN %s message number should be a positive integer!\n",
                         poster);//index must be a positive integer
            } else {
                pthread_mutex_lock(&file_mutex);
                int client_bbfile_fd = open(config->bbfile, O_RDONLY);
                if (client_bbfile_fd < 0) {
                    snprintf(response, sizeof(response), "2.2 ERROR READ cannot open the bulletin board file!\n");
                } else {

                    if (config->gamma) {
                        send(client_fd, "In delay mode, 3s to start read operation...\n",
                             sizeof("In delay mode, 3s to start read operation...\n"), 0);
                        sleep(3);  // delay the read operation
                    }
                    while (1) {
                        int n = readline(client_bbfile_fd, bbfile_command, MAXLINE - 1);
                        if (n == recv_nodata) {
                            snprintf(response, sizeof(response), "2.1 UNKNOWN %s couldn't find the message number\n",
                                     poster);
                            break;
                        }

                        if (n < 0) {
                            snprintf(response, sizeof(response),
                                     "2.2 ERROR READ cannot open the bulletin board file!\n");
                            break;
                        }
                        bbfile_com_tok[0] = strtok(bbfile_command, " ");
                        bbfile_com_tok[1] = strtok(NULL, "");
                        if (strcmp(bbfile_com_tok[0], com_tok[1]) == 0) {
                            snprintf(response, sizeof(response), "2.0 %s %s/%s\n", com_tok[1], poster,
                                     bbfile_com_tok[1]);

                            break;
                        }
                    }
                }
                close(client_bbfile_fd);
                pthread_mutex_unlock(&file_mutex);
            }
        } else if (strcmp(com_tok[0], "WRITE") == 0) {

            new_message[0] = strtok(cmd_copy, " ");
            new_message[1] = strtok(NULL, "\0");
            if (!is_valid_text(new_message[1])) {
                snprintf(response, sizeof(response), "3.1 ERROR WRITE Invalid characters in message!\n");
            } else {
                if (config->gamma && config->peer_count!=0) {
                    send(client_fd, "In delay mode, 6s to start slave server write operation...\n",
                         sizeof("In delay mode, 6s to start slave server write operation...\n"), 0);
                }
                //If it doesn't have peer servers, function return true
                //For inter server synchronization, return true if received precommit and commit signal from slave server
                if (handle_distributed_commit(config, poster, cmd_copy2, response)) {
                    max_message_number++;
                    char write_in[MAXLINE];
                    pthread_mutex_lock(&file_mutex);
                    int client_bbfile_fd = open(config->bbfile, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
                    if (client_bbfile_fd < 0) {
                        snprintf(response, sizeof(response), "3.1 ERROR cannot open the bulletin board file!\n");
                        for (int j = 0; j < config->peer_count; j++) {
                            send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                        }
                        if(config->D && config->peer_count !=0){
                            printf("UNSUCCESS cannot open the bulletin board file\n");
                        }
                    } else {
                        if (config->gamma) {
                            send(client_fd, "In delay mode, 6s to start write operation...\n",
                                 sizeof("In delay mode, 6s to start write operation...\n"), 0);
                            sleep(6);  // delay the write operation
                        }
                        if (flock(client_bbfile_fd, LOCK_EX) < 0) {// lock file before writing
                            snprintf(response, sizeof(response), "3.1 ERROR cannot lock the bulletin board file!\n");
                            for (int j = 0; j < config->peer_count; j++) {
                                send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                            }
                            if(config->D && config->peer_count !=0){
                                printf("UNSUCCESS cannot lock the bulletin board file\n");
                            }
                        } else {
                            snprintf(write_in, sizeof(write_in), "%d %s:%s\n", max_message_number, poster,
                                     new_message[1]);
                            ssize_t bytes_written = write(client_bbfile_fd, write_in, strlen(write_in));//writing in last line
                            if (bytes_written < 0) {
                                snprintf(response, sizeof(response),
                                         "3.1 ERROR cannot write in the bulletin board file!\n");
                                for (int j = 0; j < config->peer_count; j++) {
                                    send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                                }
                                if(config->D && config->peer_count !=0){
                                    printf("UNSUCCESS cannot write in the bulletin board file\n");
                                }
                            } else {
                                snprintf(response, sizeof(response), "3.0 WROTE %d\n", max_message_number);
                                for (int j = 0; j < config->peer_count; j++) {
                                    send(config->peers[j].fd, "SUCCESS\n", strlen("SUCCESS\n"), 0);
                                }
                                if(config->D && config->peer_count !=0){
                                    printf("sent SUCCESS\n");
                                }
                            }
                        }
                        fsync(client_bbfile_fd);// Flush the buffer to disk
                        flock(client_bbfile_fd, LOCK_UN);// unlock the file after writing

                    }

                    close(client_bbfile_fd);
                    pthread_mutex_unlock(&file_mutex);
                }
            }


        } else if (strcmp(com_tok[0], "REPLACE") == 0) {
            new_message[0] = strtok(cmd_copy, " ");
            new_message[1] = strtok(NULL, "\0");
            char *slash_com_tok[2] = {NULL, NULL}; // initialize to NULL
            //find two token divided by slash '/'
            int num_slash_com_tok = str_tokenize_slash(new_message[1], slash_com_tok, strlen(new_message[1]));

            if (num_slash_com_tok == -1) {
                snprintf(response, sizeof(response), "3.1 UNKNOWN %s please use punctuation slash'/'\n",
                         slash_com_tok[0]);
            } else if (atol(slash_com_tok[0]) <= 0) {
                snprintf(response, sizeof(response), "3.1 UNKNOWN %s message number should be a positve interger\n",
                         slash_com_tok[0]);
            } else if (!is_valid_text(slash_com_tok[1])) {
                snprintf(response, sizeof(response), "3.1 ERROR WRITE Invalid characters in message!\n");
            } else {
                if (config->gamma && config->peer_count != 0) {
                    send(client_fd, "In delay mode, 6s to start slave server replace operation...\n",
                         sizeof("In delay mode, 6s to start slave server replace operation...\n"), 0);
                }
                //If it doesn't have peer servers, function return true
                //For inter server synchronization, return true if received precommit and commit signal from slave server
                if (handle_distributed_commit(config, poster, cmd_copy2, response)) {
                    pthread_mutex_lock(&file_mutex);
                    int client_bbfile_fd = open(config->bbfile, O_RDWR, S_IRUSR | S_IWUSR);
                    if (client_bbfile_fd < 0) {
                        snprintf(response, sizeof(response), "3.2 ERROR WRITE cannot open the bulletin board file!\n");
                        for (int j = 0; j < config->peer_count; j++) {
                            send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                        }
                        if(config->D && config->peer_count !=0){
                            printf("UNSUCCESS cannot open the bulletin board file\n");
                        }
                    } else {
                        if (config->gamma) {
                            send(client_fd, "In delay mode, 6s to start replace operation...\n",
                                 sizeof("In delay mode, 6s to start replace operation...\n"), 0);
                            sleep(6);  // delay the replace operation
                        }

                        // Create a buffer to store all the lines of the file
                        char file_content[MAXLINE * 300]; // Assume there are at most 300 lines, and each line is at most MAXLINE
                        file_content[0] = '\0';
                        bool could_replaced = false;
                        while (1) {
                            int n = readline(client_bbfile_fd, bbfile_command, MAXLINE - 1);//read each line
                            if (n == recv_nodata) {
                                if (!could_replaced) {
                                    //Condition that didn't find the index
                                    snprintf(response, sizeof(response),
                                             "3.1 UNKNOWN %ld couldn't find the message number\n",
                                             atol(slash_com_tok[0]));
                                    for (int j = 0; j < config->peer_count; j++) {
                                        send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                                    }
                                    if(config->D && config->peer_count !=0){
                                        printf("UNSUCCESS couldn't find the message number\n");
                                    }
                                }
                                break;
                            }

                            if (n < 0) {
                                snprintf(response, sizeof(response),
                                         "3.1 ERROR cannot open the bulletin board file!\n");
                                for (int j = 0; j < config->peer_count; j++) {
                                    send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                                }
                                if(config->D && config->peer_count !=0){
                                    printf("UNSUCCESS cannot open the bulletin board file\n");
                                }
                                break;
                            }
                            char bbfile_command_copy[MAXLINE];
                            strcpy(bbfile_command_copy, bbfile_command);
                            bbfile_com_tok[0] = strtok(bbfile_command, " ");
                            bbfile_com_tok[1] = strtok(NULL, "");
                            char write_in[MAXLINE];
                            //if finding the index, delete the content and write in new one
                            if (strcmp(bbfile_com_tok[0], slash_com_tok[0]) == 0) {
                                snprintf(write_in, sizeof(write_in), "%ld %s:%s\n", atol(slash_com_tok[0]), poster,
                                         slash_com_tok[1]);
                                strcat(file_content,
                                       write_in); // replace the line and write in temporary string file_content
                                could_replaced = true;//complete replace
                            } else {
                                strcat(file_content,
                                       bbfile_command_copy); // keep other line and write in temporary string file_content
                                strcat(file_content, "\n"); // add '\n'
                            }
                        }
                        fsync(client_bbfile_fd);
                        close(client_bbfile_fd);

                        //If finding the index to replace, write the contents of the buffer to file
                        if (could_replaced) {
                            // Reopen the file for writing
                            int write_fd = open(config->bbfile, O_WRONLY | O_TRUNC);
                            if (write_fd < 0) {
                                snprintf(response, sizeof(response),
                                         "3.1 ERROR cannot open the bulletin board file for writing!\n");
                                for (int j = 0; j < config->peer_count; j++) {
                                    send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                                }
                                if(config->D && config->peer_count !=0){
                                    printf("UNSUCCESS cannot open the bulletin board file for writing\n");
                                }
                            } else {
                                if (flock(write_fd, LOCK_EX) < 0) {// lock file during writing
                                    snprintf(response, sizeof(response),
                                             "3.1 ERROR cannot lock the bulletin board file!\n");
                                    for (int j = 0; j < config->peer_count; j++) {
                                        send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                                    }
                                    if(config->D && config->peer_count !=0){
                                        printf("UNSUCCESS cannot lock the bulletin board file\n");
                                    }
                                } else {
                                    // Write the contents of the buffer to a file
                                    ssize_t bytes_written = write(write_fd, file_content, strlen(file_content));
                                    if (bytes_written < 0) {
                                        snprintf(response, sizeof(response),
                                                 "3.1 ERROR cannot write in the bulletin board file!\n");
                                        for (int j = 0; j < config->peer_count; j++) {
                                            send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                                        }
                                        if(config->D && config->peer_count !=0){
                                            printf("UNSUCCESS cannot write in the bulletin board file\n");
                                        }
                                    } else {
                                        snprintf(response, sizeof(response), "3.0 WROTE %ld\n", atol(slash_com_tok[0]));
                                        for (int j = 0; j < config->peer_count; j++) {
                                            send(config->peers[j].fd, "SUCCESS\n", strlen("SUCCESS\n"), 0);
                                        }
                                        if(config->D && config->peer_count !=0){
                                            printf("sent SUCCESS\n");
                                        }
                                    }
                                }
                                fsync(write_fd);
                                flock(write_fd, LOCK_UN);
                            }
                            close(write_fd);
                        }


                    }
                    pthread_mutex_unlock(&file_mutex);
                }
            }

        } else {
            snprintf(response, sizeof(response), "ERROR Unknown command\n");//For wrong form of command
        }

        send(client_fd, response, strlen(response), 0);

        memset(cmd, 0, sizeof(cmd));
        memset(cmd_copy, 0, sizeof(cmd_copy));
        memset(cmd_copy2, 0, sizeof(cmd_copy2));
        memset(response, 0, sizeof(response));
        memset(bbfile_command,0,sizeof(bbfile_command));
        memset(bbfile_com_tok,0,sizeof(bbfile_com_tok));
    }

    close(client_fd);
    return NULL;
}

//Check if it contains unacceptable characters in username
bool is_valid_username(const char *name) {
    for (size_t i = 0; i < strlen(name); i++) {
        if (!isalnum(name[i])) {
            return false;
        }
    }
    return true;
}

// identify whether it include non-printable character
bool is_valid_text(const char *text) {
    for (size_t i = 0; i < strlen(text); i++) {
        if (!isprint(text[i])) {
            return false;
        }
    }
    return true;
}

//get last index of bbfile
int get_last_message_number(struct Config *config) {
    int bbfile_fd = open(config->bbfile, O_RDONLY);
    if (bbfile_fd < 0) {
        perror("bbfile");
        error_handling("cannot open the bbfile, giving up ...\n");
    }

    int last_number = 0;
    char line[MAX_LEN];
    char *com_line_tok[MAX_LEN];

// read each line to get the last line index
    while (1) {
        int n = readline(bbfile_fd, line, MAXLINE - 1);
        if (n == recv_nodata) {
            break;
        }

        if (n < 0) {
            perror("cannot read bbfile");
            break;
        }

        str_tokenize(line, com_line_tok, MAX_LEN - 1);
        last_number = atol(com_line_tok[0]);
    }

    close(bbfile_fd);
    return last_number;
}

//If it doesn't have peer servers, function return true
//For inter server synchronization, return true if received precommit and commit signal from slave server
bool handle_distributed_commit(struct Config *config, char *poster, char *cmd, char *response) {
    bool precommit_success = true;
    bool commit_success = true;
    if (config->peer_count == 0) {
        if (config->D) {
            printf("Is is the only one server, no peer server to connect\n");
        }
        return true;
    }
    //send PRECOMMIT
    for (int i = 0; i < config->peer_count; i++) {
        int send_check = send(config->peers[i].fd, "PRECOMMIT\n", strlen("PRECOMMIT\n"), 0);
        if (send_check == -1) {
            perror("send PRECOMMIT\n");
        } else {
            printf("sent PRECOMMIT to fd %d, port %d\n", config->peers[i].fd, config->peers[i].port);
        }
    }

    for (int i = 0; i < config->peer_count; i++) {
        char inter_server_respond[MAXLINE];
        int recv_num = recv_nonblock(config->peers[i].fd, inter_server_respond, sizeof(inter_server_respond), 10000);
        if (inter_server_respond[recv_num - 1] == '\n')
            inter_server_respond[recv_num - 1] = '\0';
        if (inter_server_respond[recv_num - 1] == '\r')
            inter_server_respond[recv_num - 1] = '\0';
        if (inter_server_respond[recv_num - 2] == '\n')
            inter_server_respond[recv_num - 2] = '\0';
        if (inter_server_respond[recv_num - 2] == '\r')
            inter_server_respond[recv_num - 2] = '\0';
        if (recv_num == recv_nodata) {
            printf("time out to receive precommit SUCCESS signal, from %s:%d\n", config->peers[i].host,
                   config->peers[i].port);// if not received message in 10 second, precommit fail, send ABORT
            precommit_success = false;
            for (int j = 0; j < config->peer_count; j++) {
                send(config->peers[j].fd, "ABORT\n", strlen("ABORT\n"), 0);
            }
            snprintf(response, MAXLINE, "3.2 ERROR WRITE fail to connect to slave server\n");
        } else if (strcmp(inter_server_respond, "SUCCESS") == 0) {
            printf("received SUCCESS for precommit, from %s:%d\n",config->peers[i].host,
                   config->peers[i].port);//received SUCCESS
        } else {
            // if not received message SUCCESS, precommit fail, send ABORT
            precommit_success = false;
            for (int j = 0; j < config->peer_count; j++) {
                send(config->peers[j].fd, "ABORT\n", strlen("ABORT\n"), 0);
            }
            snprintf(response, MAXLINE, "3.2 ERROR WRITE slave server is not prepared\n");
        }
        memset(inter_server_respond, 0, sizeof(inter_server_respond));
    }

    //If received SUCCESS from all slave server, precommit success!
    if (precommit_success) {
        printf("precommit success\n");
        for (int i = 0; i < config->peer_count; i++) {
            char commit_cmd[MAXLINE];
            //send commit message
            snprintf(commit_cmd, sizeof(commit_cmd), "COMMIT %s %s", poster, cmd);
            int send_check_commit = send(config->peers[i].fd, commit_cmd, strlen(commit_cmd), 0);
            fflush(stdout);
            if (send_check_commit == -1) {
                perror("send COMMIT\n");
            } else {
                printf("sent %s to fd %d, port %d\n", commit_cmd, config->peers[i].fd, config->peers[i].port);
            }
            memset(commit_cmd, 0, sizeof(commit_cmd));
        }

        int wait_time;
        if(config->gamma)
            wait_time = 16000;//16 s for delay mode, because we need to give 6s of slave server writing
        else
            wait_time = 10000;//10 s for receive all message

        //logic of receiving commit is same as receiving precommit
        for (int i = 0; i < config->peer_count; i++) {
            char inter_server_respond2[MAXLINE - 1];
            int recv_num2 = recv_nonblock(config->peers[i].fd, inter_server_respond2, sizeof(inter_server_respond2),
                                          wait_time);
            if (inter_server_respond2[recv_num2 - 1] == '\n')
                inter_server_respond2[recv_num2 - 1] = '\0';
            if (inter_server_respond2[recv_num2 - 1] == '\r')
                inter_server_respond2[recv_num2 - 1] = '\0';
            if (inter_server_respond2[recv_num2 - 2] == '\n')
                inter_server_respond2[recv_num2 - 2] = '\0';
            if (inter_server_respond2[recv_num2 - 2] == '\r')
                inter_server_respond2[recv_num2 - 2] = '\0';
            if (recv_num2 == recv_nodata) {
                if (commit_success) {
                    printf("time out to receive commit SUCCESS signal, from %s:%d\n", config->peers[i].host,
                           config->peers[i].port);
                    commit_success = false;
                    snprintf(response, MAXLINE, "3.2 ERROR WRITE fail to connect to slave server\n");
                }
            } else if (strcmp(inter_server_respond2, "SUCCESS") == 0) {
                printf("received SUCCESS for commit\n");
            } else {
                if (commit_success) {
                    printf("received UNSUCCESS signal, from %s:%d\n", config->peers[i].host, config->peers[i].port);
                    commit_success = false;
                    snprintf(response, MAXLINE, "%s\n", inter_server_respond2);
                }
            }
            memset(inter_server_respond2, 0, sizeof(inter_server_respond2));
        }

        //If not received SUCCESS from all slave server, send UNSUCCESS
        if (!commit_success) {
            for (int j = 0; j < config->peer_count; j++) {
                send(config->peers[j].fd, "UNSUCCESS\n", strlen("UNSUCCESS\n"), 0);
                printf("send UNSUCCESS");
            }
        }
    }

    if (precommit_success == false) {
        commit_success = false;
    }

    return commit_success;
}

