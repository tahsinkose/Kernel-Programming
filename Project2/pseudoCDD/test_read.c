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





int main() {
    //int er = ioctl(fd, _IO('m',1), 0);
    int fd = open("/dev/reminder0",O_RDWR);
    if(fd<0){
    	perror("Failed to open the device...");
    	return errno;
    }
    printf("Reminder is opened\n");
    int expected = 0x0400,actual;
    printf("Expected Bitset is %d\n",expected);
    int er = ioctl(fd, _IO('d',1), &expected);
    char* rbuf = malloc(4095);
    er = ioctl(fd,_IO('d',2), &actual);
    printf("Actual Bitset is %d\n",actual);
    int val = read(fd, rbuf, 30);
    printf("Return from read: %s\n",rbuf);
    
    close(fd);
    return 0;
}