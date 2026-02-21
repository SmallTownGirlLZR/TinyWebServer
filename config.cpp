#include "config.h"
#include <unistd.h>

Config::Config()
    :PORT(9906), LOGWrite(0),/* 0 为同步模式 */
    TRIGMode(0),/* 默认listen LT +  connfd LT */
    LISTENTrigmode(0),CONNTrigmode(0),
    OPT_LINGER(0),  /* 默认不使用优雅关闭连接 */
    sql_num(8),thread_num(8),close_log(0),actor_model(0)/* 默认Proactor */
{}

void Config::parse_arg(int argc,char* argv[]){
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";
    while((opt = getopt(argc ,argv, str)) != -1){
        switch(opt){
        case 'p':{
            PORT = atoi(optarg);
            break;
        }
        case 'l':{
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':{
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':{
            OPT_LINGER = atoi(optarg);
            break;
        }case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':{
            thread_num = atoi(optarg);
            break;
        }
        case 'c':{
            close_log = atoi(optarg);
            break;
        }
        case 'a':{
            actor_model = atoi(optarg);
            break;
        }
        default:{
            break;
        }
    }
    }
}
