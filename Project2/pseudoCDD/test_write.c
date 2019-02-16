//
// Created by efecan on 07.01.2018.
//
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>


struct writebuf {
    char channel;
    char content[];
} *wbuf;

struct readbuf{
   uint16_t channel_set;
   char content[]; 
} *rbuf;



int main() {
/* user space write n bytes message on write set 0xaaaa */
    char* message = "testreminder";
    char* message2 = "testreminder2";
    //int er = ioctl(fd, _IO('m',1), 0);
    int fd = open("/dev/reminder0",O_RDWR);
    if(fd<0){
    	perror("Failed to open the device...");
    	return errno;
    }
    printf("Reminder is opened\n");
    int delay = 5;
    int er = ioctl(fd, _IO('d',3), &delay);
    wbuf = malloc(4098);
    //memcpy(wbuf->content, message, strlen(message));
    memset(wbuf->content,65,4095);
    wbuf->channel = 10;
    int val = write(fd, wbuf, 4096);
    sleep(2.0);
    
    
    delay = 0;
    er = ioctl(fd, _IO('d',3), &delay);
    printf("Delayed Write return: %d\n",val);
    memcpy(wbuf->content,message2,strlen(message2)+2);
    //val = write(fd, wbuf, strlen(message2)+2);
    //printf("Immediate Write return: %d\n",val);

    sleep(2.0);
    //rbuf  = malloc(4095);
    //rbuf->channel_set = 0x0400;
    
    //read(fd,rbuf,300);   
    close(fd);
    return 0;
}