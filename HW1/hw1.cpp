#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>

#include <cstdio>
#include <cstdlib>
#define _OPEN_THREADS
#include <csignal>
#include <ctime>
#include <string>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <mutex>

#define MAX_CLIENT 1000000
#define BUF_SIZE 1024

#define PARENT(x) (x-1)/2
#define LEFT(x) 2*x + 1
#define RIGHT(x) 2*x + 2

#define SEMNAME "/tool"


enum status{
	REST,
	REQUEST,
	QUIT
};

typedef struct share{
	double share_;
	pid_t pid_;
	double start_time_;
	status* state;
} share_t;

typedef struct tool{
	double current_share_;
	pid_t user_pid_;
	pid_t tool_pid_;
	double start_time_;
	status current_customer_state;
} tool_t;

typedef struct allocate{
	sigset_t set;
	int ns;
	char* buff;
	share_t* share;
	
	int* available_tool;
} allocate_t;



typedef struct shared_info{
	double total_share; // Since total share will be expressed in ms diff,
	int number_of_customers;
	// All the customers those are in waiting list are in WAITING state by definition
	share_t waiting_list[MAX_CLIENT];
	int waiting_list_sz;

	tool_t tool_data[MAX_CLIENT];

	bool any_process_in_rest;
} shared_info_t;

// Create shared memory storage as suggested in the homework description.
shared_info_t* shared_info;
int min_usage,max_usage,k;
// Priority Queue routines
void heapify_up(share_t* list,int i)
{
	if(i && list[PARENT(i)].share_ > list[i].share_){
		std::swap(list[i],list[PARENT(i)]);
		heapify_up(list,PARENT(i));
	}
}
void heapify_down(share_t* list,int i,int size)
{
	int left = LEFT(i);
	int right = RIGHT(i);

	int smallest = i;

	if(left < size && list[left].share_ < list[i].share_)
		smallest = left;

	if(right < size && list[right].share_ < list[smallest].share_)
		smallest = right;

	if(smallest != i){
		std::swap(list[i],list[smallest]);
		heapify_down(list,smallest,size);
	}
}

void PQinsert(share_t* list,int& size,double share,pid_t pid, double start_time,status* state)
{
	if(size==MAX_CLIENT){
		printf("There are too many[%d] waiting clients in the gym already..\n",size);
		exit(-1);
		return;
	}
	int i=size;
	list[i].share_ = share;
	list[i].pid_ = pid;
	list[i].start_time_ = start_time;
	list[i].state = state;
	heapify_up(list,i);

	size++; 
}

void PQpop(share_t* list,int& size)
{
	if(size == 0){
		printf("There isn't any waiting client..\n");
		return ;
	}
	list[0] = list[size-1];
	size--;
	heapify_down(list,0,size);
}

share_t PQtop(share_t* list,int size)
{
	return list[0];
}

void PQerase(share_t* list, int& size,pid_t pid){
	share_t tmp[size-1];
	int j=0;
	for(int i=0;i<size;i++){
		if(list[i].pid_ == pid) continue;
		tmp[j++] = list[i];
	}
	size = 0;
	for(int l=0;l<j;k++)
		PQinsert(list,size,tmp[l].share_,tmp[k].pid_,tmp[l].start_time_,tmp[l].state);
}
// Priority Queue routines
pthread_mutex_t* wl_mutex;

