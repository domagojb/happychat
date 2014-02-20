#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <ncurses.h>

/* Text input box width */
#define INPUT_WINDOW_H 8

/* Input and output windows */
WINDOW *inscr;
WINDOW *outscr;

/* Window size */
int row;
int cell;

/* Maximum length of received and sent messages in bytes */
#define MAX_LEN 1024

/* Server socket file descriptor */
int sock;

/* Server information */
struct sockaddr_in server;

/* The message buffer stores the last message received from stdin or the server
 * nr tells the threads what number the message is(is it a new one) */
struct message_buffer {
    char message[MAX_LEN];
    uint32_t nr;
};

struct message_buffer msg_from_in;
struct message_buffer msg_from_serv;

/* Server ip addres and port */
char ip[INET_ADDRSTRLEN];
uint16_t port;

/* Thread flag, when to disconnect */
int work;

/* Menu message */
char menu_msg[MAX_LEN];

/* Error log file */
FILE *errlogf;

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
/* Get the ip and port of the server */
void menu(char *msg);
/* The chat */
void chat();

int main(int argc, char** argv)
{
    /* Init ncurses */
    initscr();

    /* Get window size */
    getmaxyx(stdscr, row, cell);

    /* Make scrolling available */
    scrollok(outscr, TRUE);

    /* Open the error log file */
    errlogf = fopen("./err.log", "w");

    chat();

    /* Clean up */
    endwin();
    return 0;
}

void chat()
{
    int quit = 1;
    sprintf(menu_msg, "Enter ip and port to connect");
    while(quit) {
        menu(menu_msg);

        /* Ip and port == 0 signal exit */
        if (strcmp(ip, "0") == 0 && port == 0)
            break;

        if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
            fprintf(errlogf, "Failed to create socket\n");
            sprintf(menu_msg, "Failed to create socket");
            continue;
        }

        /* Construct server struct */
        memset(&server, 0, sizeof(server));
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(ip);
        server.sin_port = htons(port);

        /* Connect to the server */
        if (connect(sock,
                    (struct sockaddr *) &server,
                    sizeof(server)) < 0) {
                fprintf(errlogf, "Failed to connect to server\n");
                sprintf(menu_msg, "Failed to connect to server");
                continue;
        }

        /* Initialize the cat window */
        inscr = newwin(INPUT_WINDOW_H, cell, row-INPUT_WINDOW_H, 0);
        outscr = newwin(row-INPUT_WINDOW_H, cell, 0, 0);

        wprintw(outscr, "Connected\n");
        wrefresh(outscr);

        msg_from_in.nr = 0;
        msg_from_serv.nr = 0;
        work = 1;

        /* Start the chat */
        pthread_t thr[3];
        pthread_create(&thr[1], NULL, scan_in, NULL);
        pthread_create(&thr[2], NULL, print_out, NULL);
        pthread_create(&thr[3], NULL, recvsend, NULL);

        pthread_join(thr[1], NULL);
        pthread_join(thr[2], NULL);
        pthread_join(thr[3], NULL);

        /* Clear and delete the chat windows */
        close(sock);
        werase(inscr);
        werase(outscr);
        wrefresh(inscr);
        wrefresh(outscr);
        delwin(inscr);
        delwin(outscr);
    }
}

void menu(char *msg)
{
    WINDOW *loginscr;

    loginscr = newwin(7, 30, row/2 -4, cell/2-15);
    box(loginscr, 0, 0);
    /* Write happy chat and some glitter on top of input window */
    attron(A_BOLD);
    mvprintw(row/2 - 6, cell/2 - 5, "HAPPYCHAT");
    attroff(A_BOLD);
    mvprintw(row/2 - 5, cell/2 - 16, "(Enter 0 for ip and port to exit)");
    mvprintw(row/2 + 6, cell/2 - strlen(msg)/2, "%s", msg);
    refresh();

    /* Ip and port input window */
    wattron(loginscr, A_BOLD | A_STANDOUT);
    mvwprintw(loginscr, 2, 1, "ip:                         ");
    mvwprintw(loginscr, 4, 1, "port:                       ");
    wattroff(loginscr, A_BOLD | A_STANDOUT);
    wrefresh(loginscr);

    /* Get the input */
    mvwgetstr(loginscr, 2, 7, ip);
    mvwscanw(loginscr, 4, 7, "%d", &port);

    /* Clear and delete the window */
    erase();
    werase(loginscr);
    refresh();
    wrefresh(loginscr);
    delwin(loginscr);
}

void scan_in()
{
    mvwhline(inscr, 0, 0, 0, cell);

    while(work) {
        mvwgetstr(inscr, 1, 0, msg_from_in.message);
        if (strcmp(msg_from_in.message, "!EXIT") == 0) {
            sprintf(menu_msg, "Enter ip and port to connect");
            work = 0;
            break;
        }
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
    while(work) {
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

    while(work) {
        reads = buffr;
        sends = buffs;

        if (select(maxfd+1, &reads, &sends, NULL, &tv) == -1)
            fprintf(errlogf, "Error while selection file descriptor");

        if (FD_ISSET(sock, &reads)) {
            memset(&(msg_from_serv.message), 0, MAX_LEN);
            len = recv(sock, msg_from_serv.message, MAX_LEN, 0);
            if (len == -1) {
                fprintf(errlogf, "Failed while receiving message from server\n");
            }
            else if (len == 0) {
                fprintf(errlogf, "Server closed connection\n");
                sprintf(menu_msg, "Server closed connection");
                work = 0;
                break;
            }
            else {
                msg_from_serv.nr++;
            }
        }

        if (FD_ISSET(sock, &sends) && (oldmsg.nr != msg_from_in.nr)) {
            total = 0;
            do {
                n = send(sock, msg_from_in.message+total, MAX_LEN-total, 0);
                if (n == -1) {
                    fprintf(errlogf, "Failed to send chat message to client\n");
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
    if (errlogf != NULL)
        fclose(errlogf);
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
