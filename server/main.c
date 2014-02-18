/* Happy chat server
 * Domagoj Boroš 2014.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>
#include <malloc.h>
#include <errno.h>

/* The default port for the server to listen on */
#define DEFAULT_PORT 33333

/* Maximum connections allowed in queue */
#define MAX_CONNECTIONS 20

/* Maximum length of received and sent messages in bytes */
#define MAX_LEN 1024

/* Max length of clients name */
#define MAX_CLI_NAME_LEN 9

/* User commands */
#define CHANGE_NAME  "!USERINFO"
#define CHANGE_NAME2 "!NAME"  /* TBD */
#define WHOIS        "!WHOIS" /* TBD */

/* Server socket file descriptor */
int server_sock;

/* Sets that hold socket file descriptors */
fd_set buff_fds; /* Save all the sockets */
fd_set read_fds; /* Sockets that are ready for reading */
fd_set write_fds; /* Sockets that are ready for writing */

/* The maximum number of the socket file descriptors */
int nfd;

/* Msg sent by client */
char chat_msg[MAX_LEN];

/* Length of the chat msg */
ssize_t len;

/* Client information */
struct client_info {
    char name[MAX_CLI_NAME_LEN]; /* The clients name */
    int socket; /* The socket on which the client communicates, used for identifying */
};

/* List of all online clients */
struct clients {
    struct client_info *client;
    struct clients *next_client;
};

/* The clients */
struct clients *online_client_list = NULL;

/* Print error and exit */
void die(char *msg);
/* Print usage */
void pusage();
/* Catch termination signals, clean up memory and exit */
void *clex(int sig);
/* The chat service */
void chat();
/* Sends a message to all the clients */
void send_to_all(char *chat_msg);


/* Server port number is an optional argument */
int main(int argc, char **args)
{
    sigset(SIGINT, clex);
    sigset(SIGTERM, clex);
    /* Port is always 16 bit */
    uint16_t port = DEFAULT_PORT;

    if (argc == 2) {
        if ((strcmp("-h", args[1])) == 0)
            pusage;
        else
            port = atoi(args[1]);
    }

    printf("Starting on port %d\n", port);

    /* Setup server side socket
     * Works only with IPv4 */
    struct sockaddr_in server;

    server_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == -1)
        die("Failed to create server socket\n");

    /* Clear struct memory and fill with server information */
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        die("Failed to bind the server socket\n");

    if (listen(server_sock, MAX_CONNECTIONS) < 0)
        die("Failed to listen on server socket\n");

    printf("Listening for connections\n");

    /* Start the chat service */
    chat();

    return 0;
}

/* Print error and exit */
void die(char *msg)
{
    perror(msg);
    exit(1);
}

void pusage()
{
    printf("USAGE: happyserver\nor\nhappyserver <port>\nport defaults to %d\n",
            DEFAULT_PORT);
}

/* Catch termination signals, clean up memory and exit */
void *clex(int sig) {
    printf("Caugth termination signal '%d'\n", sig);
    close(server_sock);
    exit(1);
}

/* Creates a new client and adds his info to the list of online clients
 * Returns -1 on error */
int add_new_to_cli_list(int client_sock)
{
    printf("Creating new client with socket %d\n", client_sock);
    /* Create a new client(chat user) */
    struct client_info *new_client_info = malloc(sizeof(struct client_info));
    if (new_client_info == NULL) {
        perror("add_new_to_cli_list: Failed to allocate memory for the new client information\n");
        close(client_sock);
        return -1;
    }

    /* Get default username "User<socket>" */
    strcpy(new_client_info->name, "User");
    char clinum[5];
    sprintf(clinum, "%d", client_sock);
    strcat(new_client_info->name, clinum);
    new_client_info->socket = client_sock;

    /* If there were no clients online */
    if (online_client_list == NULL) {
        online_client_list = malloc(sizeof(struct clients));
        if (online_client_list == NULL) {
            perror("add_to_cli_list: Failed to allocate memory for online clients list\n");
            return -1;
        }

        online_client_list->client = new_client_info;
        online_client_list->next_client = NULL;

        printf("New online client info: Name: '%s', socket: '%d'\n",
               online_client_list->client->name, online_client_list->client->socket);

        return 0;
    }

    struct clients *new_client = malloc(sizeof(struct clients));
    if (new_client == NULL) {
        perror("add_to_cli_list: Failed to allocat memory for new client in list\n");
        return -1;
    }

    new_client->client = new_client_info;
    new_client->next_client = online_client_list;
    online_client_list = new_client;

    printf("New online client info: Name: '%s', socket: '%d'\n",
            online_client_list->client->name, online_client_list->client->socket);
}

/* Removes the client with socket client_sock from the online client list */
void delete_from_cli_list(int client_sock)
{
    struct clients *p;
    p = online_client_list;

    /* If it's the first element in the list */
    if (online_client_list->client->socket == client_sock) {
        online_client_list = online_client_list->next_client;
        free(p);
        return;
    }

    struct clients *pp;
    for (; p->next_client != NULL; p = p->next_client) {
        if (p->next_client->client->socket == client_sock) {
            pp = p->next_client;
            p->next_client = p->next_client->next_client;
            free(pp);
            return;
        }
    }
}

/* Accepts the new connection and adds it to the socket set.
 * Keeps track of the maximum file descriptor number.
 * Creates new client info and adds it to client list */
