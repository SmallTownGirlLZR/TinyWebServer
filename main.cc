#include "config.h"

int main(int argc, char* argv[]){
    std::string user = "root";
    std::string passwd = "root";
    std::string databasename = "myDB";

    /* 命令行解析 */
    Config config;
    config.parse_arg(argc,argv);

    WebServer server;
    
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    

    //日志
    server.log_write();

    //数据库
    server.sql_pool();

    //线程池
    server.threadpool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}
