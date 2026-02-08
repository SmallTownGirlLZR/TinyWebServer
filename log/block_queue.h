#ifndef BLOCK_QUEUE
#define BLOCK_QUEUE

#include "../lock/locker.h"
#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>


template<typename T>
class block_queue{
public:
    block_queue(int max_size = 1000){
        if(max_size <= 0){
            exit(-1);
        }
        m_array = new T[max_size];

        m_max_size = max_size;
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear(){
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue(){
        m_mutex.lock();
        if(m_array){
            delete[] m_array;
        }
        m_mutex.unlock();
    }

    bool full(){
        m_mutex.lock();
        if(m_size >= m_max_size){
            m_mutex.unlock();   /* 防止死锁 */
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool empty(){
        m_mutex.lock();
        if(m_size == 0){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool front(T& val){
        m_mutex.lock();
        if(m_size == 0){
            m_mutex.unlock();
            return false;
        }
        val = m_array[m_front];
        m_mutex.unlock();
        return true;
    }

    bool back(T& val){
        m_mutex.lock();
        if(m_size == 0){
            m_mutex.unlock();
            return false;
        }
        val = m_array[back];
        m_mutex.unlock();
        return true;
    }
    int size(){
        int temp = 0;
        m_mutex.lock();
        temp = m_size;
        m_mutex.unlock();

        return temp;
    }

    int max_size(){
        int temp = 0;
        m_mutex.lock();

        temp = m_max_size;

        m_mutex.unlock();
        return temp;
    }

    bool push(const T& item){
        m_mutex.lock();
        if(m_size >= m_max_size){
            m_cond.broadcast();     /* 唤醒消费者 */
            m_mutex.unlock();
            return false;
        }
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;

        m_cond.broadcast();     /* 唤醒消费者 */
        m_mutex.unlock();
    }

    bool pop(T& item){
        m_mutex.lock();
        
        /* 必须使用while */
        while(m_size <= 0){
            if(!m_cond.wait(m_mutex.get())){        /* wait 1. 解锁 2.休眠 3.被唤醒 + 抢锁*/
                                                    /*只有抢到了锁，pthread_cond_wait 函数才会返回，线程才会继续往下执行。*/
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;

        m_mutex.unlock();
        return true;
    }

    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;

    T* m_array;
    int m_size;
    int m_max_size;
    int m_front;    /* 指向第一个元素的前一个元素*/
    int m_back      /* 指向最后一个元素*/
};
#endif