/*
 * Part of the solution for Final projectChallenge 1, by Guan Wang, Dan Luo and Hind Djebien.
 *
 * This file implements the main server logic for a distributed
 * bulletin board system. It built two server, one for client managed by threadpool,
 * and the other one for inter server with multi thread.
 * some important function including signal handing, replica server handing(for accept to peer server),
 * logic of inter server respond, all peer server connection, and daemonize
 */



#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>


#include "config.h"
#include "threadpool.h"
#include "tcp-utils.h"
#include "fserv.h"
#include "tokenize.h"

#define MAX_LEN 1024
#define MAXLINE 1024


//Main

//When other peer server connect to our replica server, record the information
typedef struct {
    char ip[16];
    int port;
    int fd;
} Client_t;

//Global variables

ThreadPool pool; // Thread pool variables
int server_client_fd = -1; // client server socket, default -1
int inter_server_fd = -1; // inter server socket, default -1


//Initialize the value of config
struct Config config;

struct sockaddr_in servaddr;

pthread_mutex_t restart_mutex = PTHREAD_MUTEX_INITIALIZER;

//In case receive multi SIGHUP signal
bool restart_in_progress = false;

//error handling function
void error_handling(const char *message);

//If bbfile not exist, create bbfile
void create_bbfile_if_not_exists();

//setup server
void setup_server_socket(int port, int *fd);

//handle signal
void handle_signal(int signum);

//Check whether daemonize
void whether_daemonize();

//server in daemonize
void daemonize();

//thread of replica server management
void *replica_server();

//algorithm of inter server communication on replica server side(slave server side)
void *replica_client(void *args);

//Connect to other peers
int handle_peer_communication(Peer *peer);

//close all peers socket
void close_peer_connections();

//change ip address from integer to dot form
void ip_to_dotted(unsigned int ip, char *buffer);

int main(int argc, char *argv[]) {
    // Check the number of command line arguments
    if (argc > 1) {
        // If the command line contains more than one argument then the server
        // must refuse to start
        fprintf(stderr, "Error: Too many arguments provided. The server does not accept command line arguments.\n"
                        "Please edit your parameter in config file, thank you!\n");
        exit(EXIT_FAILURE);
    }
    (void)argv;

    int client_fd;


    struct sockaddr_in clntaddr;
    socklen_t clntaddr_sz;

    //create the configuration file if it not exists
    //If it exists, load the configuration value from config file
    load_config(&config);

    //create the bbfile if it not exists
    create_bbfile_if_not_exists();


    //Check whether daemonize, daemonize the process if it needs
    whether_daemonize();

    //Setup client server socket
    setup_server_socket(config.bp, &server_client_fd);

    //If there have peers in config file, setup inter server and connect to other peers
    if (config.peer_count > 0) {
        //Setup inter server socket
        setup_server_socket(config.sp, &inter_server_fd);

        //Detach a thread to manage replica server.
        // replica server accept and communicate to other peers as slave server side
        // replica server receive the message from master server and do operation
        pthread_t peer_thread;
        if (pthread_create(&peer_thread, NULL, replica_server, NULL) != 0) {
            perror("pthread_create for peer communication");
        }
        pthread_detach(peer_thread);

        //Wait 10s to ensure all replica servers are built and ready to be connected
        //Which means we give 10 second to start all servers to connect each other
        printf("waiting 10s to build all replica server\n");
        sleep(10);


        // Create a peer-to-peer server communication thread
        for (int i = 0; i < config.peer_count; i++) {
            if (handle_peer_communication(&config.peers[i]) != 0) {
                perror("pthread_create for peer communication");
            }
        }
        // Give other 10 seconds to connect to other peers
        printf("waiting 10s to connect to all other replica server\n");
        sleep(10);

        // After that, all inter server should be connected to each others
    }




    // Initiate the threadpool
    init_threadpool(&pool, config.T);

    // handle SIGTERM, SIGINT, SIGQUIT and SIGHUP signals

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGQUIT, handle_signal);
    signal(SIGHUP, handle_signal);

    // After building the client server and replica server(if there have peers)
    // give a notice to remind client to connect
    // All signal will also work after this notice
    printf("Ready to connect to client\n");
    fflush(stdout);
    while (1) {
        if (server_client_fd == -1) {
            perror("server_client_fd is invalid\n");
        }
        client_fd = accept(server_client_fd, (struct sockaddr *) &clntaddr, &clntaddr_sz);
        if (client_fd == -1) {
            perror("client accept() error\n");
            if (config.D) {
                fprintf(stderr, "Debug:errno: %d, error: %s\n", errno, strerror(errno)); // Print detailed error information
            }

        } else {
            //create new thread to communicate with client
            Task task;
            task.client_fd = client_fd;
            task.config = &config;
            add_task(&pool, task);

        }
    }
    printf("Server shutting down...\n");
    destroy_threadpool(&pool); // destroy thread pool

    close_peer_connections();

    close(server_client_fd); // close client server socket
    close(inter_server_fd); // close inter server socket




    return 0;
}

