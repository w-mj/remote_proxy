#include <iostream>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <cstdlib>
#include <netinet/in.h>
#include <ctype.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unordered_map>
#include <unordered_set>
#include <arpa/inet.h>
#include <unistd.h>


std::unordered_map<int, int> sock_pair;

/**
  A      B <--- C
  A ---> B ---> C
  A ---> B <--- C
           <--> C
  A <--> B <--> C
  */

#define MAXSIZE 4096

void help(char *argv[]) {
    std::cout << argv[0] << " server <external port> <server port>" << std::endl;
    std::cout << argv[0] << " client <server ip> <server port> <local ip> <local port>" << std::endl;
}

int listen_sock(int port) {
    int i_listenfd;
    struct sockaddr_in st_sersock;
    printf("try listen %d\n", port);

    if((i_listenfd = socket(AF_INET, SOCK_STREAM, 0) ) < 0)
    {
        printf("socket Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(0);
    }

    memset(&st_sersock, 0, sizeof(st_sersock));
    st_sersock.sin_family = AF_INET;
    st_sersock.sin_addr.s_addr = htonl(INADDR_ANY);
    st_sersock.sin_port = htons(port);

    int reuse = 0x0;
    int result = setsockopt(i_listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(int));
    if(result != 0)
    {
        printf(" Fail to set socket reuseraddr options, errno = 0x%x!\n", result);
        exit(0);
    }

//    reuse = 0x0;
//    result = setsockopt(i_listenfd, SOL_SOCKET, SO_REUSEPORT, (char *)&reuse, sizeof(int));
//    if(result != 0)
//    {
//        printf(" Fail to set socket reuseraddr options, errno = 0x%x!\n", result);
//        exit(0);
//    }


    if(bind(i_listenfd,(struct sockaddr*)&st_sersock, sizeof(st_sersock)) < 0)
    {
        printf("bind Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(0);
    }

    if(listen(i_listenfd, 20) < 0)
    {
        printf("listen Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(0);
    }
    return i_listenfd;
}

void add_epoll(int ep, int sock) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sock;
    if(epoll_ctl(ep, EPOLL_CTL_ADD, sock, &ev) < 0)
    {
        printf("epoll_ctl Error3: %s (errno: %d) fd %d\n", strerror(errno), errno, sock);
        exit(-1);
    }
}

char msg[MAXSIZE];
void send_pair(int msg_fd, int epfd) {
    int pair_fd;
    if (sock_pair.find(msg_fd) == sock_pair.end() || (pair_fd = sock_pair[msg_fd]) == 0) {
        printf("Cannot find pair fd for %d\n", msg_fd);
        close(msg_fd);
        return;
    }
    // printf("send_pair %d -> %d\n", msg_fd, pair_fd);

    int size = read(msg_fd, msg, MAXSIZE);
    if (size <= 0) {
        if (size < 0) {
            printf("Read from %d error\n", msg_fd);
            perror("read");
        } else {
            printf("Bye1 %d<->%d\n", msg_fd, pair_fd);
        }

        epoll_ctl(epfd, EPOLL_CTL_DEL, msg_fd, NULL);
        epoll_ctl(epfd, EPOLL_CTL_DEL, pair_fd, NULL);
        close(msg_fd);
        close(pair_fd);
        return;
    }
    if ((size = write(pair_fd, msg, size)) <= 0) {
        if (size < 0) {
            printf("Write to %d error\n", msg_fd);
            perror("Write");
        } else {
            printf("Bye2 %d<->%d\n", msg_fd, pair_fd);
        }

        epoll_ctl(epfd, EPOLL_CTL_DEL, msg_fd, NULL);
        epoll_ctl(epfd, EPOLL_CTL_DEL, pair_fd, NULL);
        close(msg_fd);
        close(pair_fd);
        return;
    }
    // printf("send_pair %d->%d  %d\n", msg_fd, pair_fd, size);
}

void server(int external_port, int server_port) {
    printf("start server, %d %d\n", external_port, server_port);
    int i_connfd;

    struct epoll_event ev, events[MAXSIZE];
    int epfd, nCounts;


    if((epfd = epoll_create(MAXSIZE)) < 0) {
        printf("epoll_create Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }

    int listen_a_fd = listen_sock(external_port);
    int listen_c_fd = listen_sock(server_port);
    printf("listen a fd %d, c fd %d\n", listen_a_fd, listen_c_fd);
    add_epoll(epfd, listen_a_fd);
    add_epoll(epfd, listen_c_fd);

    printf("======waiting for client's request======\n");
    while(1) {
        if((nCounts = epoll_wait(epfd, events, MAXSIZE, -1)) < 0) {
            printf("epoll_ctl Error1: %s (errno: %d)\n", strerror(errno), errno);
            exit(-1);
        }
        else if(nCounts == 0){
            printf("time out, No data!\n");
        } else {
            for(int i = 0; i < nCounts; i++) {
                int tmp_epoll_recv_fd = events[i].data.fd;
                if(tmp_epoll_recv_fd == listen_a_fd) {
                    // new service client connect
                    if((i_connfd = accept(listen_a_fd, (struct sockaddr*)NULL, NULL)) < 0) {
                        printf("accept Error: %s (errno: %d)\n", strerror(errno), errno);
                        continue;
                    } else {
                        printf("Client[%d], welcome!\n", i_connfd);
                    }

                    int inner_server_fd = sock_pair[0];
                    if (inner_server_fd == 0) {
                        printf("inner service not connect. close socket!\n");
                        close(i_connfd);
                        continue;
                    }

                    if (write(inner_server_fd, &i_connfd, sizeof(i_connfd)) < 0) {
                        perror("write to inner client error");
                        sock_pair[0] = 0;
                        continue;
                    }

                } else if (tmp_epoll_recv_fd == listen_c_fd) {
                    // answer from C
                    int fd_a, fd_c, size;
                    if((fd_c = accept(listen_c_fd, (struct sockaddr*)NULL, NULL)) < 0) {
                        printf("accept Error: %s (errno: %d)\n", strerror(errno), errno);
                        continue;
                    } else {
                        printf("Inner Client[%d], welcome!\n", fd_c);
                    }
                    if (read(fd_c, &fd_a, sizeof(fd_a)) <= 0) {
                        perror("Cannot read a's fd.");
                        close(fd_c);
                        continue;
                    }
                    if (fd_a == 0) {
                        printf("Register client c.\n");
                        sock_pair[0] = fd_c;
                    } else {
                        sock_pair[fd_c] = fd_a;
                        sock_pair[fd_a] = fd_c;

                        add_epoll(epfd, fd_a);
                        add_epoll(epfd, fd_c);
                    }
                } else {
                    int msg_fd = tmp_epoll_recv_fd;
                    send_pair(msg_fd, epfd);
                }
            }
        }
    }//while
    close(listen_a_fd);
    close(listen_c_fd);
    close(epfd);
}

int connect_addr(char *ip, int port) {
    printf("tring connect to %s:%d\n", ip, port);
    int b_fd;
    if ((b_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        return -1;
    }
    struct sockaddr_in staddr;
    memset(&staddr, 0, sizeof(staddr));
    if (inet_pton(AF_INET, ip, &staddr.sin_addr) < 0) {
        printf("Wrong ip %s\n", strerror(errno));
        return -1;
    }
    staddr.sin_family = AF_INET;
    staddr.sin_port = htons(port);
    if (connect(b_fd, (struct sockaddr*)&staddr, sizeof(staddr)) < 0) {
        printf("connect to %s:%d error\n", ip, port);
        perror("connect");
        return -1;
    }
    printf("connect success to %s:%d\n", ip, port);
    return b_fd;
}

void client(char *server_ip, int server_port, char *local_ip, int local_port) {
    int b_fd = connect_addr(server_ip, server_port);
    if (b_fd < 0) {
        printf("Cannot connect to server %s:%d\n", server_ip, server_port);
        exit(1);
    }
    int n = 0;
    if (write(b_fd, &n, sizeof(n)) <= 0) {
        perror("write");
        exit(1);
    } else {

    }
    struct epoll_event ev, events[MAXSIZE];
    int epfd, nCounts;
    if((epfd = epoll_create(MAXSIZE)) < 0) {
        printf("epoll_create Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }
    add_epoll(epfd, b_fd);
    while (1) {
        if((nCounts = epoll_wait(epfd, events, MAXSIZE, -1)) < 0) {
            printf("epoll_ctl Error2: %s (errno: %d)\n", strerror(errno), errno);
            exit(-1);
        }
        else if(nCounts == 0){
            printf("time out, No data!\n");
        }
        help(argv);
        for(int i = 0; i < nCounts; i++) {
            int tmp_epoll_recv_fd = events[i].data.fd;
            if (tmp_epoll_recv_fd == b_fd) {
                printf("create new channel.\n");
                int a_fd;
                int size = read(tmp_epoll_recv_fd, &a_fd, sizeof(a_fd));
                if (size <= 0) {
                    printf("server error. quit\n");
                    perror("server");
                    exit(0);
                }
                int cfd = connect_addr(local_ip, local_port);
                if (cfd <= 0) {
                    perror("New channel: connect to local ip");
                    continue;
                }

                int nfd = connect_addr(server_ip, server_port);
                if (nfd <= 0 || (write(nfd, &a_fd, sizeof(a_fd)) <= 0)) {
                    perror("New channel: connect to server.");
                    close(nfd);
                    continue;
                }
                add_epoll(epfd, cfd);
                add_epoll(epfd, nfd);
                sock_pair[cfd] = nfd;
                sock_pair[nfd] = cfd;
            } else {
                send_pair(tmp_epoll_recv_fd, epfd);
            }
        }
    }
    close(epfd);
    close(b_fd);
}

int main(int argc, char *argv[])
{
    char *type;
    if (argc == 1){
        help(argv);
        return 1;
    }
    type = argv[1];
    if (argc == 4 && strcmp(type, "server") == 0) {
        server(atoi(argv[2]), atoi(argv[3]));
    } else if (argc == 6 && strcmp(type, "client") == 0) {
        client(argv[2], atoi(argv[3]), argv[4], atoi(argv[5]));
    } else {
        help(argv);
        return 1;
    }
}
