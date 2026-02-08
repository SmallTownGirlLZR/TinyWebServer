#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>

Log::Log(){
    m_count = 0;
    m_is_async = false;
}

Log::~Log(){
    if(m_fp){
        fclose(m_fp);
    }
}

void Log::flush(void){
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();
}

bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
    /* 设置了阻塞队列 即可以完成异步写入日志 */
    if(max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new block_queue<std::string>(max_queue_size);
        /* 创建消费者线程 */
        pthread_t id;
        pthread_create(&id,nullptr,flush_log_thread,nullptr);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_split_lines = split_lines;
    m_buf = new char[m_log_buf_size];
    memset(m_buf,0,sizeof(m_buf));

    /* 时间处理 */
    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char* p = strrchr(file_name,'/');     /* 寻找最后一个 '/' 路径分隔符 */
    char log_full_name[256] = {0};
    if(p == nullptr){   // "a.log"
        snprintf(log_full_name,sizeof(log_full_name) - 1,"%d_%02d_%02d_%s",my_tm.tm_year,my_tm.tm_mon + 1,my_tm.tm_mday,file_name);
    }else{      // "/home/Lin/b.log"
        strcpy(log_name,p + 1);
        strncpy(dir_name,file_name,p - file_name + 1);  /* 把最后一个 '/' 也加入 */
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }
    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name,"a");
    if(m_fp == nullptr){
        return false;
    }
    return true;
}

void Log::write_log(int level,const char* format, ...){
    struct timeval now = {0,0};
    gettimeofday(&now,nullptr);

    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    /* log level */
    char s[16] = {0};
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    m_mutex.lock();
    m_count++;
    /* Log Rotation */
    if(my_tm.tm_mday != m_today || m_count % m_split_lines == 0){
        char tail[16] = {0};
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);   /* close old lop file */

        snprintf(tail,sizeof(tail) - 1,"%d_%02d_%02d_",my_tm.tm_year + 1900,my_tm.tm_mon + 1,my_tm.tm_mday);

        if(my_tm.tm_mday != m_today){
            snprintf(new_log,sizeof(new_log) - 1,"%s%s%s",dir_name,tail,log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;    /* record number of log on one day*/
        }else if(m_count % m_split_lines == 0){
            snprintf(new_log,sizeof(new_log)-1,"%s%s%s.%llf",dir_name,tail,log_name,m_count / m_split_lines);
        }
        fopen(new_log,"a");
    }
    m_mutex.unlock();

    va_list valst;
    va_start(valst,format);
    
    std::string logstr;
    m_mutex.lock();
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    
    m_buf[m + n] = '\n';
    m_buf[m + n + 1] = '\0';
    logstr = m_buf;

    m_mutex.unlock();
    /* async */
    if(m_is_async && !m_log_queue -> full()){
        m_log_queue->push(logstr);      /* product */
    }else{
        m_mutex.lock();
        fputs(logstr.c_str(),m_fp);
        m_mutex.unlock();
    }
    va_end(valst);

}