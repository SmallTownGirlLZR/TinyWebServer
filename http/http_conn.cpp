#include "http_conn.h"
#include <fstream>
#include <mysql/mysql.h>

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form =
    "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form =
    "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form =
    "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form =
    "There was an unusual problem serving the request file.\n";

locker m_lock;
std::map<std::string, std::string> users;

/* 取出mysql 中 表user 中的 用户名密码映射 */
void http_conn::initmysql_result(connection_pool *connPool) {
    MYSQL *mysql;
    connectionRAII mysqlcon(&mysql, connPool);     /* 从数据连接池中取出一个MYSQL句柄 */

    if (mysql_query(mysql, "SELECT username,passwd FROM user")) {
        LOG_ERROR("SELECT err : %s\n", mysql_error(mysql));
    }

    MYSQL_RES *result = mysql_store_result(mysql);

    int num_fields = mysql_num_fields(result);

    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    /* typedef char** MYSQL_ROW */
    while (MYSQL_ROW row = mysql_fetch_row(result)) {   /* mssql_fetch_row 获取一行数据并且向下移动光标 */
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        users[temp1] = temp2;
    }
}

int setnonblocking(int fd) {
  int old = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, old | O_NONBLOCK);

  return old;
}

void addfd(int epollfd, int fd, bool one_shot, int TRIGMode) { /* 是否 ET */
  epoll_event event;
  event.data.fd = fd;

  if (TRIGMode == 1) /* EPOLLRDHUP 高效检测对端是否关闭连接 */
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
  else
    event.events = EPOLLIN | EPOLLRDHUP;

  if (one_shot) /* EPOLLONESHOT 保证只有一个线程收到通知 */
    event.events |= EPOLLONESHOT;

  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

/* 重置为EPOLLONESHOT */
void modfd(int epollfd, int fd, int ev, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode) /* ET */
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

bool http_conn::add_response(const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);

    int n = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - m_write_idx - 1, format,arg_list);

    if (n >= WRITE_BUFFER_SIZE - m_write_idx - 1) {
        va_end(arg_list);
        return false;
    }

    m_write_idx += n;

    va_end(arg_list);

    LOG_INFO("request: %s", m_write_buf); /* m_write_buf 的全部内容 */
    return true;
}

/*
    m_user_count 和 m_epollfd 是static 变量
    为多个用户所共享
    此两变量属于类，而不属于某个具体的对象(用户)
*/
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/*
    关闭连接，客户总量减少1
    一个用户，一个http_conn类
*/
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const struct sockaddr_in &addr, char *root,
                     int TRIGMode, int close_log, std::string user,
                     std::string passwd, std::string sqlname) {
  m_sockfd = sockfd;
  m_address = addr;
  addfd(m_epollfd, m_sockfd, true, m_TRIGMode);
  m_user_count++;

  doc_root = root;
  m_TRIGMode = TRIGMode;
  m_close_log = close_log;

  strcpy(sql_user, user.c_str());
  strcpy(sql_passwd, passwd.c_str());
  strcpy(sql_name, sqlname.c_str());
  init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init() {
  mysql = NULL;
  bytes_to_send = 0;
  bytes_have_send = 0;
  m_check_state = CHECK_STATE_REQUESTLINE; /*主状态机的初始状态*/
  m_linger = false;
  m_method = GET;
  m_url = 0;
  m_version = 0;
  m_content_length = 0;
  m_host = nullptr;
  m_start_line = 0;
  m_checked_idx = 0;
  m_read_idx = 0;
  m_write_idx = 0;
  cgi = 0;
  m_state = 0;
  timer_flag = 0;
  improv = 0;

  memset(m_read_buf, '\0', READ_BUFFER_SIZE);
  memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
  memset(m_real_file, '\0', FILENAME_LEN);
}

/*  HTTP响应报文 状态行格式
    HTTP-Version Status-Code Reason-Phrase CRLF
    HTTP/1.1 200 OK
    HTTP/1.1 404 Not Found
*/

bool http_conn::add_status_line(int status, const char *title) {
  return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/*
    headers
*/
bool http_conn::add_headers(int content_len) {
  return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
  return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_content_type() {
  return add_response("Content-Type:%s\r\n", "text/html");
}

/* 是否长连接 */
bool http_conn::add_linger() {
  return add_response("Connection:%s\r\n",
                      (m_linger == true) ? "keep-alive" : "close");
}
/*
    blank_line
*/
bool http_conn::add_blank_line() { return add_response("%s", "\r\n"); }
/* response body */
bool http_conn::add_content(const char *content) {
  return add_response("%s", content);
}

/* 从状态机 */
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; m_checked_idx++) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if (m_checked_idx + 1 == m_read_idx) {
                return LINE_OPEN;
            } /* \r 是最后一个字符，需要继续读取 */
            if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = 0;
                m_read_buf[m_checked_idx++] = 0;
                return LINE_OK;
            }
        return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_idx >= 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } /*
         第一次发：GET / HTTP/1.1\r
         第二次发: /n
         所以需要向前检测是否有 \r 出现
     */
    }
  return LINE_OPEN; /* 没有发现 \r和\n */
}

