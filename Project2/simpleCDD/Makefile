obj-m+=ebbchar.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd) modules
	$(CC) testpseudochar.c -o test
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(shell pwd) clean
	rm test