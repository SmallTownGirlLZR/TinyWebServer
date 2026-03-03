#include "webServer.h"
#include "CGImysql/sql_connection_pool.h"
#include "http/http_conn.h"
#include "log/log.h"
#include "timer/lst_timer.h"
#include <asm-generic/socket.h>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <netinet/in.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
//#include <thread>

WebServer::WebServer(){
    users = new http_conn[MAX_FD];   /* 客户最大数和文件描述符最大值有关 */ 
    char server_path[200];
    /* Get the pathname of current working directory */
    getcwd(server_path,sizeof(server_path));
    char root[7] = "./root";
    /* 此文件必须和root在一个目录下 
       + 1 保存 '\0'
    */ 
    m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root,server_path);
    strcpy(m_root,root);
    
    /* 用于定时器 */ 
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer(){
    close(m_epollfd);    
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    close(m_listenfd);

    delete[] users;
    delete[] users_timer;
    delete m_pool;

}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}


void WebServer::trig_mode(){
    /* 为m_LISTENTTrigmode 和 m_CONNTrigmode 设置 LT ET */
    m_LISTENTrigmode = ((m_TRIGMode >> 1) & 1);
    m_CONNTrigmode = (m_TRIGMode & 1);  
}

void WebServer::log_write(){
    if(m_close_log == 0){
        if(m_log_write == 1)
            Log::getInstance() -> init("./ServerLog",m_close_log,2000,800000,800);
        else 
            Log::getInstance() -> init("./ServerLog",m_close_log,2000,80000,0);
    }
}


void WebServer::sql_pool(){
    m_connPool = connection_pool::GetInstance();
    m_connPool -> init("localhost",m_user,m_passWord,m_databaseName,3306,m_sql_num,m_close_log);

    users -> initmysql_result(m_connPool);     /* initmysql_result 取出所有的用户名和密码 */
}

void WebServer::threadpool(){
    m_pool = new threadPool<http_conn>(m_actormodel,m_connPool,m_thread_num);
}

void WebServer::eventListen(){
    /* 网络编程基础步骤 */
    m_listenfd = socket(AF_INET,SOCK_STREAM,0);
    assert(m_listenfd >= 0);

    if(m_OPT_LINGER == 0){
        struct linger tmp = {0,1};
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }else if(m_OPT_LINGER == 1){
        struct linger tmp = {1,1};   /* 启用 */    
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_port = htons(m_port);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    int flag = 1;
    setsockopt(m_listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
    ret = bind(m_listenfd,(const struct sockaddr*)&address,sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd,5);
    assert(ret >= 0);

    utils.init(TIMESLOT); /* 设置超时时间 */

    /* epoll */
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(1);      /* 创建epoll 句柄*/
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd,m_listenfd,false,m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;   /* 为每个连接初始化 empollfd */
    
    // 创建域套接字 
    ret = socketpair(PF_UNIX,SOCK_STREAM,0,m_pipefd);
    assert(ret != -1);

    utils.setnonblocking(m_pipefd[1]);
    // 将读端加入epoll红黑树中
    utils.addfd(m_epollfd,m_pipefd[0],false,0);
    

    utils.addsig(SIGPIPE,SIG_IGN);    // ignore signal + restar
    utils.addsig(SIGALRM,utils.sig_handler,false);
    // sig_handler 将使用域套接字 发送信号信息给另一个进程 
    utils.addsig(SIGTERM,utils.sig_handler,false);
    
    alarm(TIMESLOT);   // TIMESLOT 后发送SIGALRM信号
    
    Utils::u_epollfd = m_epollfd;
    Utils::u_pipefd = m_pipefd;
}

void WebServer::timer(int connfd,struct sockaddr_in client_address){
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer* timer = new util_timer;
    timer -> user_data = &users_timer[connfd];
    timer -> cb_func = cb_func;

    time_t cur = time(NULL);
    timer -> expire = cur + 3 * TIMESLOT;   /* 设置过期时间 */
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);     /* 向链表中插入节点 */ 
    
}

/* 如果有新数据到达，客户端连接由不活跃变为活跃
 * 则需要将定时器延迟三个单位
 * 并且要调整节点在链表中的位置
 * */
void WebServer::adjust_timer(util_timer* timer){
    time_t cur = time(NULL);
    timer -> expire = cur + 3 * TIMESLOT;

    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s","adjust timer once");
}


void WebServer::deal_timer(util_timer* timer,int sockfd){
    /* 回调函数，将timer对应的客户fd从epoll中删除 */
    timer -> cb_func(&users_timer[sockfd]);
    if(timer)
        utils.m_timer_lst.del_timer(timer);     /* 含delete timer，所以必须先调回调函数 */
    LOG_INFO("close fd %d",users_timer[sockfd].sockfd);
}

/* 处理m_listenfd活动 */
bool WebServer::dealclientdata(){
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    
    if(m_LISTENTrigmode == 0){
        int connfd  = accept(m_listenfd,(struct sockaddr*)&client_address,&client_addrlength);
        if(connfd < 0){
            LOG_ERROR("%s: errno is %d","accept error",errno);
            return false;
        }
        if(http_conn::m_user_count > MAX_FD){
            utils.show_error(connfd,"Interna server busy");
            LOG_ERROR("%s","Internal server busy");
            return false;
        }

        timer(connfd,client_address);       // 完成utils_timer节点初始化，并把节点加入链表
    }else{  // ET 模式
        while(true){
            int connfd = accept(m_listenfd,(struct sockaddr*)&client_address,&client_addrlength);
            if(connfd < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK)
                    break;      /* 在此处退出循环 */
                LOG_ERROR("%s:errno is %d","accpet",errno);
                break;
            }
            if(http_conn::m_user_count >= MAX_FD){
                utils.show_error(connfd,"Internal server busy");
                LOG_ERROR("%s","Intenal server busy");
                break;
            }
            timer(connfd,client_address);
        }
        return false;
    }

    return true;
}