//If bbfile not exist, create bbfile
void create_bbfile_if_not_exists() {
    int bbfile_fd = open(config.bbfile, O_RDONLY);
    if (bbfile_fd < 0) {
        bbfile_fd = open(config.bbfile, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        if (bbfile_fd < 0) {
            perror("bbfile");
            error_handling("cannot open or create the bbfile, giving up ...\n");
        }
        write(bbfile_fd, "This is Bulletin Board file\n"
                         "1 alice:Welcome to the bulletin board system!\n"
                         "2 bob:Hi Alice, glad to be here.\n"
                         "3 charlie:Hello everyone, this is Charlie.\n"
                         "4 dave:Does anyone know how to configure the server settings?\n"
                         "5 alice:@dave Yes, you can refer to the server manual in the documentation.\n"
                         "6 bob:Anyone interested in a coding challenge this weekend?\n"
                         "7 charlie:@bob I'm in! What are the details?\n"
                         "8 dave:@bob Count me in as well.\n"
                         "9 alice:@bob Sounds fun! Let’s do it.\n"
                         "10 eve:Hi all, I’m new here. Nice to meet you!\n", strlen("This is Bulletin Board file\n"
                                                                                    "1 alice:Welcome to the bulletin board system!\n"
                                                                                    "2 bob:Hi Alice, glad to be here.\n"
                                                                                    "3 charlie:Hello everyone, this is Charlie.\n"
                                                                                    "4 dave:Does anyone know how to configure the server settings?\n"
                                                                                    "5 alice:@dave Yes, you can refer to the server manual in the documentation.\n"
                                                                                    "6 bob:Anyone interested in a coding challenge this weekend?\n"
                                                                                    "7 charlie:@bob I'm in! What are the details?\n"
                                                                                    "8 dave:@bob Count me in as well.\n"
                                                                                    "9 alice:@bob Sounds fun! Let’s do it.\n"
                                                                                    "10 eve:Hi all, I’m new here. Nice to meet you!\n"));
    }
    close(bbfile_fd);

    max_message_number = get_last_message_number(&config);
}

//setup server
void setup_server_socket(int port, int *fd) {

    int *server_fd = fd;

    *server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_fd == -1)
        error_handling("socket() error");

    if(config.D){
        //In debug mode, print server socket fd
        printf("Debug:fd is : %d\n", *server_fd);
    }

    // Set the SO_REUSEADDR option
    int opt = 1;
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        error_handling("setsockopt() error");
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind(*server_fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        //In debug mode, print the detail of bind error
        if (config.D) {
            perror("Debug:bind\n");
        }
        error_handling("bind() error");
    }


    if (listen(*server_fd, 128) == -1){
        //In debug mode, print the detail of accept error
        if (config.D) {
            perror("Debug:listen\n");
        }
        error_handling("listen() error");
    }

    //Show the host address and port number of Client server or Internal server
    if (port == config.bp) {
        printf("Client server is listening on host %s\n", inet_ntoa(servaddr.sin_addr));
        printf("Client server is listening on port %d\n", port);
    } else {
        printf("Internal server is listening on host %s\n", inet_ntoa(servaddr.sin_addr));
        printf("Internal server is listening on port %d\n", port);
    }

}

//error handling function
void error_handling(const char *message) {
    //print customized message
    fputs(message, stderr);
    fputc('\n', stderr);
    //terminate the process
    exit(1);
}

