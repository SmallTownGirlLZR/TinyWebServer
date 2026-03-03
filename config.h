#ifndef CONFIG_H
#define CONFIG_H

#include "webServer.h"

class Config{
public:
    Config();
    ~Config(){}

    void parse_arg(int argc,char* argv[]);

    int PORT;
    int LOGWrite;   /* 日志写入方式 */
    int TRIGMode;   /* 触发组合方式 */

    int LISTENTrigmode;
    int CONNTrigmode;

    int OPT_LINGER;     

    int sql_num;        /* 数据库连接池数目 */

    int thread_num;
    int close_log;

    int actor_model;    /* Proactor / Reactor */


};


#endif
