#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
//#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,         /* Keep Going */
        GET_REQUEST,        /* Finish */
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, std::string user, std::string passwd, std::string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;


private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;                           /* 客户所对应的socket*/
    sockaddr_in m_address;                  /* sockaddr_in 结构体保存客户端信息*/
    char m_read_buf[READ_BUFFER_SIZE];      /* 读缓冲区*/
    long m_read_idx;                        /* 读偏移量，标识已经读取m_read_idx字节的数据(char)*/
    long m_checked_idx;                     /* 当前要解析的字符位置 */
    int m_start_line;                       /* 当前解析的行的首字符位置，标定从状态机的parse_line */
    char m_write_buf[WRITE_BUFFER_SIZE];    /* 写缓冲区 */
    int m_write_idx;                        /* 写偏移量，标识已经写入m_write_idx字节的数据(char)*/
    CHECK_STATE m_check_state;              /* 主状态机状态 */
    METHOD m_method;                        /* HTTP 方法，如GET POST */
    char m_real_file[FILENAME_LEN];         /* 真实路径名 如home/Lin/project/tinyWebServer/root/index.html */
    char *m_url;                            /* 浏览器输入的url,如/index.html 拼接之后形成m_real_file */
    char *m_version;                        /* HTTP版本，如HTTP/1.1 */
    char *m_host;                           /* 从HTTP报文中解析出的客户主机名 */
    long m_content_length;                  /* HTTP请求体的长度 */
    bool m_linger;                          /* 是否长链接 */
    char *m_file_address;                   /* 内存映射(mmap)的起始地址，用户零拷贝，将m_real_file映射到内存中 */
    struct stat m_file_stat;                /* 所请求的资源的文件状态 如index.html的状态 */
    struct iovec m_iv[2];                   /* 由于聚集写，[0]指向m_write_buffer(响应头部)，[1]指向m_file_address（所请求的资源）*/
    int m_iv_count;                         /* 1(只含响应头)或2(包括响应体)*/
    int cgi;                                /* 是否启用的POST */
    char *m_string;                         /* 存储用户名和密码信息
                                                user=123&passwd=123
                                             */
    int bytes_to_send;                      /* 需要发送的字节数 */
    int bytes_have_send;                    /* 已经发送的字节数 */
    char *doc_root;                         /* 网站根目录地址 */

    std::map<std::string, std::string> m_users;
    int m_TRIGMode;                         /* 设置ET 和 LT*/
    int m_close_log;                        /* 是否关闭日志 */

    char sql_user[100];                     /* 数据库的用户名 */
    char sql_passwd[100];                   /* 数据库用户的密码 */
    char sql_name[100];                     /* 数据库名 */
};

#endif