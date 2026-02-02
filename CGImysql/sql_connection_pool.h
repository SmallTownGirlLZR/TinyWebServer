#ifndef POOL
#define POOL
#include <mysql/mysql.h>
#include <string>
#include "../lock/locker.h"
#include <list>


class connection_pool{
public:
    MYSQL* GetConnection();     /* 在连接池中获取数据库连接 */
    bool ReleaseConnection(MYSQL* con);   /* 释放连接，归还于连接池 */
    int GetFreeConn();          /* 获取连接 */
    void DestroyPool();         /* 销毁连接池中的所有连接 */

    static connection_pool* GetInstance();      /* 实现单例模式 */

    void init(const std::string& url,const std::string& User,
              const std::string& passwd,const std::string DateBaseName,
              int port,int MaxConn,int close_log);

private:
    connection_pool();  /* 单例模式 */
    ~connection_pool(); /* 局部静态变量 在程序结束时自动调用析构函数 */

    int m_MaxConn;      /* 最大连接数 */
    int m_CurConn;      /* 当前已使用连接数 */
    int m_FreeConn;     /* 连接池中空闲连接数 */

    locker lock;                     /* 互斥锁 */
    std::list<MYSQL* > connList;     /* 连接池容器 */
    sem reserve;                     /* 信号量 */

public:
    std::string m_url;              /* IP */
    std::string m_Port;             /* 数据库端口号 */
    std::string m_User;             /* 用户名 */
    std::string m_PassWord;         /* 密码 */
    std::string m_DataBaseName;     /* 数据库名称 */
    int         m_close_log;        /* 日志开关 */
    
};
#endif