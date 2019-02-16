#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <bitset>
#include <iostream>
#include <cstring>

#define THREAD_COUNT 1

void bin(unsigned n,char* b,int* i) 
{ 
    if (n > 1) 
    bin(n>>1,b,i); 
    b[(*i)++] = n&1;  
    //printf("%d", n & 1); 
} 
class SemArray{
	private:
		int semset[16];
		pthread_mutex_t lock;
		pthread_cond_t cv[16];	
	public:
		SemArray(){
			for(int i=0;i<16;i++)
				semset[i] = 1;
			pthread_mutex_init(&lock,NULL);
			for(int i=0;i<16;i++)
				pthread_cond_init(&cv[i],NULL);
		}
		void signal(short set){
			char bset[4][4];
			for(int i=0;i<4;i++,set/=10){
				int j=0;
				bin(set %10,bset[i],&j);
			}
			pthread_mutex_lock(&lock);
				for(int i=3;i>=0;i--)
					for(int j=3;j>=0;j--)
						if(bset[i][j]){
							semset[(3-i)*4 + (3-j)]++;
							pthread_cond_signal(&cv[(3-i)*4 + (3-j)]);
						}
			pthread_mutex_unlock(&lock);

		}
		void wait(short set){
			char bset[4][4];
			for(int i=0;i<4;i++,set/=10){
				int j=0;
				bin(set %10,bset[i],&j);
			}
			pthread_mutex_lock(&lock);
			for(int i=0;i<4;i++){
				for(int j=0;j<4;j++){
					while(bset[i][j] && semset[(3-i)*4 + (3-j)]==0)
						pthread_cond_wait(&cv[(3-i)*4 + (3-j)],&lock); 
				}
			}

			for(int i=0;i<16;i++)
				if(bset[i][j]) semset[(3-i)*4 + (3-j)]--;
			pthread_mutex_unlock(&lock);
		}

};

SemArray sem_array;

void* run(void* param){
	sem_array.signal(1017);
	sem_array.wait(1017);
}

int main(){
	
	pthread_t threads[THREAD_COUNT];
	for(int i=0;i<THREAD_COUNT;i++)
		pthread_create(&threads[i],NULL,run,NULL);
	sleep(5);
	return 0;
}