#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "ksys_ss_l4_crypto.h"

#define IV_SIZE MAX_IV_SIZE
#define KEY_SIZE MAX_KEY_SIZE
#define BUF_SIZE 64

#define MSG "hello world 12345!"

static void hexdump(char *in, size_t count) {
	//I am lazy, so just round to the nearest lower
	for (size_t i = 0; i < (count & (~15)); i += 16) {
		unsigned *ptr = (unsigned*)(in + i);
		printf("%08x:| %08x %08x %08x %08x | ",
			i,
			ptr[0], ptr[1], ptr[2], ptr[3]);

		for (int j = 0; j < 16; j++) {
			char c = in[i + j];
			if (isalnum(c)) {
				putchar(c);
			}
			else {
				putchar('_');
			}
		}

		printf("\n");
	}
}

int main() {
	int rc = 0;
	static struct ksys_crypto_request req = {};
	char iv[IV_SIZE] = {};
	char key[KEY_SIZE] = {};
	char raw[BUF_SIZE];
	
	puts("KSYS crypto test");
	memset(iv, 0xab, IV_SIZE);
	memset(key, 0x13, KEY_SIZE);
	memset(raw, 0, BUF_SIZE);
	memcpy(raw, MSG, sizeof(MSG));

	int fd = open("/dev/l4_crypto", O_RDWR);
	if (fd < 0) {
		perror("open");
		rc = fd;
		goto done;
	}

	puts("original data");
	hexdump(raw, BUF_SIZE);

	puts("encrypting data");
	ioctl(fd, KSYS_CRYPTO_AES_ENC, &req);
	hexdump(raw, BUF_SIZE);

	puts("decrypting data");
	ioctl(fd, KSYS_CRYPTO_AES_DEC, &req);
	hexdump(raw, BUF_SIZE);
	
done:
	if (fd >= 0) {
		close(fd);
	}

	while (1) {}

	return rc;
}