void accept_new()
{
    int client_sock;
    struct sockaddr_in client;

    int clientlen = sizeof(client);
    client_sock = accept(server_sock, (struct sockaddr *)&client, &clientlen);
    if (client_sock == -1) {
        perror("Failed to accept new connection\n");
        return;
    }

    char ip[INET_ADDRSTRLEN];
    printf("Got new connection from %s:%d\n",
            inet_ntop(AF_INET, &(client.sin_addr.s_addr), ip, INET_ADDRSTRLEN),
            ntohs(client.sin_port));

    if (add_new_to_cli_list(client_sock) == -1)
        return;

    /* Add it to the socket file descriptor lists */
    if ((nfd) < client_sock)
        (nfd) = client_sock;

    FD_SET(client_sock, &buff_fds);

    /* Tell everyone someone connected */
    char notf[MAX_LEN];
    sprintf(notf, "User%d connected", client_sock);
    send_to_all(notf);
}

/* Accepts new incoming message
 * TODO: convert message from network byte to host byte order*/
void accept_message(int socket)
{
    len = recv(socket, chat_msg, MAX_LEN, 0);

    if (len == -1) {
        fprintf(stderr, "Error receiving message from client: %s\n", strerror(errno));
        return;
    }
    if (len == 0)
        return;

    printf("Got this message: '%s', with lenghth %d\n", chat_msg, len);
}

/* Sends a chat message to all the clients
 * nfd - range of socket file descriptors
 * TODO: convert message from host byte to network byte order */
void send_to_all(char *chat_msg)
{
    int i;
    int n;
    int total = 0; /* Total bytes sent */

    for (i = 0; i <= nfd; i++) {
        if (FD_ISSET(i, &write_fds) && (i != server_sock)) { /* Don't send to the server, he doesn't want the messagei */
            total = 0;
            do {
                n = send(i, chat_msg+total, MAX_LEN-total, 0);
                if (n == -1) {
                    perror("Failed to send chat message to client\n");
                    break;
                }
                total += n;
            } while (total < MAX_LEN);
        }
    }
    printf("Done sending\n");
}

/* Set the client's name with the socket client_sock
 * Tell all users it happened */
void set_client_name(int client_sock, char *name)
{
    struct clients *clients = online_client_list;

    for(; clients != NULL; clients = clients->next_client) {
        if (clients->client->socket == client_sock) {
            char buff[MAX_CLI_NAME_LEN];
            strcpy(buff, clients->client->name);
            strncpy(clients->client->name, name, MAX_CLI_NAME_LEN);
            printf("Client %s changed name to %s\n",
                    buff, clients->client->name);
            /* Send the message to all the clients */
            char msg[MAX_LEN];
            sprintf(msg, "%s changed name to %s", buff, clients->client->name);
            send_to_all(msg);
            return;
        }
    }

    fprintf(stderr, "set_client_name: Failed to set client name: No such client with file descriptor %d\n", socket);
}

/* Decode the message and execute the messages
 * socket - where the message came from
 * msg - the message
 * len - length of message */
void handle_message(int socket)
{
    printf("Handling message from socket %d\n", socket);
    char notf[MAX_LEN];
    struct clients *clients = online_client_list;

    /* If the client closed the connection */
    if (len == 0) {
        printf("Client closed connection\n");

        close(socket);
        FD_CLR(socket, &buff_fds);
        FD_CLR(socket, &read_fds);
        FD_CLR(socket, &write_fds);

        for(; clients != NULL; clients = clients->next_client) {
            if (clients->client->socket == socket) {
                sprintf(notf, "%s has disconnected", clients->client->name);
                break;
            }

        delete_from_cli_list(socket);
        send_to_all(notf);

        }
        return;
    }

    /* Check if it's client info */
    if (memcmp(chat_msg, "!USERINFO", 9) == 0) {
        printf("Got client info\n");
        set_client_name(socket, chat_msg+10); /* 9 bytes code word + 1 byte white space */
        return;
    }

    /* Else it's a chat message; send to all clients
     * Add username to message */
    for(; clients != NULL; clients = clients->next_client)
        if (clients->client->socket == socket)
            break;

    char msg[MAX_LEN + MAX_CLI_NAME_LEN];
    msg[0] = 0;
    strcpy(msg, clients->client->name);
    strcat(msg, ": ");
    strcat(msg, chat_msg);
    send_to_all(msg);
}

void chat()
{
    nfd = server_sock; /* The maximum number of the socket file descriptors */
    printf("Server file descriptor %d\n", server_sock);

    FD_ZERO(&buff_fds);
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_SET(server_sock, &buff_fds); /* Add the server to the set */

    int i, j;

    /* Timeout time for select() */
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    /* Only terminable by termination signal's */
    while(1) {
        read_fds = buff_fds;
        write_fds = buff_fds;

        /* Delete the old message */
        memset(chat_msg, 0, MAX_LEN);

        if (select(nfd+1, &read_fds, &write_fds, NULL, &tv) == -1)
            perror("Failed while selecting file descriptor\n");

        for(i = 0; i <= nfd; i++) {
            if (FD_ISSET(i, &read_fds)) {/* If the socket has an action */
                if(i == server_sock) { /* If the server has a new connection pending */
                    accept_new();
                }
                else { /* Message pending */
                    accept_message(i);
                    handle_message(i);
                }
            }
        }
        usleep(100);
    }
}
