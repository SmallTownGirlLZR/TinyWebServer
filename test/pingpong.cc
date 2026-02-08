#include <iostream>
#include <pthread.h>

class pingPong{
public:
    pingPong(){
        pthread_mutex_init(&mutex,nullptr);
        pthread_cond_init(&cond,nullptr);
        whoTurn = 1;
    }
    
    static void* printOne(void* arg){
        pingPong* self = static_cast<pingPong*>(arg);
        for(int i = 0;i < 5; i++){
            pthread_mutex_lock(&self -> mutex);
            while(self -> whoTurn != 1){
                pthread_cond_wait(&self -> cond,&self -> mutex);
            }

            std::cout << 1 << " ";
            self -> whoTurn = 2;
            pthread_cond_broadcast(&self -> cond);
            pthread_mutex_unlock(&self -> mutex);
        }
        return nullptr;
    }

    static void* printTwo(void* arg){
        pingPong* self = static_cast<pingPong*>(arg);
        for(int i = 0;i < 5; i++){
            pthread_mutex_lock(&self -> mutex);
            while(self -> whoTurn != 2){
                pthread_cond_wait(&self -> cond,&self -> mutex);
            }

            std::cout << 2 << " ";
            self -> whoTurn = 3;
            pthread_cond_broadcast(&self -> cond);
            pthread_mutex_unlock(&self -> mutex);
        }

        return nullptr;
    }

    static void* printThree(void* arg){
        pingPong* self = static_cast<pingPong*>(arg);
        for(int i = 0;i < 5; i++){
            pthread_mutex_lock(&self -> mutex);
            while(self -> whoTurn != 3){
                pthread_cond_wait(&self -> cond,&self -> mutex);
            }

            std::cout << 3 << std::endl;
            self -> whoTurn = 1;
            pthread_cond_broadcast(&self -> cond);
            pthread_mutex_unlock(&self -> mutex);
        }

        return nullptr;
    }
    
private:
    pthread_mutex_t mutex;      /* 互斥锁 */
    pthread_cond_t cond;        /* 条件变量 */
    int whoTurn;
};


int main(){
    pingPong p;
    pthread_t t1,t2,t3;
    pthread_create(&t1,nullptr,p.printOne,&p);
    pthread_create(&t2,nullptr,p.printTwo,&p);
    pthread_create(&t3,nullptr,p.printThree,&p);
    

    pthread_join(t1,nullptr);
    pthread_join(t1,nullptr);
    pthread_join(t1,nullptr);
}