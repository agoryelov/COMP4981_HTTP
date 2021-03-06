#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <errno.h>

#include <dc/sys/socket.h>

#include "http_protocol/thread_pool.h"
#include "http_protocol/process_pool.h"
#include "http_protocol/http.h"

#define BACKLOG 5

static int create_server_fd();

int main(int argc, char **argv) {
    config * cmd_conf = get_cmd_config(argc, argv);
    config * conf = get_config(cmd_conf);
    int server_fd = create_server_fd(conf->port);

    for(;;) {
        process_pool * p_pool;
        thread_pool * t_pool;

        if(conf->mode == 'p'){
            p_pool = process_pool_create(cmd_conf);
            process_pool_start(p_pool);
            printf("Starting processes\n");
            while(conf->mode == 'p') {
                int client_fd = accept(server_fd, NULL, NULL);
                process_pool_notify(p_pool, client_fd);
                destroy_config(conf);
                conf = get_config(cmd_conf);
            }
            process_pool_stop(p_pool);
            process_pool_destroy(p_pool);
        }

        if(conf->mode == 't') {
            t_pool = thread_pool_create(cmd_conf);
            thread_pool_start(t_pool);
            printf("Starting threads\n");
            while(conf->mode == 't') {
                int client_fd = accept(server_fd, NULL, NULL);
                thread_pool_notify(t_pool, client_fd);
                destroy_config(conf);
                conf = get_config(cmd_conf);
            }
            thread_pool_stop(t_pool);
            thread_pool_destroy(t_pool);
        }
    }
    close(server_fd);
    destroy_config(cmd_conf);
    destroy_config(conf);
    
    return EXIT_SUCCESS;
}

static int create_server_fd(int port) {
    struct sockaddr_in addr;
    int sfd;
    signal(SIGPIPE, SIG_IGN);

    sfd = dc_socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    int optval = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    dc_bind(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    dc_listen(sfd, BACKLOG);
    return sfd;
}