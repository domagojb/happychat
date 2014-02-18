#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <ncurses.h>

/* GUI */

/* Text input box width */
#define INPUT_WINDOW_H 8

/* Input and output windows */
WINDOW *inscr;
WINDOW *outscr;

/* Window size */
int row;
int cell;

/* GUI END */

/* Maximum length of received and sent messages in bytes */
#define MAX_LEN 1024

/* Server socket file descriptor */
int sock;

/* The message buffer stores the last message received from stdin or the server
 * nr tells the threads what number the message is(is it a new one) */
struct message_buffer {
    char message[MAX_LEN];
    uint32_t nr;
};

struct message_buffer msg_from_in;
struct message_buffer msg_from_serv;

/* Prints out the messages received from the server */
void print_out();
/* Get all the input from stdio and puts it in a global buffer */
void scan_in();
/* Receives and sends messages from and to the server */
void recvsend();
/* Clean the memmory */
void cleanup();
/* Simple print error and die function */
void die(char *msg);

int main(int argc, char** argv)
{
    struct sockaddr_in server;

    if (argc != 3) {
        die("USAGE: happychat <ip> <port>\n");
    }

    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        die("Failed to create socket\n");
    }

    /* Init ncurses */
    initscr();

    /* Get window size */
    getmaxyx(stdscr, row, cell);

    /* Init the windows */
    inscr = newwin(INPUT_WINDOW_H, cell, row-INPUT_WINDOW_H, 0);
    outscr = newwin(row-INPUT_WINDOW_H, cell, 0, 0);

    /* Make scrolling available */
    scrollok(outscr, TRUE);

    /* Construct server struct */
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(atoi(argv[2]));

    /* Connect to the server */
    if (connect(sock,
                (struct sockaddr *) &server,
                sizeof(server)) < 0) {
            die("Failed to connect to server\n");
    }

    wprintw(outscr, "Connected\n");
    wrefresh(outscr);

    msg_from_in.nr = 0;
    msg_from_serv.nr = 0;

    pthread_t thr[3];
    pthread_create(&thr[1], NULL, scan_in, NULL);
    pthread_create(&thr[2], NULL, print_out, NULL);
    pthread_create(&thr[3], NULL, recvsend, NULL);

    pthread_join(thr[1], NULL);
    pthread_join(thr[2], NULL);
    pthread_join(thr[3], NULL);

    /* Clean up */
    close(sock);
    delwin(inscr);
    delwin(outscr);
    endwin();
    return 0;
}

void scan_in()
{
        mvwhline(inscr, 0, 0, 0, cell);
    while(1) {
        mvwgetstr(inscr, 1, 0, msg_from_in.message);
        msg_from_in.nr++;
        werase(inscr);
        mvwhline(inscr, 0, 0, 0, cell);
        wrefresh(inscr);
        usleep(100);
    }
}

void print_out()
{
    struct message_buffer oldmsg;
    oldmsg.nr = 0;
    while(1) {
        if (oldmsg.nr != msg_from_serv.nr) {
            wprintw(outscr, "%s\n", msg_from_serv.message);
            oldmsg.nr = msg_from_serv.nr;
            wrefresh(outscr);
        }
        usleep(100);
    }
}

void recvsend()
{
    fd_set reads, buffr;
    fd_set sends, buffs;
    FD_ZERO(&reads);
    FD_ZERO(&sends);
    FD_ZERO(&buffr);
    FD_ZERO(&buffs);
    FD_SET(sock, &buffr);
    FD_SET(sock, &buffs);
    int maxfd = sock;

    struct message_buffer oldmsg;
    oldmsg.nr = 0;

    size_t len;
    int n;
    int total = 0;

    /* Timeout time for select() */
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    while(1) {
        reads = buffr;
        sends = buffs;

        if (select(maxfd+1, &reads, &sends, NULL, &tv) == -1)
            perror("Error while selection file descriptor");

        if (FD_ISSET(sock, &reads)) {
            memset(&(msg_from_serv.message), 0, MAX_LEN);
            len = recv(sock, msg_from_serv.message, MAX_LEN, 0);
            if (len == -1)
                perror("Failed while receiving message from server\n");
            else if (len == 0)
                die("Server closed connection\n");
            else
                msg_from_serv.nr++;
        }

        if (FD_ISSET(sock, &sends) && (oldmsg.nr != msg_from_in.nr)) {
            total = 0;
            do {
                n = send(sock, msg_from_in.message+total, MAX_LEN-total, 0);
                if (n == -1) {
                    perror("Failed to send chat message to client\n");
                    break;
                }
                total += n;
            } while (total < MAX_LEN);
            oldmsg.nr = msg_from_in.nr;
        }
        usleep(100);
    }
}

void cleanup()
{
    close(sock);
    delwin(inscr);
    delwin(outscr);
    endwin();
}

/* Simple print error and die function */
void die(char *msg) {
    perror(msg);
    cleanup();
    exit(1);
}
