#include "http_conn.h"
#include <mysql/mysql.h>
#include <fstream>

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

int setnonblocking(int fd){
    int old = fcntl(fd,F_GETFL);
    fcntl(fd,F_SETFL,old | O_NONBLOCK);

    return old;
}

void addfd(int epollfd, int fd, bool one_shot,int TRIGMode){    /* 是否 ET */
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else 
        event.events = EPOLLIN | EPOLLRDHUP;
    
    if(one_shot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,nullptr);
    close(fd);
}

/* 重置为EPOLLONESHHOT */
void modfd(int epollfd, int fd,int ev, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode)    /* ET */
        event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);

}
