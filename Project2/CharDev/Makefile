obj-m+=chardev.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd) modules
	$(CC) test.c -o test
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd) clean
	rm test