// REQUEST runner thread
void* allocate_tool(void* arg){

	allocate_t* at = (allocate_t*) arg;
	int sig;
	pthread_sigmask(SIG_BLOCK, &(at->set), NULL);
	struct timeval  tv;
	gettimeofday(&tv, NULL);
	double current_time = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ;

	
	pthread_mutex_lock(wl_mutex);
	
	PQinsert(shared_info->waiting_list,shared_info->waiting_list_sz,at->share->share_,at->share->pid_,current_time,at->share->state); // Insert into waiting list
	bool mutex_taken = true;// Imagine a REST/QUIT occurs at this point.
	while(*(at->share->state)==REQUEST){
		while(*(at->share->state)==REQUEST && *(at->available_tool) == -1){
			if(mutex_taken == false){
				pthread_mutex_lock(wl_mutex);
				mutex_taken = true;
			}
			else printf("[%d-Runner thread]-> Have the lock\n",getpid());
			double usage = 0;
			for(int i=0;i<k;i++){
				if(shared_info->tool_data[i].user_pid_ == 0){// Means the tool is being used by noone
					share_t least_share_customer = PQtop(shared_info->waiting_list,shared_info->waiting_list_sz);
					if(least_share_customer.pid_ == getpid()){
						*(at->available_tool) = i;
						printf("[%d-Runner thread]->Tool %d is empty\n",getpid(),i);
						break;
					}
					
				}
				else{
					struct timeval  tv;
					gettimeofday(&tv, NULL);
					double current_time = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ;
					double used_time = current_time - shared_info->tool_data[i].start_time_;
					if(used_time < min_usage + 1 || shared_info->tool_data[i].start_time_ == 0) continue;
					// Find the tool with maximum current_share that has also bigger share than the current customer.
					double total_time = used_time + shared_info->tool_data[i].current_share_;
					share_t least_share_customer = PQtop(shared_info->waiting_list,shared_info->waiting_list_sz);
					if(least_share_customer.pid_ == getpid()){
						if(total_time - at->share->share_ > min_usage + 1 && total_time > usage){ usage = total_time; *(at->available_tool)= i;
						printf("[%d-Runner thread]->Tool %d had customer[%d] with share %lf, this customer has share %lf\n",getpid(),i,shared_info->tool_data[i].user_pid_,total_time,at->share->share_);}
					}
				}
			}
			if(*(at->available_tool) == -1 || shared_info->any_process_in_rest){
				mutex_taken = false;
				*(at->available_tool) = -1;
				pthread_mutex_unlock(wl_mutex);
				usleep(50);
			}
		}
		if(*(at->share->state)==REST || *(at->share->state) == QUIT){
			PQerase(shared_info->waiting_list,shared_info->waiting_list_sz,getpid());
			break;
		}
		//Any who comes down to here is the true winner of polling.
		
		kill(shared_info->tool_data[*(at->available_tool)].tool_pid_,SIGUSR1); // Inform the tool with maximum current share so that it can be prepared.	
		printf("[%d-Runner thread]->Waiting acknowledgement by tool process\n",getpid());
		
		sigwait(&(at->set),&sig); // Wait for acknowledgement by the tool.
		usleep(50);
		struct timeval  tv;
		gettimeofday(&tv, NULL);

		double current_time = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ;
		shared_info->tool_data[*(at->available_tool)].user_pid_ = at->share->pid_;
		shared_info->tool_data[*(at->available_tool)].start_time_ = current_time;
		shared_info->tool_data[*(at->available_tool)].current_share_ = at->share->share_;
		
		printf("[%d-Runner thread]->Customer started using the tool-%d[with pid: %d]..\n",getpid(),*(at->available_tool),shared_info->tool_data[*(at->available_tool)].tool_pid_);
		kill(shared_info->tool_data[*(at->available_tool)].tool_pid_,SIGUSR1);// Assign customer to the tool literally.
		
		//  Assign-ACK part.	
		bzero(at->buff,BUF_SIZE);
		std::ostringstream oss;
		oss<<"Customer with share "<<at->share->share_<<" is assigned to tool "<<*(at->available_tool)<<"."<<std::endl;
		std::string notification = oss.str();
		strcpy(at->buff,notification.c_str());
		send(at->ns,at->buff,strlen(at->buff)+1,0);

		mutex_taken = false;
		pthread_mutex_unlock(wl_mutex);
		// Update share part.
		
		gettimeofday(&tv, NULL);
		double start_time = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ;
		pid_t current_id = getpid();

		pthread_sigmask(SIG_BLOCK, &(at->set), NULL);
		
		sigwait(&(at->set),&sig); // Will take SIGUSR2 ignited by tool process. This denotes that tool has taken away from the customer.
		
		
		usleep(50);

		gettimeofday(&tv, NULL);
		double end_time = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ;
		int add = end_time - start_time;
		if(add > max_usage) add = max_usage;
		if(add < min_usage) add = min_usage + 1;
		at->share->share_ += add;// Update customer's share
		shared_info->total_share += add;	
		if(add!= max_usage && shared_info->waiting_list_sz){
			share_t least_share_customer = PQtop(shared_info->waiting_list,shared_info->waiting_list_sz);
			if(at->share->share_ < least_share_customer.share_){
				int diff = least_share_customer.share_ + 1 - at->share->share_;
				at->share->share_ += diff;
				shared_info->total_share += diff;
			}

		} 
		
		
		printf("[%d-Runner thread]-> Tool is returned after %d ms\n",getpid(),add);
		bool at_waiting_list = false;
		
		if(*(at->share->state) != REQUEST) printf("[%d-Runner thread]->Customer cannot be pushed to waiting list.\n",at->share->pid_);
		if(*(at->share->state) == REQUEST){
			printf("[%d-Runner thread]->Customer is pushed to waiting list.\n",at->share->pid_);
			PQinsert(shared_info->waiting_list,shared_info->waiting_list_sz,at->share->share_,at->share->pid_,end_time,at->share->state);
			at_waiting_list = true;
		}
		// ..
		// Leave-ACK part
		bzero(at->buff,BUF_SIZE);
		std::ostringstream pss;
		
		if(!at_waiting_list || add == max_usage){
			pss<<"Customer with share "<<at->share->share_<<" leaves tool "<<*(at->available_tool)<<"."<<std::endl;
			notification = pss.str();
			strcpy(at->buff,notification.c_str());
			send(at->ns,at->buff,strlen(at->buff)+1,0);
			printf("[%d-Runner thread]->%s\n",getpid(),at->buff);
			if(!at_waiting_list){
				*(at->available_tool) = -1;
				continue;
			}
		}
		else{
			share_t least_share_customer = PQtop(shared_info->waiting_list,shared_info->waiting_list_sz);
			pss<<"Customer with share "<<at->share->share_<<" removed from tool "<<*(at->available_tool)<<" due to a customer with "<<least_share_customer.share_<<std::endl;
			notification = pss.str();
			strcpy(at->buff,notification.c_str());
			send(at->ns,at->buff,strlen(at->buff)+1,0);
			printf("[%d-Runner thread]->%s\n",getpid(),at->buff);
		}
		
		kill(shared_info->tool_data[*(at->available_tool)].tool_pid_,SIGUSR1); // Tool process waits for correct output.
		pthread_sigmask(SIG_BLOCK, &(at->set), NULL);
		sigwait(&(at->set),&sig); // Will take SIGUSR2 ignited by tool process. This denotes that tool is ready for a new run.
		*(at->available_tool) = -1;
	}
	if(mutex_taken){
		mutex_taken = false; 
		pthread_mutex_unlock(wl_mutex);
	}
	printf("[%d-Runner thread]->Exiting thread..\n",getpid());
	*(at->available_tool) = -1;
}

