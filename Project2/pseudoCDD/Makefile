obj-m+=reminder.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd) modules
	$(CC) test_write.c -o test_write
	$(CC) test_read.c -o test_read
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd) clean
	rm test