/*
    可处理 ET LT 模式
*/
bool http_conn::read_once() {
  if (m_read_idx >= READ_BUFFER_SIZE)
    return false;
  int bytes_read = 0;

  if (m_TRIGMode == 0) { /* LT 模式*/
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                      READ_BUFFER_SIZE - m_read_idx - 1, 0);
    m_read_idx += bytes_read;

    if (bytes_read <= 0)
      return false;

    return true;
  } else { /* ET 模式 */
    /* ET 只触发一次 所以必须循环读取 */
    while (true) {
      bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                        READ_BUFFER_SIZE - m_read_idx, 0);

      if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) /* 暂无数据可读 */
          break;
        return false;
      } else if (bytes_read == 0) { /* 客户端关闭了链接 客户端发送FIN包 */
        return false;
      }

      m_read_idx += bytes_read;
    }
    return true;
  }
}
/*
    解析HTTP 请求行
    当主状态机为 CHECK_STATE_REQUESTLINE 时调用
    HTTP请求行格式：GET /a/b/c HTTP/1.1\r\n
    从状态机将\r\n替换为 \0\0
*/
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
  /*
      char *strpbrk(const char *s, const char *accept);
      The strpbrk() function locates the first occurrence in the string s of any
     of the bytes in the string accept.
  */
  m_url = strpbrk(text, " \t"); /* 在浏览器的请求行中寻找第一个空格和制表符 */
  if (!m_url) {
    return BAD_REQUEST;
  }

  *m_url++ = '\0';     /* 空格转化为 \0 以获取method */
  char *method = text; /* 请求方法后的空格已经被改为 \0 */
  if (strcasecmp(method, "GET") ==
      0) { /*  The  strcasecmp()  function performs a byte-by-byte comparison of
              the strings s1 and s2, ignoring the case of the characters.*/
    m_method = GET;
  } else if (strcasecmp(method, "POST") == 0) {
    m_method = POST;
    cgi = 1; /* 在POST 方法时设置 */
  } else {
    return BAD_REQUEST;
  }

  /*
      The strspn() function calculates the length (in bytes)
       of the initial segment of s which consists entirely of bytes in accept
  */
  m_url += strspn(m_url, " \t");     /* 去除前导空格和\t */
  m_version = strpbrk(m_url, " \t"); /* 寻找第一个制表符或空格 */

  if (!m_version)
    return BAD_REQUEST;
  *m_version++ = '\0';                   /* 将m_url的后缀空格改为 \0 */
  m_version += strspn(m_version, " \t"); /* 去除前导空格和\t */

  if (strncasecmp(m_url, "http://", 7) == 0) {
    m_url += 7;                 /* 去除前缀http */
    m_url = strchr(m_url, '/'); /* /前还有域名主机名端口号等信息 */
  }

  if (strncasecmp(m_url, "https://", 8) == 0) {
    m_url += 8;                 /* 去除前缀https */
    m_url = strchr(m_url, '/'); /* /前还有域名主机名端口号等信息 */
  }

  if (!m_url || m_url[0] != '/')
    return BAD_REQUEST;

  if (strlen(m_url) == 1) { /* / 根目录 */
    strcat(m_url, "judge.html");
  }
  /* 驱动状态机的状态变化 */
  m_check_state = CHECK_STATE_HEADER;
  return NO_REQUEST;
}

