#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h> // for bool, true, false
#include "tcp-utils.h"

#define MAX_PEERS 1024
#define CONFIG "config"

// Define the struct of Peer, including host(host name or ip address), port number and the socket to connect to peer server
typedef struct {
    char host[256];
    int port;
    int fd;
} Peer;

// Define the struct of configuration values
struct Config {
    int T;
    int bp;
    int sp;
    char bbfile[129];
    Peer peers[MAX_PEERS];
    int peer_count;
    bool d;
    bool D;
    bool gamma;
};

// related function
void load_config(struct Config *config);
//convert string to bool
bool str_to_bool(const char* str);
//Used to add peers to the peers array, ensuring that the maximum number MAX_PEERS is not exceeded.
void add_peer(struct Config *config, const char *host, int port);

#endif // CONFIG_H
