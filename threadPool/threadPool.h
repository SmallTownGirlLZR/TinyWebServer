#ifndef THREADPOOL
#define THREADPOOL

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template<typename T>        /* 本项目中 T -> http_conn* */ 
class threadPool{
private:
    int m_thread_number;        /* 线程池的总线程数 */
    int m_max_request;          /* 请求队列中允许的最大请求数 */
    pthread_t* m_threads;       /* 线程数组，记录线程池中的线程id */
    std::list<T*> m_workqueue;  /* 请求队列 */
    locker m_queuelocker;       /* 保护请求队列的互斥锁 */
    sem m_queuestat;             /* 是否有任务需要处理 信号量 */
    connection_pool* m_connPool; /* 数据库 */
    int m_actor_model;           /* 模式切换 */
private:
    static void *worker(void *arg);     /* 线程调度函数 */
    void run();
public:
    threadPool(int actor_model,connection_pool* connPool,int thread_number = 8,int max_request = 10000);
    ~threadPool();
    bool append(T* request,int state);
    bool append_p(T* request);
};


template <typename T>
threadPool<T>::threadPool(int actor_model,connection_pool* connPool,int thread_number, int max_request)
    :m_actor_model(actor_model), m_connPool(connPool),
     m_thread_number(thread_number),m_max_request(max_request){
    if(thread_number <=0 || max_request <= 0){
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }

    for(int i = 0; i < m_thread_number; i++){
        if(pthread_create(m_threads + i,nullptr,worker,this)){
            delete[] m_threads;
            throw std::exception();
        }
        /* 父线程和子线程解除关联，父线程可以不调用wait */
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }

}


template<typename T>
threadPool<T>::~threadPool(){
    delete[] m_threads;
}

template<typename T>
bool threadPool<T>::append(T* request, int state){
    m_queuelocker.lock();

    if(m_workqueue.size() >= m_max_request){
        m_queuelocker.unlock();
        return false;
    }
    request -> m_state = state;     /* m_state = 0 代表可以读取 m_state = 1 代表可以写入 */
    m_workqueue.push_back(request);
    
    m_queuelocker.unlock();
    m_queuestat.post();     /* post放在unlock之外，避免惊群效应 */
    return true;
}

template<typename T>
bool threadPool<T>::append_p(T* request){
    m_queuelocker.lock();

    if(m_workqueue.size() >= m_max_request){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();

    m_queuestat.post();
    return true;
}

template<typename T>
void* threadPool<T>::worker(void* arg){
    threadPool* pool = static_cast<threadPool*>(arg);
    pool -> run();
    return pool;
}

template <typename T>
void threadPool<T>::run()
{
    while (true)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();     /* 已经取出共享队列中的数据，下文对单个节点的操作不需要加锁 */

        if (!request)
            continue;
        if (m_actor_model == 1){    /* Reactor */
            if (request->m_state == 0){     /* m_state 为 0 代表当前socket 可以进行读取 */
                if (request->read_once()){
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();     /* process_read() + process_write() */
                }else{
                    request->improv = 1; 
                    request->timer_flag = 1;
                }
            }else{      /* m_state 为 1 代表当前socket 可以进行写入 */
                if (request->write()){
                    request->improv = 1;
                }else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }else{      /* Proactor */
            /* 主线程进行读写，工作线程只负责业务处理 */
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}


#endif