/* epoll 上的 域套接字 收到信号信息 */
bool WebServer::dealwithsignal(bool &timeout,bool &stop_server){
    int ret = 0;
    int sig;
    char signals[1024];
    
    ret = recv(m_pipefd[0],signals,sizeof(signals),0);

    if(ret == -1 || ret == 0)   return false;
    else{
        /* sig_handler 函数中 只发送信号编号一个字节
         * 一般来说ret == 1
         * */
        for(int i = 0;i < ret; i++){
            switch(signals[i]){
            case SIGALRM:{
                timeout = true;
                break;
            }
            case SIGTERM:{
                stop_server = true;
                break;
            }
            }
        }
    }

    return true;
}

/* 处理读任务 */
void WebServer::dealwithread(int sockfd){
    util_timer* timer = users_timer[sockfd].timer;

    /* Reactor */
    if(m_actormodel == 1){
            /* timer对应的客户活跃，修改到期时间*/
        if(timer)   adjust_timer(timer);

        m_pool -> append(users + sockfd,0);

        while(true){
            if(users[sockfd].improv == 1){      /* 线程将工作处理完毕 */
                if(users[sockfd].timer_flag == 1){      /* 读任务处理失败时timer_flag设为1 */
                    deal_timer(timer,sockfd);   /* 清理节点 */
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }else{  /* Proactor */
        if(users[sockfd].read_once()){      /* 主线程负责 IO ,工作线程负责业务处理 */
            LOG_INFO("deal with the client(%s)",inet_ntoa(users[sockfd].get_address() -> sin_addr));
            
            /* 线程池的run函数中对Reactor 和 Proactor 做出不同处理
             * Proactor 在 run 函数中 不进行IO
             * */
            m_pool -> append_p(users + sockfd);
            if(timer)   adjust_timer(timer);
        }else{  /* read_once 返回false 时清理节点*/
            deal_timer(timer,sockfd);
        }
    }
}

/* 处理写任务 */
void WebServer::dealwithwrite(int sockfd){
    util_timer *timer = users_timer[sockfd].timer;

    /* Reactor */
    if(m_actormodel == 1){
        if(timer)   adjust_timer(timer);
        
        m_pool -> append(users + sockfd,1);
        
        while(true){
            if(users[sockfd].improv == 1){
                if(users[sockfd].timer_flag == 1){      /* 写入失败时 timer_flag 设置为 1 */
                    deal_timer(timer,sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users -> improv = 0;
                break;
            }
        }
    }else{  /* Proactor 模式*/
        if(users[sockfd].write()){
            LOG_INFO("send data to the client(%s)",inet_ntoa(users[sockfd].get_address() -> sin_addr));
            if(timer)   adjust_timer(timer);
        }else{
            /* 写入失败 */
            deal_timer(timer,sockfd);
        }
    }
}

void WebServer::eventLoop(){
    bool timeout = false;
    bool stop_server = false;

    while(!stop_server){
        int number = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if(number < 0 && errno != EINTR){
            LOG_ERROR("%s","epoll failure");
            break;
        }

        for(int i = 0;i < number; i++){
            int sockfd = events[i].data.fd;
            /* listenfd 有活动 处理新连接*/
            if(sockfd == m_listenfd){
                bool flag = dealclientdata();
                if(!flag)   continue;
            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                /* 关闭服务端连接，并且移除定时器 */
                util_timer* timer = users_timer[sockfd].timer;
                deal_timer(timer,sockfd);
            }else if((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)){ /* 收到信号 */
                bool flag = dealwithsignal(timeout,stop_server);
                if(flag == false)
                    LOG_ERROR("%s","dealclientdata failure");
            }else if(events[i].events & EPOLLIN){ /* 读任务 */
                dealwithread(sockfd);
            }else if(events[i].events & EPOLLOUT){  /* 写任务 */
                dealwithwrite(sockfd);
            }
            
            if(timeout){
                utils.timer_handler();

                LOG_INFO("%s","time tick");
                timeout = false;
            }
        }
    }
}












