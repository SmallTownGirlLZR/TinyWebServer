#include "sql_connection_pool.h"

connection_pool* connection_pool::GetInstance(){
    static connection_pool instance;    /* 局部静态变量，在程序结束时调用析构函数 */
    return &instance;
}

connection_pool::connection_pool()
    :m_FreeConn(0),m_MaxConn(0)
{}

void connection_pool::init(const std::string& url,const std::string& User,
              const std::string& passwd,const std::string DateBaseName,
              int port,int MaxConn,int close_log){
    m_url = url;
    m_Port = port;
    m_User = User;
    m_PassWord = passwd;
    m_DataBaseName = DateBaseName;
    m_close_log = close_log;
    
    /* 向连接池内插入Mysql连接 */
    for(int i = 0; i < MaxConn; i++){
        MYSQL* con = nullptr;
        con = mysql_init(con);

        if(con == nullptr){
            // log ... todo 创建失败
            exit(1);
        }

        /* 连接 */
        con = mysql_real_connect(con,url.c_str(),User.c_str(),passwd.c_str(),
                DateBaseName.c_str(),port,nullptr,0);
        if(con == nullptr){
            // log ... todo 连接失败
            exit(1);
        }

        lock.lock();
        
        connList.push_back(con);    /* 这里通常可以不加锁，因为Init工作一般在主线程完成，在创建其他线程前完成*/

        lock.unlock();
        ++m_FreeConn;
    }

    reserve = sem(m_FreeConn);  /* 信号量初始值为连接数 */

    m_MaxConn = m_FreeConn;
}

MYSQL* connection_pool::GetConnection(){
    MYSQL* con = nullptr;
    reserve.wait();     /* P操作 -- */

    lock.lock();

    con = connList.back();
    connList.pop_back();
    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return con;

    /* 如果把wait放入锁中会导致死锁 */
}

bool connection_pool::ReleaseConnection(MYSQL* con){
    if(con == nullptr)      return false;

    lock.lock();

    connList.push_back(con);
    m_FreeConn++;
    m_MaxConn--;

    lock.unlock();

    reserve.post();     /* 加锁并完成插入之后再post */

    return true;
}

int connection_pool::GetFreeConn(){
    return m_FreeConn;
}

void connection_pool::DestroyPool(){
    lock.lock();
    if(connList.size() > 0){
        std::list<MYSQL*>::iterator it;
        for(it = connList.begin();it != connList.end();it++){
            MYSQL* conn = *it;
            mysql_close(conn);
        }
        m_FreeConn = 0;
        m_CurConn = 0;
        connList.clear();
    }
    lock.unlock();
}
connection_pool::~connection_pool(){
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}