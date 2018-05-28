EXTRA_CFLAGS := -I$(src)/..
obj-m += kcpuhog.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

