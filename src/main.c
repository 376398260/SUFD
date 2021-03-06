/*
** A Simple MultiThread Unix File Daemon in C
**
** @author: Wentao Lu
** @date: 2020/04/02
*/

#include "define.h"

// define global variables and initialize
int lockfile = -1;
int DEBUG_MODE = 0;
int DELAY_MODE = 0;
int VERBOSE_MODE = 0;
int ssock = 0;
int fsock = 0;
int fsock_tmp = 0;
char* s_port = "9001";  // default shell port number
char* f_port = "9002";  // default file port number
char* peers[64] = { NULL };
pthread_attr_t attr;
pthread_mutex_t wake_mutex;
pthread_mutex_t lock_mutex;
pthread_mutex_t logger_mutex;
int thread_pool_size = 0;
struct thread_t* thread_pool;
struct monitor_t monitor = { .t_inc=128, .t_act=0, .t_tot=0, .t_max=256 };  // default thread pool parameters
struct lock_t locks[65535];
int n_lock = 0;

int main(int argc, char* argv[]) {
    // parse command line switches and arguments
    int copt = 0, err_switch = 0, count = 0, index;
    while ((copt = getopt(argc, argv, "dvDf:s:t:T:p:")) != -1) {
        char c = (char)copt;
        switch (c) {
            case 'd':
                DEBUG_MODE = 1;
                break;
            case 'D':
                DELAY_MODE = 1;
                break;
            case 'v':
                VERBOSE_MODE = 1;
                break;
            case 's':
                if (atoi(optarg) == 0) err_switch = 1;
                s_port = optarg;
                break;
            case 'f':
                if (atoi(optarg) == 0) err_switch = 1;
                f_port = optarg;
                break;
            case 't':
                monitor.t_inc = atoi(optarg);
                if (monitor.t_inc == 0) err_switch = 1;
                break;
            case 'T':
                monitor.t_max = atoi(optarg);
                if (monitor.t_max == 0) err_switch = 1;
                break;
            case 'p':
                index = optind - 1;
                while (index < argc) {  // fetch all valid host:port pairs
                    char* peer = strdup(argv[index]);
                    index++;
                    if (peer[0] == '-') {
                        optind = index - 1;
                        break;
                    }
                    peers[count++] = peer;
                }
                break;
            case '?':
                err_switch = 1;
                break;
        }
    }

    if (err_switch) {
        fprintf(stderr, "Usage: %s -p <host1:port1>..<hostN:portN> [-t] [-T] [-d] [-D] [-v] [-s port] [-f port] \n", argv[0]);
        exit(29);
    }

    // startup server
    if (init_server() != 0) {
        printf("failed to initialize server\n");
        exit(1);
    }

    // establish master sockets
    ssock = setListener("localhost", s_port, 1);  // loopback socket, allow only 1 connection from localhost
    fsock = setListener(NULL, f_port, 1024);  // passive socket, wait for client connections
    if (ssock == -1 || fsock == -1) {
        logger("unable to establish a listener socket");
        exit(2);
    }

    // establish signal mask in the main thread to block unwanted signals
    sigset_t set;
    sigemptyset(&set);

    int sigs[8] = {SIGINT, SIGTERM, SIGALRM, SIGABRT, SIGPIPE, SIGCHLD, SIGHUP, SIGQUIT};
    for (int i = 0; i < 8 ; i++) {
        sigaddset(&set, sigs[i]);
    }

    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        printf("failed to set signal masks\n");
        exit(92);
    }

    // launch a separate signal thread to receive and handle all signals
    pthread_t sid;
    if (pthread_create(&sid, &attr, signal_thread, (void*)&set) != 0) {
        perror("pthread_create");
        fflush(stderr);
        exit(-17);
    }

    // launch the monitor thread for dynamic threads management and reconfiguration
    pthread_t mid;
    if (pthread_create(&mid, &attr, monitor_thread, NULL) != 0) {
        perror("pthread_create");
        fflush(stderr);
        exit(3);
    }

    // the main thread continues to become the shell server, accept command from a local administrator
    struct sockaddr_storage cli_addr;
    socklen_t sin_size = sizeof(cli_addr);
    char ipstr[INET6_ADDRSTRLEN];

    struct pollfd pfds[1];
    pfds[0].fd = ssock;
    pfds[0].events = POLLIN;

    while (1) {
        int asock = accept(pfds[0].fd, (struct sockaddr*)&cli_addr, &sin_size);
        if (asock == -1) {
            perror("accept");
            fflush(stderr);
            exit(133);
        }

        inet_ntop(cli_addr.ss_family, extractAddr((struct sockaddr*)&cli_addr), ipstr, INET6_ADDRSTRLEN);
        char msg[128];
        memset(msg, 0, sizeof(msg));
        sprintf(msg, "admin is now online on socket %d", asock);
        logger(msg);

        serve_admin(asock);
    }

    // unreachable code
    stop_server();
    return 0;
}
