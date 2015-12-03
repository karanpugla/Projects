obj-m += sys_submitjob.o

sys_submitjob-objs := src/compress_decompress.o src/sys_submitjob.o src/encrypt_decrypt.o src/checksum.o src/common_kernel.o

INC=/lib/modules/$(shell uname -r)/build/arch/x86/include

all: xhw3 submitjob 

xhw3: xhw3.c xhw3_helper
	gcc -ggdb -Wall -Werror -I$(INC)/generated/uapi -I$(INC)/uapi   xhw3.c xhw3_helper.o -o xhw3

xhw3_helper: xhw3_helper.c
	gcc -Wall -Werror -c xhw3_helper.c 

submitjob:
	make -Wall -Werror -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f xhw3