//handle signal
void handle_signal(int signum) {
    printf("Received signal %d, shutting down...\n", signum);
    destroy_threadpool(&pool); // destroy thread pool

    close_peer_connections();

    if (server_client_fd != -1) {
        if(config.D){
            printf("Debug:Closing server_client_fd: %d\n", server_client_fd);
        }

        shutdown(inter_server_fd, SHUT_RDWR); // Shutdown all read and write operations
        close(server_client_fd); // Closing the server socket
        server_client_fd = -1;
    }

    if (inter_server_fd != -1) {
        if(config.gamma){
            printf("Debug:Closing inter_server_fd: %d\n", inter_server_fd);
        }

        shutdown(inter_server_fd, SHUT_RDWR); // Shutdown all read and write operations
        close(inter_server_fd); // Closing the server socket
        inter_server_fd = -1;
    }


    if (signum == SIGHUP) {
        printf("Reloading configuration and restarting server...\n");
        load_config(&config);
        create_bbfile_if_not_exists();
        whether_daemonize();
        setup_server_socket(config.bp, &server_client_fd);

        if (config.peer_count > 0) {
            setup_server_socket(config.sp, &inter_server_fd);

            pthread_t peer_thread;
            if (pthread_create(&peer_thread, NULL, replica_server, NULL) != 0) {
                perror("pthread_create for peer communication");
            }
            pthread_detach(peer_thread);


            printf("waiting 10s to build all replica server\n");
            sleep(10);


            for (int i = 0; i < config.peer_count; i++) {
                if (handle_peer_communication(&config.peers[i]) != 0) {
                    perror("pthread_create for peer communication");
                }
            }
            printf("waiting 10s to connect to all other replica server\n");
            sleep(10);
        }


        init_threadpool(&pool, config.T);
        printf("Ready to connect to client\n");

        pthread_mutex_lock(&restart_mutex);
        restart_in_progress = false;// Shielding SIGHUP signal after completely restart
        pthread_mutex_unlock(&restart_mutex);
    } else {
        sync();  // Make sure all data is written to disk
        printf("Exiting server...\n");
        exit(0); // Terminate the process
    }
}

//Check whether daemonize
void whether_daemonize() {
//if config d is true, create the daemon process
    if (config.d) {
        daemonize();
    }
}

