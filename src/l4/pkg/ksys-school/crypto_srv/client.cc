#include <l4/sys/err.h>
#include <l4/sys/types.h>
#include <l4/re/env>
#include <l4/re/util/cap_alloc>
#include <l4/re/dataspace>
#include <l4/cxx/ipc_stream>
#include <l4/sys/cache.h>

#include <l4/libksys-crypto/ksys_crypto.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>

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

#define MSG "hello world 12345!"

template <class T>
static int test_crypto(L4::Cap<void> &server) {
	char iv[IV_SIZE] = {};
	char key[KEY_SIZE] = {};

	memset(iv, 0xab, IV_SIZE);
	memset(key, 0x13, KEY_SIZE);

	char raw[HC_BUF_SIZE], crypt[HC_BUF_SIZE];
	memset(raw, 0, HC_BUF_SIZE);
	memcpy(raw, MSG, sizeof(MSG));

	T crypto(server, iv, IV_SIZE, key, KEY_SIZE);
	char const* Tname = typeid(T).name();

	printf("calling %s for class %s\n", __func__, Tname);

	puts("Input data");
	hexdump(raw, HC_BUF_SIZE);

	if (crypto.encrypt(raw, crypt, HC_BUF_SIZE)) {
		printf("Error calling %s::encrypt\n", Tname);
		return -1;
	}
	puts("Encryption result");
	hexdump(crypt, HC_BUF_SIZE);
	
	memset(raw, 0, HC_BUF_SIZE);
	if (crypto.decrypt(crypt, raw, HC_BUF_SIZE)) {
		printf("Error calling %s::decrypt\n", Tname);
		return -1;
	}
	puts("Decrypted data");
	hexdump(raw, HC_BUF_SIZE);
}

int main()
{
	L4::Cap <void> server =
	    L4Re::Env::env()->get_cap <void>(CS_CAP_NAME);

	if (!server.is_valid()) {
		printf("Could not get crypto server capability!\n");
		return 1;
	}

	test_crypto<Ksys::MessageBufferCrypto>(server);
	test_crypto<Ksys::DataspaceBufferCrypto>(server);

	return 0;
}
