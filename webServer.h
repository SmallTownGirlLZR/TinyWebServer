#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "./http/http_conn.h"
#include "./threadPool/threadPool.h"
#include "./timer/lst_timer.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port, std::string user, std::string passWord,
                std::string databaseName, int log_write, int opt_linger,
                int trigmode, int sql_num, int thread_num, int close_log,
                int actor_mode);
    void threadpool();
    void sql_pool();

    void log_write();
    void trig_mode();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    void eventListen();
    bool dealclientdata();
    bool dealwithsignal(bool& timeout, bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    int m_port;
    char *m_root;
    int m_log_write;
    int m_close_log;
    int m_actormodel;

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    /* 数据库 */
    connection_pool *m_connPool;
    std::string m_user;         //登陆数据库用户名
    std::string m_passWord;     //登陆数据库密码
    std::string m_databaseName; //使用数据库名
    int m_sql_num;

    //线程池相关
    threadPool<http_conn> *m_pool;
    int m_thread_num;

    /* epoll_wait 的参数 */
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;

    //定时器相关
    client_data *users_timer;
    Utils utils;
};
