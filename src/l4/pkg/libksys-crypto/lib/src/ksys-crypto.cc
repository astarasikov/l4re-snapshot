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

int Ksys::MessageBufferCrypto::encrypt(char *data,
	char *result, unsigned long size)
{
	L4::Ipc::Iostream s(l4_utcb());

	s << l4_umword_t(Opcode::CS_AES_ENCRYPT_MBUF);
	s << L4::Ipc::buf_cp_out(key, key_size);
	s << L4::Ipc::buf_cp_out(iv, iv_size);
	s << L4::Ipc::buf_cp_out(data, size);

	l4_msgtag_t res = s.call(server.cap(), Protocol::Crypto_AES);
	int err = l4_error(res);
	if (err < 0) {
		printf("IPC call failed %x: %s\n",
			err, l4sys_errtostr(err));
		return 1;
	}

	L4::Ipc::Buf_cp_in<char> out_buf(result, size);
	s >> out_buf;
	return 0;
}

int Ksys::MessageBufferCrypto::decrypt(char *data,
	char *result, unsigned long size)
{
	L4::Ipc::Iostream s(l4_utcb());

	s << l4_umword_t(Opcode::CS_AES_DECRYPT_MBUF);
	s << L4::Ipc::buf_cp_out(key, key_size);
	s << L4::Ipc::buf_cp_out(iv, iv_size);
	s << L4::Ipc::buf_cp_out(data, size);

	l4_msgtag_t res = s.call(server.cap(), Protocol::Crypto_AES);
	int err = l4_error(res);
	if (err < 0) {
		printf("IPC call failed %x: %s\n",
			err, l4sys_errtostr(err));
		return 1;
	}

	L4::Ipc::Buf_cp_in<char> out_buf(result, size);
	s >> out_buf;

	return 0;
}

int Ksys::DataspaceBufferCrypto::encrypt(char *data,
	char *result, unsigned long size)
{
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
	s << L4::Ipc::buf_cp_out(key, key_size);
	s << L4::Ipc::buf_cp_out(iv, iv_size);
	s << size;
	
	l4_msgtag_t res = s.call(server.cap(), Protocol::Crypto_AES);
	if ((rc = l4_ipc_error(res, l4_utcb()))) {
		puts("failed to call CS_AES_ENCRYPT_DS");
	}

	memcpy(result, addr + size, size);

	freeShm(addr, ds);
	return rc;
}
	
int Ksys::DataspaceBufferCrypto::decrypt(char *data,
	char *result, unsigned long size)
{
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
	s << L4::Ipc::buf_cp_out(key, key_size);
	s << L4::Ipc::buf_cp_out(iv, iv_size);
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
	
int Ksys::DataspaceBufferCrypto::freeShm(char *addr,
	L4::Cap<L4Re::Dataspace> &ds)
{
		int err = L4Re::Env::env()->rm()->detach(addr, 0);
		if (err) {
			printf("failed to unmap memory region: %d\n", err);
			return err;
		}
		L4Re::Util::cap_alloc.free(ds, L4Re::This_task);
		return 0;
	}

int Ksys::DataspaceBufferCrypto::getShm(unsigned long size,
	char **addr, L4::Cap<L4Re::Dataspace> &ds)
{
	int rc = -1;
	int err = 0;
	l4_msgtag_t res;
	ds = L4Re::Util::cap_alloc.alloc<L4Re::Dataspace>();
	if (!ds.is_valid()) {
		printf("Could not get capability slot!\n");
		return -1;
	}

	L4::Ipc::Iostream s(l4_utcb());
	s << l4_umword_t(Opcode::CS_GET_SHM);
	s << l4_umword_t(size << 1);	
	s << L4::Ipc::Small_buf(ds);
	res = s.call(server.cap(), Protocol::Crypto_AES);
	err = l4_error(res);
	if (err < 0) {
		printf("IPC call failed %x: %s\n",
			err, l4sys_errtostr(err));
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

extern "C" {

#define USE_CXX_CAP_GET 1
#define USE_MBUF_CRYPTO 0

int ksys_aes_encrypt(
	l4_cap_idx_t server_cap,
	char *iv, unsigned long iv_size,
	char *key, unsigned long key_size,
	char *data, char *out, unsigned long size
)
{
#if 1
	L4::Cap <void> cap =
	    L4Re::Env::env()->get_cap <void>(CS_CAP_NAME);
	if (!cap.is_valid()) {
		printf("Could not get crypto server capability!\n");
		return 1;
	}
#else
	L4::Cap<void> cap(server_cap);
#endif

#if USE_MBUF_CRYPTO
	Ksys::MessageBufferCrypto crypto(cap, iv, iv_size, key, key_size);
#else
	Ksys::DataspaceBufferCrypto crypto(cap, iv, iv_size, key, key_size);
#endif
	
	return crypto.encrypt(data, out, size);
}

int ksys_aes_decrypt(
	l4_cap_idx_t server_cap,
	char *iv, unsigned long iv_size,
	char *key, unsigned long key_size,
	char *data, char *out, unsigned long size)
{
#if 1
	L4::Cap <void> cap =
	    L4Re::Env::env()->get_cap <void>(CS_CAP_NAME);
	if (!cap.is_valid()) {
		printf("Could not get crypto server capability!\n");
		return 1;
	}
#else
	L4::Cap<void> cap(server_cap);
#endif

#if USE_MBUF_CRYPTO
	Ksys::MessageBufferCrypto crypto(cap, iv, iv_size, key, key_size);
#else
	Ksys::DataspaceBufferCrypto crypto(cap, iv, iv_size, key, key_size);
#endif
	return crypto.decrypt(data, out, size);
}

}
