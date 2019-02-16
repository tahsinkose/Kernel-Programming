#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
void set_read(int* lpipe)
{
    dup2(lpipe[0], STDIN_FILENO);
    close(lpipe[0]); // we have a copy already, so close it
    close(lpipe[1]); // not using this end
}
  
void set_write(int* rpipe)
{
    dup2(rpipe[1], STDOUT_FILENO);
    close(rpipe[0]); // not using this end
    close(rpipe[1]); // we have a copy already, so close it
}

void fork_and_chain(int* lpipe, int* rpipe,const char* command,int i)
{
    if(!fork())
    {
        if(lpipe) // there's a pipe from the previous process
            set_read(lpipe);
        if(rpipe) // there's a pipe to the next process
            set_write(rpipe);
        if(i==2)
    		execl(command,command,"-3",NULL);
    	else
    		execl(command,command,NULL);
    	exit(1);
    }
}

void pipeall(const char* commands[],int n){
	int lpipe[2], rpipe[2];
	pipe(rpipe);
	fork_and_chain(NULL,rpipe,commands[0],0); // First child
	lpipe[0] = rpipe[0];
	lpipe[1] = rpipe[1];

	// chain all but the first and last children
	for(int i = 1; i < n - 1; i++)
	{
	    pipe(rpipe); // make the next output pipe
	    fork_and_chain(lpipe, rpipe,commands[i],i);
	    close(lpipe[0]); // both ends are attached, close them on parent
	    close(lpipe[1]);
	    lpipe[0] = rpipe[0]; // output pipe becomes input pipe
	    lpipe[1] = rpipe[1];
	}
	fork_and_chain(lpipe, NULL,commands[n-1],n-1);
	close(lpipe[0]);
	close(lpipe[1]);
	close(rpipe[0]);
	close(rpipe[1]);
}

int main(int argc, char const *argv[])
{
	char const** commands;
	int nc = 3;
	commands = (char const**)malloc(sizeof(char const*) * nc);
	for(int i=0;i<nc;i++){
		commands[i] = (char* )malloc(sizeof(char)*25);
	}
	commands[0] = "/usr/bin/du";
	commands[1] = "/usr/bin/sort";
	commands[2] = "/usr/bin/head";

	pipeall(commands,nc);
	return 0;
}