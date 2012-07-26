#include <l4/sys/err.h>
#include <l4/sys/types.h>
#include <l4/re/env>
#include <l4/re/util/cap_alloc>
#include <l4/re/dataspace>
#include <l4/cxx/ipc_stream>
#include <l4/sys/cache.h>

#if 0
#include <l4/re/c/util/cap_alloc.h>
#include <l4/re/c/rm.h>
#include <l4/re/c/mem_alloc.h>
#include <l4/re/c/namespace.h>
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "shared.h"

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

class ICrypto {
public:
	virtual int encrypt(char *data, char *result, unsigned long size) = 0;
	virtual int decrypt(char *data, char *result, unsigned long size) = 0;
	virtual ~ICrypto() {};
};

class MessageBufferCrypto : public ICrypto {
public:
	MessageBufferCrypto(L4::Cap<void> const &server,
		char *iv, char *key) :
		server(server), iv(iv), key(key) {}
	
	virtual int encrypt(char *data, char *result, unsigned long size) {
		L4::Ipc::Iostream s(l4_utcb());

		s << l4_umword_t(Opcode::CS_AES_ENCRYPT_MBUF);
		s << L4::Ipc::buf_cp_out(key, KEY_SIZE);
		s << L4::Ipc::buf_cp_out(iv, IV_SIZE);
		s << L4::Ipc::buf_cp_out(data, size);

		l4_msgtag_t res = s.call(server.cap(), Protocol::Crypto_AES);
		if (l4_ipc_error(res, l4_utcb()))
			return 1;

		L4::Ipc::Buf_cp_in<char> out_buf(result, size);
		s >> out_buf;
		return 0;
	}

	virtual int decrypt(char *data, char *result, unsigned long size) {
		L4::Ipc::Iostream s(l4_utcb());

		s << l4_umword_t(Opcode::CS_AES_DECRYPT_MBUF);
		s << L4::Ipc::buf_cp_out(key, KEY_SIZE);
		s << L4::Ipc::buf_cp_out(iv, IV_SIZE);
		s << L4::Ipc::buf_cp_out(data, size);

		l4_msgtag_t res = s.call(server.cap(), Protocol::Crypto_AES);
		if (l4_ipc_error(res, l4_utcb()))
			return 1;

		L4::Ipc::Buf_cp_in<char> out_buf(result, size);
		s >> out_buf;

		return 0;
	}
protected:
	L4::Cap<void> const &server;
	char *iv;
	char *key;
};

class DataspaceBufferCrypto : public ICrypto {
public:
	DataspaceBufferCrypto(L4::Cap<void> const &server,
		char *iv, char *key) :
		server(server), iv(iv), key(key) {}
	
	virtual int encrypt(char *data, char *result, unsigned long size) {
		char *addr;
		int rc = 0;
		L4::Cap<L4Re::Dataspace> ds;
		if (getShm(size, &addr, ds)) {
			printf("failed to get SHM region\n");
			return -1;
		}
		
		memcpy(addr, data, size);
		
		L4::Ipc::Iostream s(l4_utcb());
		s << l4_umword_t(Opcode::CS_AES_ENCRYPT_DS);
		s << L4::Ipc::buf_cp_out(key, KEY_SIZE);
		s << L4::Ipc::buf_cp_out(iv, IV_SIZE);
		s << size;
		
		l4_msgtag_t res = s.call(server.cap(), Protocol::Crypto_AES);
		if ((rc = l4_ipc_error(res, l4_utcb()))) {
			puts("failed to call CS_AES_ENCRYPT_DS");
		}

		memcpy(result, addr + size, size);

		freeShm(addr, ds);
		return rc;
	}
	
	virtual int decrypt(char *data, char *result, unsigned long size) {
		char *addr;
		int rc = 0;
		L4::Cap<L4Re::Dataspace> ds;
		if (getShm(size, &addr, ds)) {
			printf("failed to get SHM region\n");
			return -1;
		}
		
		memcpy(addr, data, size);
		
		L4::Ipc::Iostream s(l4_utcb());
		s << l4_umword_t(Opcode::CS_AES_DECRYPT_DS);
		s << L4::Ipc::buf_cp_out(key, KEY_SIZE);
		s << L4::Ipc::buf_cp_out(iv, IV_SIZE);
		s << size;
		
		l4_msgtag_t res = s.call(server.cap(), Protocol::Crypto_AES);
		if ((rc = l4_ipc_error(res, l4_utcb()))) {
			puts("failed to call CS_AES_DECRYPT_DS");
		}

		//crypto_cbc_decrypt seems to corrupt memory
		//when working in-place. gotta check later
		memcpy(result, addr + size, size);

		freeShm(addr, ds);
		return rc;
	}
protected:
	int freeShm(char *addr, L4::Cap<L4Re::Dataspace> &ds) {
		int err = L4Re::Env::env()->rm()->detach(addr, 0);
		if (err) {
			printf("failed to unmap memory region: %d\n", err);
			return err;
		}
		L4Re::Util::cap_alloc.free(ds, L4Re::This_task);
		return 0;
	}

	int getShm(unsigned long size, char **addr, L4::Cap<L4Re::Dataspace> &ds) {
		int rc = -1;
		int err = 0;
		l4_msgtag_t res;
		ds = L4Re::Util::cap_alloc.alloc<L4Re::Dataspace>();
		if (!ds.is_valid())
		{
			printf("Could not get capability slot!\n");
			return -1;
		}

		L4::Ipc::Iostream s(l4_utcb());
		s << l4_umword_t(Opcode::CS_GET_SHM);
		s << l4_umword_t(size << 1);	
		s << L4::Ipc::Small_buf(ds);
		res = s.call(server.cap(), Protocol::Crypto_AES);
		if (l4_ipc_error(res, l4_utcb())) {
			goto fail_ipc_call;
		}
	  
		rc = L4Re::Env::env()->rm()->attach(addr, ds->size(),
											   L4Re::Rm::Search_addr, ds);
		if (rc < 0) {
			printf("Error attaching data space: %s\n", l4sys_errtostr(rc));
			goto fail_rm_attach;
		}
		//printf("got shared dataspace @ %p\n", *addr);
		return 0;

	fail_rm_attach:
		err = L4Re::Env::env()->rm()->detach(*addr, 0);
		if (err) {
			puts("failed to unmap memory region");
			rc = err;
		}
	fail_ipc_call:
		L4Re::Util::cap_alloc.free(ds, L4Re::This_task);
		return rc;
	}

	L4::Cap<void> const &server;
	char *iv;
	char *key;
};


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

	T crypto(server, iv, key);
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

	test_crypto<MessageBufferCrypto>(server);
	test_crypto<DataspaceBufferCrypto>(server);

	return 0;
}