// End REQUEST-specific threads
int main(int argc,char** argv){
	int sock,len,ns,nread;
	socklen_t plen;
	struct sockaddr_un saun,paun;
	struct sockaddr_in serverSa;
	struct ucred uc;
	char buff[BUF_SIZE];

	

	int shmid,wl_mutex_id;
	key_t shmkey,wl_mutex_key;
	int shmemsize;

	if(argc!=5){
		printf("Usage: ./hw1 <address> <min_limit> <max_limit> <tools>\n");
		exit(1);
	}
	min_usage = std::stoi(argv[2]);
	max_usage = std::stoi(argv[3]);
	k = std::stoi(argv[4]);
	
	
	
	shmemsize = sizeof(shared_info_t);
	shmid = shmget(shmkey, shmemsize, IPC_CREAT | 0666);
	wl_mutex_id = shmget(wl_mutex_key,sizeof(pthread_mutex_t),IPC_CREAT | 0666);
	// Note it is handled before any fork() call, hence all children will inherit corresponding
	// shmkey as is.
	shared_info = (shared_info_t*) shmat(shmid,NULL,0);
	wl_mutex = (pthread_mutex_t* ) shmat(wl_mutex_id,NULL,0);


	shared_info->number_of_customers = 0;
	shared_info->total_share = 0;
	shared_info->waiting_list_sz = 0;
	shared_info->any_process_in_rest = false;
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
	pthread_mutex_init(wl_mutex, &attr);
	if(argv[1][0]=='@'){
		printf("Unix domain socket is going to be created..\n");
		sock = socket(AF_UNIX,SOCK_STREAM,0);
		if(sock<0){
			perror(" server: socket");
			exit(1);
		}
		saun.sun_family = AF_UNIX;
    	strcpy(saun.sun_path, &argv[1][1]); // Exclude @ sign.

    	unlink(&argv[1][1]);
    	len = sizeof(struct sockaddr_un);
		if (bind(sock, (struct sockaddr *)&saun, len) < 0) {
	        perror("server: bind");
	        exit(1);
	    }
    	
	}
	else{
		printf("IPv4 socket is going to be created..\n");
		sock = socket(AF_INET,SOCK_STREAM,0);
		if(sock<0){
			perror(" server: socket");
			exit(1);
		}
		std::ostringstream pss;
		pss<<argv[1];
		std::string address = pss.str();
		size_t pos = address.find(":");
		std::string ip = address.substr(0,pos);
		std::string port = address.substr(pos+1);
		serverSa.sin_family = AF_INET;
  		serverSa.sin_addr.s_addr = inet_addr(ip.c_str()); 
  		serverSa.sin_port = htons(std::stoi(port));
  		if (bind(sock, (struct sockaddr *)&serverSa, sizeof(serverSa)) < 0) {
	        perror("server: bind");
	        exit(1);
	    }
	}

	

    ns=listen(sock,MAX_CLIENT);
    if (ns<0) {
    	perror("server: listen");
		exit(1);
    }
	
	//Create k process for each tool
	for(int i=0;i<k;i++){
		pid_t pid = fork();
		if(pid) continue;
		pid_t current_id = getpid();
		printf("Tool %d has pid: %d\n",i,current_id);
		shared_info->tool_data[i].current_share_ = 0; shared_info->tool_data[i].user_pid_ = 0;
		shared_info->tool_data[i].tool_pid_ = current_id;
		sigset_t set;
		int sig;
		sigemptyset(&set);
		sigaddset(&set,SIGUSR1);
		sigprocmask( SIG_BLOCK, &set, NULL);
		int another_user = -1;
		while(true){
			shared_info->tool_data[i].start_time_ = 0;
			shared_info->tool_data[i].current_share_ = 0;
			shared_info->tool_data[i].user_pid_ = 0;
			if(another_user == -1){
				printf("[Tool process-%d]-> Waiting any customer\n",i);
				sigwait(&set,&sig);
				usleep(50);
			}
			else
				another_user = -1;
			share_t least_share_customer = PQtop(shared_info->waiting_list,shared_info->waiting_list_sz);
			PQpop(shared_info->waiting_list,shared_info->waiting_list_sz);
			shared_info->tool_data[i].current_customer_state = REQUEST;

			printf("[Tool process-%d]-> Customer [%d] may use the tool..\n",i,least_share_customer.pid_);
			kill(least_share_customer.pid_,SIGUSR2); // Inform customer that it may use the tool
			sigprocmask( SIG_BLOCK, &set, NULL);
			sigwait(&set,&sig); // Wait user to get on the tool.
			
			printf("[Tool process-%d]-> Customer [%d] started using the tool..\n",i,least_share_customer.pid_);
			
			
			struct timespec ts;
			ts.tv_sec = 0;
			ts.tv_nsec = (min_usage-5) * 1000000;

			sigprocmask( SIG_BLOCK, &set, NULL);
			int is_rest = sigtimedwait(&set,NULL,&ts); // If there is a REST request from the customer, this will return a value other than -1.
			usleep(50);
			if(is_rest != -1){
				shared_info->tool_data[i].user_pid_ = 0;
				shared_info->tool_data[i].start_time_ = 0;
				shared_info->tool_data[i].current_share_ = 0;
				printf("[Tool process-%d]-> Customer [%d] with share %lf will leave the tool.\n",i,least_share_customer.pid_,least_share_customer.share_);
				kill(least_share_customer.pid_,SIGUSR2); // Acknowledge the agent process, that the tool is taken away from it.
				shared_info->any_process_in_rest = false;
				continue;
			}
			else
				printf("[Tool process-%d]-> Customer [%d] has passed the min limit\n",i,shared_info->tool_data[i].user_pid_);

				
			ts.tv_sec = 0;
			ts.tv_nsec = (max_usage - min_usage) * 1000000;
			
			sigprocmask( SIG_BLOCK, &set, NULL);
			another_user = sigtimedwait(&set,NULL,&ts); // If a SIGUSR1 is received, then it means another customer is
			//requesting to use the tool or user itself wanted to rest.
			if(another_user != -1){
				
				if(shared_info->tool_data[i].current_customer_state !=REQUEST){
					shared_info->tool_data[i].user_pid_ = 0;
					shared_info->tool_data[i].start_time_ = 0;
					shared_info->tool_data[i].current_share_ = 0;
					another_user = -1;
					printf("[Tool process-%d]-> Customer[%d] is leaving due to REST/QUIT\n",i,least_share_customer.pid_);
					kill(least_share_customer.pid_,SIGUSR2); // Acknowledge the agent process, that the tool is taken away from it.		
					shared_info->any_process_in_rest = false;
					continue;
				}
				
			}
			kill(least_share_customer.pid_,SIGUSR2); // Inform the customer that it is taken away from the tool.
			sigwait(&set,&sig); 
			printf("[Tool process-%d]-> Customer[%d] is informed\n",i,least_share_customer.pid_);
			kill(least_share_customer.pid_,SIGUSR2); // ACK customer that it can continue.	
		} 
	}
	
	// Master process
	plen = sizeof(struct sockaddr_un);
	// accept() system call extracts the first connection request on the queue and creates a new connected socket
	// and fills peer socket-related information into given arguments.
	
	while ((ns=accept(sock,(struct sockaddr *)&paun,&plen))>=0) {
	    //socklen_t val = sizeof(struct ucred);
	    //getsockopt(ns, SOL_SOCKET, SO_PEERCRED, &uc, &val);
	    
    	plen = sizeof(struct sockaddr_un);

    	pid_t pid = fork();
		if (pid) // Child process id
			continue;

		// Newly arrived customers will be on the resting state
		pid_t current_id = getpid();
		status stat = REST;
		shared_info->number_of_customers++; // Increment num of customers
		double customer_share = shared_info->total_share / (double)shared_info->number_of_customers; // Assign the average share

		shared_info->total_share += customer_share;
		share_t customer; customer.pid_ = current_id; customer.share_ = customer_share;
		customer.state = &stat;
		pthread_t tool_allocater = 0;
		/* Following is the Agent process session 
		 * with connection established */
		int available_tool = -1;
		printf("Connection established with %d\n",current_id);
		while (1) {
			bzero(buff,BUF_SIZE);
			nread=recv(ns,buff,BUF_SIZE,0);
			// if nread = 0, it means the peer has performed an orderly shutdown.
			if(nread == 0){
				stat = QUIT;
				printf("[%d] -> Leaving the GYM..\n",current_id);
				if(available_tool!=-1){
					shared_info->any_process_in_rest = true;
					printf("[%d] -> Sending QUIT\n",current_id);
					shared_info->tool_data[available_tool].user_pid_ = 0;
					shared_info->tool_data[available_tool].start_time_ = 0;
					shared_info->tool_data[available_tool].current_share_ = 0;
					shared_info->tool_data[available_tool].current_customer_state = REST;
					kill(shared_info->tool_data[available_tool].tool_pid_,SIGUSR1); // Send REST request to the corresponding tool.
				}
				PQerase(shared_info->waiting_list,shared_info->waiting_list_sz,current_id); // Remove the user from waiting list.
				shared_info->total_share -= customer.share_;
				shared_info->number_of_customers--;
				break;	
			} 
			else if (nread<0)
				exit(1);
			std::string request(buff);
			printf("[%d] -> Request: %s\n",current_id,request.c_str());
			if(request == "REQUEST"){
				char req_buff[BUF_SIZE];
				bzero(req_buff,BUF_SIZE);
				stat = REQUEST;
				sigset_t set;
				int sig;
				sigemptyset(&set);
				sigaddset(&set,SIGUSR2);
				pthread_sigmask(SIG_BLOCK, &set, NULL);

				allocate_t at;
				at.set = set;
				at.ns = ns;
				at.buff = req_buff;
				at.share = &customer;
				at.available_tool = &available_tool;
				pthread_create(&tool_allocater,NULL,allocate_tool,&at);
			}
			else if(request == "REST"){		
				stat = REST;
				if(available_tool!=-1){
					shared_info->any_process_in_rest = true;
					printf("[%d] -> Sending REST\n",current_id);
					shared_info->tool_data[available_tool].user_pid_ = 0;
					shared_info->tool_data[available_tool].start_time_ = 0;
					shared_info->tool_data[available_tool].current_share_ = 0;
					shared_info->tool_data[available_tool].current_customer_state = REST;
					kill(shared_info->tool_data[available_tool].tool_pid_,SIGUSR1); // Send REST request to the corresponding tool.
				}
			}
			else if(request == "REPORT"){
				char rep_buff[BUF_SIZE];
				bzero(rep_buff,BUF_SIZE);
				std::ostringstream oss;
				int employed_tool_size = 0;
				pthread_mutex_lock(wl_mutex);
				for(int i=0;i<k;i++)
					if(shared_info->tool_data[i].user_pid_!=0) employed_tool_size++;
				int sz = shared_info->waiting_list_sz;
				share_t wlist[sz];
				for(int i=0;i<sz;i++)
					wlist[i] = shared_info->waiting_list[i];
				struct timeval  tv;
				gettimeofday(&tv, NULL);

				double current_time = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ;
				oss<<"k: "<<k<<", customers: "<<sz<<" waiting, "<<shared_info->number_of_customers - sz - employed_tool_size<<" resting, "<<shared_info->number_of_customers<<" in total"<<std::endl;
				oss<<"average_share: "<<shared_info->total_share / (double) shared_info->number_of_customers<<std::endl;
				oss<<"waiting list:"<<std::endl;
				oss<<"customer     share        waits for(in ms)"<<std::endl;
				oss<<"------------------------------------------"<<std::endl;
				for(int i=0;i<sz;i++){
					share_t data = PQtop(wlist,sz);
					PQpop(wlist,sz);
					oss<<wlist[i].pid_<<"     "<<wlist[i].share_<<"        "<<current_time-wlist[i].start_time_<<std::endl;
				}

				std::string notification = oss.str();
				strcpy(rep_buff,notification.c_str());
				send(ns,rep_buff,strlen(rep_buff)+1,0);

				pthread_mutex_unlock(wl_mutex);
			}
			else if(request == "QUIT"){
				stat = QUIT;
				if(available_tool!=-1){
					shared_info->tool_data[available_tool].current_customer_state = QUIT;
					kill(shared_info->tool_data[available_tool].tool_pid_,SIGUSR1); // Send QUIT request to the corresponding tool.
					PQerase(shared_info->waiting_list,shared_info->waiting_list_sz,getpid()); // Remove the user from waiting list.
				}
				shared_info->total_share -= customer.share_;
				shared_info->number_of_customers--;
				
				pthread_cancel(tool_allocater);
			    break;
			}
			else{
				printf("Not supported/Unknown request..\n");
				printf("Will be ignored..\n");
				continue;
			}
			
		}
	}

    close(sock);	
	return 0;
}