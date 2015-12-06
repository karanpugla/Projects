obj-m += sys_xcrypt.o
CFLAGS_sys_xcrypt.o := -DEXTRA_CREDIT
INC=/lib/modules/$(shell uname -r)/build/arch/x86/include
XCRYPT_INC=$(PWD)/include
all:  xcrypt xcipher

xcipher: xcipher.c
	gcc -Wall -Werror  -I$(INC)/generated/uapi -I$(INC)/uapi xcipher.c -lssl -I$(XCRYPT_INC) -o xcipher

xcrypt:
	make -Wall -Werror -C /lib/modules/$(shell uname -r)/build M=$(PWD)  modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f xcipher
