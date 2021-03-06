#include "./process_pool.h"

#define SEM_WORKER_READY "/worker_ready"
#define SEM_WORKER_BINDED "/worker_binded"
#define SEM_WAKE_WORKER "/wale_worker"
#define SOCKET_PATH "/tmp/fd-pass.socket"
#define SHMEM_HAME "/sharedmem"
#define DC_S_IRUSR 0400
#define DC_S_IWUSR 0200

/**
 * Creates and listens to a domain socket for with the path of SOCKET_PATH.
 * @return socket_fd
 */
static int worker_bind();
/**
 * Waits to receive a msg containing the client fd over the passed
 * in socket. Once the msg is received it returns the client fd.
 * @param socked_fd
 * @return client fd
 */
static int worker_receive();
/**
 * The loop uses semaphores to post that a worker is ready for work then waits until a worker process
 * should be woken. Once woken the worker will exit if mode is not set to process else it binds to a socket
 * then posts that the server can send the client's fd.  It uses worker receive to get the client fd then
 * handles the http request.
 * @param pool
 */
static void worker_loop(process_pool * pool);
/**
 * Creates and connects to a domain socket at SOCKET_PATH. once connected
 * it sends the client fd to a worker process listening to the socket.
 * @param http_client_fd
 */
static void send_socket(int http_client_fd);
/**
 * Creates the required semaphores for managing the process pool.
 * @return semaphores
 */
static semaphores * create_semaphores();

process_pool * process_pool_create(config *cfg) {
    process_pool * pool = calloc(1, sizeof(process_pool));
    pool->sem = create_semaphores();
    pool->cfg = cfg;
    memory *ptr;
    int shared_mem_fd = dc_shm_open(SHMEM_HAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shared_mem_fd, sizeof(memory));
    ptr = mmap(0, sizeof(memory), PROT_WRITE|PROT_READ, MAP_SHARED, shared_mem_fd, 0);
    pool->mem = ptr;
    return pool;
}

void process_pool_start(process_pool * pool) {
    pool->mem->is_running = true;
    for(int i = 0; i < NUM_PROCESSES; i++){
        int pid = fork();
        if(pid == -1){
            exit(EXIT_FAILURE);
        }
        if(pid == 0){
            worker_loop(pool);
            exit(EXIT_FAILURE);
        }
    }
}

void process_pool_stop(process_pool * pool) {
    pool->mem->is_running = false;
    for(int i = 0; i < NUM_PROCESSES; i++)
        dc_sem_post(pool->sem->wake_worker);
}

void process_pool_notify(process_pool * pool, int http_client_fd) {
    semaphores * sem = pool->sem;
    dc_sem_wait(sem->worker_ready);
    dc_sem_post(sem->wake_worker);
    dc_sem_wait(sem->worker_binded);
    send_socket(http_client_fd);
    dc_close(http_client_fd);
}

void process_pool_destroy(process_pool * pool) {
    free(pool);
}

static semaphores * create_semaphores() {
    semaphores * sem = calloc(1, sizeof(semaphores));
    sem_unlink(SEM_WORKER_READY);
    sem_unlink(SEM_WORKER_BINDED);
    sem_unlink(SEM_WAKE_WORKER);
    sem->worker_ready = dc_sem_open(SEM_WORKER_READY, O_CREAT, DC_S_IRUSR|DC_S_IWUSR, 0);
    sem->worker_binded = dc_sem_open(SEM_WORKER_BINDED, O_CREAT, DC_S_IRUSR|DC_S_IWUSR, 0);
    sem->wake_worker = dc_sem_open(SEM_WAKE_WORKER, O_CREAT, DC_S_IRUSR|DC_S_IWUSR, 0);
    return sem;
}


static void send_socket(int http_client_fd) {
    struct sockaddr_un process_address;
    int process_sfd;
        
    process_sfd = dc_socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&process_address, 0, sizeof(struct sockaddr_un));
    process_address.sun_family = AF_UNIX;
    strcpy(process_address.sun_path, SOCKET_PATH);
    dc_connect(process_sfd, (struct sockaddr *) &process_address, sizeof(struct sockaddr_un));

    struct msghdr msg = {0};
    struct cmsghdr *cmsg;
    char buf[CMSG_SPACE(sizeof(int))], *dup = "hello world";
    memset(buf, '\0', sizeof(buf));
    struct iovec io = { .iov_base = &dup, .iov_len = sizeof(dup) };

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof (buf);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    int *fdptr = (int *)CMSG_DATA(cmsg);
    *fdptr = http_client_fd;

    if (sendmsg(process_sfd, &msg, 0) == -1){
        perror("sendmsg()");
        exit(EXIT_FAILURE);
    }
}

static int worker_receive(int socked_fd) {
    struct msghdr msg = {0};
    struct cmsghdr *cmsg;
    char buf[CMSG_SPACE(sizeof(int))], dup[256];
    memset(buf, '\0', sizeof(buf));
    struct iovec io = { .iov_base = &dup, .iov_len = sizeof(dup) };

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);
        
    if (recvmsg(socked_fd, &msg, 0) < 0){
        perror("recvmsg()");
        exit(EXIT_FAILURE);
    }
    
    cmsg = CMSG_FIRSTHDR(&msg);
    int *fdptr = (int *)CMSG_DATA(cmsg);
    return *fdptr;
}

static int worker_bind() {
    struct sockaddr_un addr;

    int socket_fd = dc_socket(AF_UNIX, SOCK_STREAM, 0);


    unlink(SOCKET_PATH);
        
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
        
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
        
    dc_bind(socket_fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un));

    dc_listen(socket_fd, 5);
    
    return socket_fd;
}

static void worker_loop(process_pool * pool) {
    semaphores * sem = pool->sem;
    for (;;) {
        dc_sem_post(sem->worker_ready);
        dc_sem_wait(sem->wake_worker);
        if(!pool->mem->is_running) {
            exit(EXIT_SUCCESS);
        } 
        int worker_fd = worker_bind();
        dc_sem_post(sem->worker_binded);

        int main_process_fd = dc_accept(worker_fd, NULL, NULL);
        int http_client_fd = worker_receive(main_process_fd);

        config * conf = get_config(pool->cfg);
        http_handle_client(conf, http_client_fd);
        destroy_config(conf);

        close(worker_fd);
        close(main_process_fd);
        close(http_client_fd);
    }
}