//server in daemonize
void daemonize() {
    pid_t pid = -1;//create a process

    // 1. Set umask
    umask(0);

    // 2. Fork and exit parent process
    if ((pid = fork()) < 0) {
        error_handling("fork() error");
    } else if (pid > 0) {
        exit(0); // Parent process exits
    }

    // 3. Create a new session
    setsid();

    // 4. Ignore SIGHUP signal
    signal(SIGHUP, SIG_IGN);

    // 5. Fork again and exit parent process
    if ((pid = fork()) < 0) {
        error_handling("fork() error");
    } else if (pid != 0) {
        exit(0); // Parent process exits
    }

    // 6. Close all file descriptors, including stdin, stdout, and stderr
    for (int i = getdtablesize(); i >= 0; --i) {
        close(i);
    }

    // 7. Redirect stdin, stdout, and stderr to /dev/null
    open("/dev/null", O_RDWR); // stdin
    dup(0); // stdout
    dup(0); // stderr

    // 8. Redirect log output to bbserv.log
    int log_fd = open("bbserv.log", O_WRONLY | O_CREAT | O_APPEND | O_TRUNC, S_IRUSR | S_IWUSR);
    if (log_fd < 0) {
        error_handling("cannot open or create bbserv.log");
    }
    dup2(log_fd, fileno(stdout));//redirect output to bbserv.log
    dup2(log_fd, fileno(stderr));//redirect error to bbserv.log
    close(log_fd);

    // 9. Write to PID file
    int pid_fd = open("bbserv.pid", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (pid_fd < 0) {
        error_handling("cannot open or create bbserv.pid");
    }
    char pid_str[10];
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    write(pid_fd, pid_str, strlen(pid_str));
    close(pid_fd);

}

//thread of replica server management
void *replica_server() {


    int ssock;                                       // slave sockets
    struct sockaddr_in client_addr;                  // the address of the client...
    socklen_t client_addr_len = sizeof(client_addr); // ... and its length
    // Setting up the thread creation:
    pthread_t r_tt;


    printf("about to replica server loop\n");
    fflush(stdout);
    while (1) {

        // Accept connection unless beyond the concurrency limit:
        ssock = accept(inter_server_fd, (struct sockaddr *) &client_addr, &client_addr_len);

        if (ssock < 0) {
            perror("replica server accept() error\n");
            // Check here whether errno is an error caused by the file descriptor being closed
            if (errno == EBADF || errno == EINTR || errno == EINVAL) {
                // Handle invalid file descriptors or interrupted system calls, end the thread
                printf("replica_server accept() interrupted, exiting...\n");
                break;
            }
            printf("fail to connect to %s:%d\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
            continue;

        }

        printf("received new server %s %d\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);

        // Dynamically allocate memory to the Client_t structure
        Client_t *clnt = (Client_t *) malloc(sizeof(Client_t));
        if (clnt == NULL) {
            // If memory allocation failures, cannot execute the function of slave server, back to accept stage
            perror("Failed to allocate memory");
            continue;
        }

        clnt->fd = ssock;
        ip_to_dotted(client_addr.sin_addr.s_addr, clnt->ip);
        clnt->port = client_addr.sin_port;


        if (pthread_create(&r_tt, NULL, (void *(*)(void *)) replica_client, (void *) clnt) != 0) {
            perror("pthread_create for peer communication");
            free(clnt);
            return 0;
        }
        pthread_detach(r_tt); // Detaching threads, no explicit pthread_join required


    }
    //if inter server socket is closed, end this thread
    pthread_exit(NULL);
    return 0; // will never reach this anyway...
}

//algorithm of inter server communication on replica server side(slave server side)
void *replica_client(void *args) {
    Client_t *clnt = (Client_t *) args;
    char int_server_cmd[MAXLINE];
    char int_server_cmd_cpy[MAXLINE];
    int str_len;

    char *inter_com_tok[128];

    char *new_message[MAXLINE];

    char response[MAXLINE];

    char final_respond[MAXLINE];   // buffer for commands
    char *bbfile_com_tok[MAXLINE];  // buffer for the tokenized commands
    while ((str_len = readline(clnt->fd, int_server_cmd, sizeof(int_server_cmd) - 1)) != recv_nodata) {

        if (str_len == -1) {
            perror("Receive message failed");
            break;
        }

        int wait_time;
        if(config.gamma)
            wait_time = 18000;//18 second in delay mode, 12 second for master server writing and 6s for delay
        else
            wait_time = 12000;//12 second for master server writing

        if (int_server_cmd[str_len - 1] == '\n')
            int_server_cmd[str_len - 1] = '\0';
        if (int_server_cmd[str_len - 2] == '\r')
            int_server_cmd[str_len - 2] = '\0';

        strcpy(int_server_cmd_cpy, int_server_cmd);

        //tokenize the command, see explaining on "tokenize.h"
        str_tokenize(int_server_cmd_cpy, inter_com_tok, strlen(int_server_cmd_cpy));


        //read client command
        printf("Received from internal server: %s\n", int_server_cmd);

        if (strcmp(int_server_cmd, "PRECOMMIT") == 0) {
            send(clnt->fd, "SUCCESS\n", strlen("SUCCESS\n"), 0);
            printf("sent SUCCESS for precommit\n");
        } else if (strcmp(int_server_cmd, "ABORT") == 0) {
            printf("received ABORT\n");
        } else if (strcmp(inter_com_tok[0], "COMMIT") == 0) {
            printf("received COMMIT from master server\n");
            if (strcmp(inter_com_tok[2], "WRITE") == 0) {
                new_message[0] = strtok(int_server_cmd, " ");
                new_message[1] = strtok(NULL, "\0");
                new_message[2] = strtok(new_message[1], " ");// This is poster
                new_message[3] = strtok(NULL, "\0");
                new_message[4] = strtok(new_message[3], " ");
                new_message[5] = strtok(NULL, "\0");// This is content to WRITE

                printf("message to write is %s\n", new_message[5]);
                if (new_message[5][strlen(new_message[5]) - 1] == '\n')
                    new_message[5][strlen(new_message[5]) - 1] = '\0';
                if (new_message[5][strlen(new_message[5]) - 1] == '\r')
                    new_message[5][strlen(new_message[5]) - 1] = '\0';
                if (new_message[5][strlen(new_message[5]) - 2] == '\r')
                    new_message[5][strlen(new_message[5]) - 2] = '\0';
                if (new_message[5][strlen(new_message[5]) - 2] == '\n')
                    new_message[5][strlen(new_message[5]) - 2] = '\0';
                printf("message to write is %s\n", new_message[5]);



                char file_to_copy[MAXLINE]; // copy 1024 bytes each time
                file_to_copy[0] = '\0'; // Make sure the buffer starts with an empty string
                if (!is_valid_text(new_message[5])) {
                    snprintf(response, sizeof(response), "3.2 ERROR WRITE Invalid characters in message!\n");
                    printf("situation %s\n", response);
                } else {
                    max_message_number++;
                    bool success_write = false;
                    bool copy_error = false;
                    char write_in[MAXLINE];
                    char temp_file_template[] = "temp_bbfile_XXXXXX";//template file name

                    //Extract content of bbfile, put them in a new backup file
                    pthread_mutex_lock(&file_mutex);
                    int client_bbfile_fd = open(config.bbfile, O_RDWR, S_IRUSR | S_IWUSR);
                    if (client_bbfile_fd < 0) {
                        snprintf(response, sizeof(response), "3.2 ERROR WRITE cannot open the bulletin board file!\n");
                        printf("situation %s\n", response);
                        copy_error = true;
                    } else {
                        if (flock(client_bbfile_fd, LOCK_SH) < 0) {
                            snprintf(response, sizeof(response),
                                     "3.2 ERROR cannot lock the bulletin board file for reading!\n");
                            printf("situation %s\n", response);
                            copy_error = true;
                        } else {

                            int backup_fd = mkstemp(temp_file_template);//give a random temp file name
                            if (backup_fd < 0) {
                                snprintf(response, sizeof(response),
                                         "3.2 ERROR WRITE cannot open the backup bulletin board file!\n");
                                printf("situation %s\n", response);
                                copy_error = true;
                            } else {
                                ssize_t bytes_read, bytes_written;
                                // Loop through the contents of config.bbfile and write to temp_fd
                                while ((bytes_read = read(client_bbfile_fd, file_to_copy, sizeof(file_to_copy))) > 0) {
                                    bytes_written = write(backup_fd, file_to_copy, bytes_read);
                                    if (bytes_written != bytes_read) {
                                        snprintf(response, sizeof(response),
                                                 "3.2 ERROR WRITE failed to write to the temporary bulletin board file!\n");
                                        printf("situation %s\n", response);
                                        copy_error = true;
                                        break;
                                    }
                                }
                            }
                            close(backup_fd);
                        }
                    }
                    fsync(client_bbfile_fd);// Flush the buffer to disk
                    // Unlock the file after reading.
                    flock(client_bbfile_fd, LOCK_UN);
                    close(client_bbfile_fd);

                    //If copy successed, execute the write operation
                    if (!copy_error) {
                        printf("copy successed!\n");
                        client_bbfile_fd = open(config.bbfile, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
                        if (client_bbfile_fd < 0) {
                            snprintf(response, sizeof(response),
                                     "3.2 ERROR cannot open the bulletin board file!\n");
                            printf("situation %s\n", response);
                        } else {
                            if (config.gamma) {
                                printf("In delay mode, 6s to start write operation...\n");
                                sleep(6);  // delay the write operation
                            }
                            if (flock(client_bbfile_fd, LOCK_EX) < 0) {// 锁定文件
                                snprintf(response, sizeof(response),
                                         "3.2 ERROR cannot lock the bulletin board file!\n");
                                printf("situation %s\n", response);
                            } else {
                                snprintf(write_in, sizeof(write_in), "%d %s:%s\n", max_message_number,
                                         new_message[2],//poster
                                         new_message[5]);//content to write
                                ssize_t bytes_written = write(client_bbfile_fd, write_in, strlen(write_in));
                                if (bytes_written < 0) {
                                    snprintf(response, sizeof(response),
                                             "3.2 ERROR cannot write in the bulletin board file!\n");
                                    printf("situation %s\n", response);
                                } else {
                                    snprintf(response, sizeof(response), "SUCCESS\n");
                                    printf("send SUCCESS for commit\n");
                                }

                            }
                            fsync(client_bbfile_fd);// Flush the buffer to disk
                            flock(client_bbfile_fd, LOCK_UN);// Flush the buffer to disk
                        }
                        close(client_bbfile_fd);
                    }

                    //send the SUCCESS or error message to master server
                    printf("ready to send\n");
                    send(clnt->fd, response, strlen(response), 0);
                    printf("sent the message %s\n", response);

                    int final_recv_num = recv_nonblock(clnt->fd, final_respond, sizeof(final_respond), wait_time);
                    if (final_respond[final_recv_num - 1] == '\n')
                        final_respond[final_recv_num - 1] = '\0';
                    if (final_respond[final_recv_num - 1] == '\r')
                        final_respond[final_recv_num - 1] = '\0';
                    if (final_respond[final_recv_num - 2] == '\n')
                        final_respond[final_recv_num - 2] = '\0';
                    if (final_respond[final_recv_num - 2] == '\r')
                        final_respond[final_recv_num - 2] = '\0';
                    if (final_recv_num == recv_nodata) {
                        printf("time out to receive commit SUCCESS signal\n");
                    }
                    //If timeout, redo the writing by rename temp file
                    if (final_recv_num == -1) {
                        printf("fail to receive\n");
                        if (rename(temp_file_template, config.bbfile) != 0) {
                            printf("redo writing unsuccessed, please find the 'backup_bbfile' to redo writing manually\n");
                        } else {
                            printf("redo writing successed\n");
                        }
                    }
                    else if (strcmp(final_respond, "SUCCESS") == 0) {
                        printf("WRITE SUCCESS\n");
                        unlink(temp_file_template);//delete temp file
                        success_write = true;
                    } else {
                        //If not received the SUCCESS, redo the writing
                        printf("received message: %s\n", final_respond);
                        printf("WRITE UNSUCCESS\n");
                        if (rename(temp_file_template, config.bbfile) != 0) {
                            perror("redo writing unsuccessed, please find the 'backup_bbfile' to redo writing manually\n");
                        } else {
                            printf("redo writing successed\n");
                        }
                    }

                    if (!success_write) {
                        max_message_number--;
                    }
                    pthread_mutex_unlock(&file_mutex);
                }
            } else if (strcmp(inter_com_tok[2], "REPLACE") == 0) {

                new_message[0] = strtok(int_server_cmd, " ");
                new_message[1] = strtok(NULL, "\0");
                new_message[2] = strtok(new_message[1], " ");// This is poster
                new_message[3] = strtok(NULL, "\0");
                new_message[4] = strtok(new_message[3], " ");// This is WRITE
                new_message[5] = strtok(NULL, "\0");

                printf("message to write is %s\n", new_message[5]);
                if (new_message[5][strlen(new_message[5]) - 1] == '\n')
                    new_message[5][strlen(new_message[5]) - 1] = '\0';
                if (new_message[5][strlen(new_message[5]) - 1] == '\r')
                    new_message[5][strlen(new_message[5]) - 1] = '\0';
                if (new_message[5][strlen(new_message[5]) - 2] == '\r')
                    new_message[5][strlen(new_message[5]) - 2] = '\0';
                if (new_message[5][strlen(new_message[5]) - 2] == '\n')
                    new_message[5][strlen(new_message[5]) - 2] = '\0';
                printf("message to write is %s\n", new_message[5]);
                char *slash_com_tok[2] = {NULL, NULL}; // initialize NULL
                str_tokenize_slash(new_message[5], slash_com_tok, strlen(new_message[1]));

                //copy bbfile to temp file for backup
                char file_to_copy[MAXLINE]; // copy 1024 bytes each time
                file_to_copy[0] = '\0'; // Make sure the buffer starts with an empty string
                if (!is_valid_text(slash_com_tok[1])) {
                    snprintf(response, sizeof(response), "3.2 ERROR WRITE Invalid characters in message!\n");
                    printf("situation %s\n", response);
                } else {
                    bool copy_error = false;
                    char temp_file_template[] = "temp_bbfile_XXXXXX";//template file name


                    pthread_mutex_lock(&file_mutex);
                    int client_bbfile_fd = open(config.bbfile, O_RDWR, S_IRUSR | S_IWUSR);
                    if (client_bbfile_fd < 0) {
                        snprintf(response, sizeof(response), "3.2 ERROR WRITE cannot open the bulletin board file!\n");
                        printf("situation %s\n", response);
                        copy_error = true;
                    } else {
                        if (flock(client_bbfile_fd, LOCK_SH) < 0) {
                            snprintf(response, sizeof(response),
                                     "3.2 ERROR cannot lock the bulletin board file for reading!\n");
                            printf("situation %s\n", response);
                            copy_error = true;
                        } else {

                            int backup_fd = mkstemp(temp_file_template);
                            if (backup_fd < 0) {
                                snprintf(response, sizeof(response),
                                         "3.2 ERROR WRITE cannot open the backup bulletin board file!\n");
                                printf("situation %s\n", response);
                                copy_error = true;
                            } else {
                                ssize_t bytes_read, bytes_written;
                                // Loop through the contents of config.bbfile and write to temp_fd
                                while ((bytes_read = read(client_bbfile_fd, file_to_copy, sizeof(file_to_copy))) > 0) {
                                    bytes_written = write(backup_fd, file_to_copy, bytes_read);
                                    if (bytes_written != bytes_read) {
                                        snprintf(response, sizeof(response),
                                                 "3.2 ERROR WRITE failed to write to the temporary bulletin board file!\n");
                                        printf("situation %s\n", response);
                                        copy_error = true;
                                        break;
                                    }
                                }
                            }
                            close(backup_fd);
                        }
                    }
                    fsync(client_bbfile_fd);// Flush the buffer to disk
                    // Unlock the file after reading.
                    flock(client_bbfile_fd, LOCK_UN);
                    close(client_bbfile_fd);


                    //Replace the line of index
                    if (!copy_error) {
                        printf("copy successed!\n");
                        client_bbfile_fd = open(config.bbfile, O_RDWR, S_IRUSR | S_IWUSR);
                        if (client_bbfile_fd < 0) {
                            snprintf(response, sizeof(response),
                                     "3.2 ERROR cannot open the bulletin board file!\n");
                            printf("situation %s\n", response);
                        } else {
                            if (config.gamma) {
                                printf("In delay mode, 6s to start replace operation...\n");
                                sleep(6);  // delay the replace operation
                            }

                            // Create a buffer to store all the lines of the file
                            char file_content[MAXLINE * 300]; // Assume there are at most 300 lines, and each line is at most MAXLINE
                            file_content[0] = '\0'; // Make sure the buffer starts with an empty string
                            bool could_replaced = false;
                            char bbfile_command[MAXLINE];   // buffer for commands
                            bbfile_command[MAXLINE - 1] = '\0';

                            while (1) {
                                int n = readline(client_bbfile_fd, bbfile_command, MAXLINE - 1);
                                if (n == recv_nodata) {
                                    if (!could_replaced){
                                        snprintf(response, sizeof(response),
                                                 "3.2 UNKNOWN %ld couldn't find the message number\n",
                                                 atol(slash_com_tok[0]));
                                        printf("situation %s\n", response);
                                    }

                                    break;
                                }

                                if (n < 0) {
                                    snprintf(response, sizeof(response),
                                             "3.2 ERROR cannot open the bulletin board file!\n");
                                    printf("situation %s\n", response);
                                    break;
                                }
                                char bbfile_command_copy[MAXLINE];
                                strcpy(bbfile_command_copy, bbfile_command);
                                bbfile_com_tok[0] = strtok(bbfile_command, " ");
                                bbfile_com_tok[1] = strtok(NULL, "");
                                char write_in_replace[MAXLINE];
                                if (strcmp(bbfile_com_tok[0], slash_com_tok[0]) == 0) {
                                    snprintf(write_in_replace, sizeof(write_in_replace), "%ld %s:%s\n",
                                             atol(slash_com_tok[0]), new_message[2],
                                             slash_com_tok[1]);
                                    strcat(file_content,
                                           write_in_replace); // replace the line and write in temporary string file_content
                                    could_replaced = true;
                                } else {
                                    strcat(file_content,
                                           bbfile_command_copy); // keep other line and write in temporary string file_content
                                    strcat(file_content, "\n");
                                }
                            }
                            fsync(client_bbfile_fd);// Flush the buffer to disk
                            close(client_bbfile_fd);

                            if (could_replaced) {
                                // Reopen the file for writing
                                int write_fd = open(config.bbfile, O_WRONLY | O_TRUNC);
                                if (write_fd < 0) {
                                    snprintf(response, sizeof(response),
                                             "3.2 ERROR cannot open the bulletin board file for writing!\n");
                                    printf("situation %s\n", response);
                                } else {
                                    if (flock(write_fd, LOCK_EX) < 0) {// 锁定文件
                                        snprintf(response, sizeof(response),
                                                 "3.2 ERROR cannot lock the bulletin board file!\n");
                                        printf("situation %s\n", response);
                                    } else {
                                        // Write the contents of the buffer to a file
                                        ssize_t bytes_written = write(write_fd, file_content, strlen(file_content));
                                        if (bytes_written < 0) {
                                            snprintf(response, sizeof(response),
                                                     "3.2 ERROR cannot write in the bulletin board file!\n");
                                            printf("situation %s\n", response);
                                        } else {
                                            snprintf(response, sizeof(response), "SUCCESS\n");
                                            printf("send SUCCESS for commit\n");
                                        }
                                    }
                                    fsync(write_fd);// Flush the buffer to disk
                                    flock(write_fd, LOCK_UN);// Flush the buffer to disk
                                }
                                close(write_fd);
                            }

                        }
                    }

                    //send the SUCCESS or error message
                    printf("ready to send\n");
                    send(clnt->fd, response, strlen(response), 0);
                    printf("sent the message %s\n", response);

                    int final_recv_num = recv_nonblock(clnt->fd, final_respond, sizeof(final_respond), wait_time);
                    if (final_respond[final_recv_num - 1] == '\n')
                        final_respond[final_recv_num - 1] = '\0';
                    if (final_respond[final_recv_num - 1] == '\r')
                        final_respond[final_recv_num - 1] = '\0';
                    if (final_respond[final_recv_num - 2] == '\n')
                        final_respond[final_recv_num - 2] = '\0';
                    if (final_respond[final_recv_num - 2] == '\r')
                        final_respond[final_recv_num - 2] = '\0';
                    if (final_recv_num == recv_nodata) {
                        printf("time out to receive commit SUCCESS signal\n");
                    }
                    //If timeout, redo the writing by rename temp file
                    if (final_recv_num == -1) {
                        printf("fail to receive\n");
                        if (rename(temp_file_template, config.bbfile) != 0) {
                            printf("redo writing unsuccessed, please find the 'backup_bbfile' to redo writing manually\n");
                        } else {
                            printf("redo writing successed\n");
                        }
                    }
                    else if (strcmp(final_respond, "SUCCESS") == 0) {
                        printf("WRITE SUCCESS\n");
                        unlink(temp_file_template);//delete temp file
                    } else {
                        //If didn't receive SUCCESS, redo the writing by rename temp file
                        printf("received message: %s\n", final_respond);
                        printf("WRITE UNSUCCESS\n");
                        if (rename(temp_file_template, config.bbfile) != 0) {
                            perror("redo writing unsuccessed, please find the 'backup_bbfile' to redo writing manually\n");
                        } else {
                            printf("redo writing successed\n");
                        }
                    }

                    pthread_mutex_unlock(&file_mutex);
                }
            }
        }


        memset(new_message, 0, sizeof(new_message));
        memset(int_server_cmd, 0, sizeof(int_server_cmd));
        memset(response, 0, sizeof(response));
    }

    printf("server %s:%d is disconnected\n", clnt->ip, clnt->port);

    close(clnt->fd);

    //when first disconnecting happened, lock the thread and give SIGHUP signal to restart server
    //since restart the server, one server disconnecting will cause every server restart
    //set the mutex in case receive multi SIGHUP during restart
    free(clnt);
    pthread_mutex_lock(&restart_mutex);
    if (!restart_in_progress) {
        restart_in_progress = true;
        pthread_mutex_unlock(&restart_mutex);

        kill(getpid(), SIGHUP);// if one server is disconnected, restart all the server
    } else {
        pthread_mutex_unlock(&restart_mutex);
    }

    return NULL;
}

//Connect to other peers
int handle_peer_communication(Peer *peer) {
    peer->fd = connectbyportint(peer->host, peer->port);

    if (peer->fd < 0) {
        perror("connectbyport");
        return -1;
    }

    //connect success
    printf("Connected to peer %s:%d, fd is %d\n", peer->host, peer->port, peer->fd);

    //If peer server received the message, connection test succeed
    char cmd[MAXLINE];
    snprintf(cmd, sizeof(cmd), "Can you hear me, %s:%d\nThis is %d\n", peer->host, peer->port, config.sp);
    int send_status = send(peer->fd, cmd, strlen(cmd), 0);

    if (send_status < 0) {
        perror("Failed to send message to peer\n");
    }


    return 0;
}

//close all peers socket
void close_peer_connections() {
    int i;
    for (i = 0; i < config.peer_count; i++) {
        if (config.peers[i].fd != -1) {
            close(config.peers[i].fd);
            config.peers[i].fd = -1;
        }
    }
    config.peer_count = 0;
}

//change ip address from integer to dot form
void ip_to_dotted(unsigned int ip, char *buffer) {
    struct in_addr ip_addr;
    ip_addr.s_addr = ip;
    inet_ntop(AF_INET, &ip_addr, buffer, INET_ADDRSTRLEN);
}