/*
    状态为 CHECK_STATE_HEADER 时调用
    解析请求头
    该函数需要多次调用，每次解析一行
*/
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] =='\0'){ /* 不意味 请求头没有任何内容，而是解析到了请求头的末尾（空行）*/
        if (m_content_length != 0){ /* 请求头之后还有请求体 */
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST; /* Keep Going 继续处理请求体 */
        }
        return GET_REQUEST; /* 无请求体，完成HTTP报文解析 */
    }
    else if (strncasecmp(text, "Connection:", 11) == 0){
        text += 11;
        text += strspn(text, " \t");
        if (strncasecmp(text, "keep-alive", 10) == 0){
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else{
        LOG_INFO("Unknow header : %s", text);
    }
    return NO_REQUEST; /* Keep Going 读取header的下一行 */
}

http_conn::HTTP_CODE http_conn::parse_content(char *text) {
  /*
       m_checked_idx指向请求体的第一个字符，而不扫描请求体
       Body 可以是任何东西
     可能偶然出现\r\n，所以一般用content_length描述请求体的最后位置
       if()检测是否报文已经完全读入，如果m_read_idx较小，说明还有部分请求体没有读入
  */
  if (m_read_idx >=
      m_checked_idx + m_content_length) { /*m_checked_idx + m_content_length
                                             即请求体的最后位置*/
    text[m_content_length] = '\0';
    m_string = text;
    return GET_REQUEST; /* Finish */
  }

  return NO_REQUEST; /* Keep-Going */
}

/* 主状态机 */
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||(line_status = parse_line()) == LINE_OK) {
        text = get_line(); /* parse_line 函数把\r\n 改为 \0\0 */
        m_start_line = m_checked_idx; /* 更新当前解析行，parse_line调用后,m_checked_idx指向下一行
                                   */
        LOG_INFO("%s", text);
        switch (m_check_state) {
        case CHECK_STATE_REQUESTLINE: {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER: {
            ret = parse_headers(text);
            if(ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST) {
            /* 没有请求体 */
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT: {
            ret = parse_content(text);
            if (ret == GET_REQUEST) {
                return do_request();
            }
            line_status = LINE_OPEN; /* parse_content没有返回GET_REQUEST，则说明还有数据要继续读取*/
            break;
        }
    }
  }
  return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root); /* 拷贝网站根目录 */
    int len = strlen(doc_root);

    const char *p = strrchr(m_url, '/'); /* 寻找最后一个 / */
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        char flag = m_url[1];

        /* 将/1 /2 /3 这种简易的url转为真正的文件路径 */
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2); /* 原始： /2action -> /action*/
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        /*
         * user=123&passwd=123
           提取账户密码
        */

        char name[100], password[100];
        int i = 0, j = 0;
        for (i = 5; m_string[i] != '&'; i++)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';
        for (i = i + 10; m_string[i] != '\0'; i++)
            password[j++] = m_string[i];
        password[j] = '\0';

        /* 注册 */
        if (*(p + 1) == '3')
        {
            /* 检测数据库中是否有重名 */
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username,passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "','");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(std::pair<std::string, std::string>(name, password));
                m_lock.unlock();

                if (!res)
                {
                    strcpy(m_url, "/log.html"); /* 此前的m_url已经拷贝至m_url_real
                                              并连接到 m_real_file 中*/
                }
                else
                {
                    strcpy(m_url, "/registerError.html");
                }
            }
            else
            {
                strcpy(m_url, "/registerError.html");
            }
        }
        else if (*(p + 1) == '2')
        {
            /* 登录 */
            if (users.find(name) != users.end() && users[name] == password)
            {
                strcpy(m_url, "/welcome.html");
            }
            else
            {
                strcpy(m_url, "/logError.html");
            }
        }
    }

    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
    {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    /* 目录 */
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    int fd = open(m_real_file, O_RDONLY);

    /* 将磁盘级别的文件 映射到内存中 成为内存级别的文件*/
    m_file_address =
        (char *)mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
}
/* 取消文件和内存间的映射关系 */
void http_conn::unmap() {
  if (m_file_address) { /* 内存映射的起始地址 */
    munmap(m_file_address, m_file_stat.st_size);
    m_file_address = nullptr;
  }
}

bool http_conn::write() {
    
    int temp = 0;
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (true) {
        /* 聚集写 */
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp < 0) {
        if (errno == EAGAIN) { /* 缓冲区被写满，不再允许写入 */
            modfd(m_epollfd, m_sockfd, EPOLLOUT,m_TRIGMode); /* EPOLLOUT 下次缓冲区有剩余空间时，epoll通知 */
            return true;
        }
        unmap();
        return false;
    }

    bytes_have_send += temp;
    bytes_to_send -= temp;
    /* 发送的数据总量 超过报文头部的长度
        header已经全部发送完毕
        body可能已经发送一部分
    */
    if (bytes_have_send >= m_iv[0].iov_len) {
        m_iv[0].iov_len = 0; /* 重置长度，下次writev 不用再写入header */

        /* 重置body 偏移量
             bytes_have_send - m_write_idx 已发送数据 -
            header长度(header储存在m_write_buf)中 bytes_have_send - m_write_idx
             即为已经发送的body长度
        */
        m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
        m_iv[1].iov_len = bytes_to_send;

    }else { /* header 没有发送完毕 */
      /* 源码中的错误写法：
           m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
      */
        m_iv[0].iov_len = m_write_idx - bytes_have_send;
        m_iv[0].iov_base = m_write_buf + bytes_have_send;
    }

    if (bytes_to_send <= 0) {
        unmap();
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

        if (m_linger) {
            init();
            return true;
    }else {
        return false;
    }
    }
  }
}

bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
    case INTERNAL_ERROR: {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form)){
            LOG_INFO("process_write: return  false %s","INTERNAL_ERROR");
            return false;
        }
        break;
    }
    case BAD_REQUEST: {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form)){
            LOG_INFO("process_write: return  false %s","BAD_REQUEST");
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST: {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form)){
            LOG_INFO("process_write: return  false %s","FORBIDDEN_REQUEST");
            return false;
        }
        break;
    }
    case FILE_REQUEST: {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0) {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;

            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;

            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        } else {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                LOG_INFO("process_write: return  false %s","FILE_REQUEST");
                return false;
            }
        break;
    }
    default:{ 
        LOG_INFO("process_write: return  false %s","default");
        return false;
    }
  }

    /* 没有body 需要发送*/
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process() {
    HTTP_CODE read_ret = process_read();    /* 主状态机 */
    if (read_ret == NO_REQUEST){          /* Keep-Going 继续读取 */
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret); /* 处理需要写入的数据 (没有进行数据写入 ) */
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT,m_TRIGMode); /* 唤醒主线程的epoll_wait 提示主线程可以开始写操作 */
}
