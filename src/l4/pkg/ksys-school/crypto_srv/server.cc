#include <stdio.h>
#include <l4/re/env>
#include <l4/re/util/cap_alloc>
#include <l4/re/util/object_registry>
#include <l4/re/dataspace>
#include <l4/cxx/ipc_server>

#include <l4/re/c/util/cap_alloc.h>
#include <l4/re/c/rm.h>
#include <l4/re/c/mem_alloc.h>
#include <l4/re/c/namespace.h>

#include <l4/libksys-crypto/ksys_crypto.h>

#include <string.h>

static void cs_encrypt_mbuf(L4::Ipc::Iostream &ios) {
	unsigned long size = HC_BUF_SIZE;
	unsigned long key_size = KEY_SIZE;
	unsigned long iv_size = IV_SIZE;
	
	char msg_buf[HC_BUF_SIZE];
	char msg_enc[HC_BUF_SIZE];
	char key_buf[KEY_SIZE];
	char iv_buf[IV_SIZE];

	L4::Ipc::Buf_cp_in<char> key(key_buf, key_size);
	L4::Ipc::Buf_cp_in<char> iv(iv_buf, iv_size);
	L4::Ipc::Buf_cp_in<char> msg(msg_buf, size);
	ios >> key;
	ios >> iv;
	ios >> msg;
	
	memset(msg_enc, 0, HC_BUF_SIZE);

	crypto_aes_ctx_t ctx;
	aes_cipher_set_key(&ctx, key_buf, AES128_KEY_SIZE, 0);
	crypto_cbc_encrypt(aes_cipher_encrypt, &ctx, AES_BLOCK_SIZE,
		msg_buf, msg_enc, iv_buf, HC_BUF_SIZE);

	ios << L4::Ipc::buf_cp_out(msg_enc, size);
}

static void cs_decrypt_mbuf(L4::Ipc::Iostream &ios) {
	unsigned long size = HC_BUF_SIZE;
	unsigned long key_size = KEY_SIZE;
	unsigned long iv_size = IV_SIZE;
	
	char msg_buf[HC_BUF_SIZE];
	char msg_dec[HC_BUF_SIZE];
	char key_buf[KEY_SIZE];
	char iv_buf[IV_SIZE];
	
	L4::Ipc::Buf_cp_in<char> key(key_buf, key_size);
	L4::Ipc::Buf_cp_in<char> iv(iv_buf, iv_size);
	L4::Ipc::Buf_cp_in<char> message(msg_buf, size);
	ios >> key;
	ios >> iv;
	ios >> message;

	memset(msg_dec, 0, HC_BUF_SIZE);

	crypto_aes_ctx_t ctx;
	aes_cipher_set_key(&ctx, key_buf, AES128_KEY_SIZE, 0);
	crypto_cbc_decrypt(aes_cipher_decrypt, &ctx, AES_BLOCK_SIZE,
		msg_buf, msg_dec, iv_buf, HC_BUF_SIZE);

	ios << L4::Ipc::buf_cp_out(msg_dec, size);
}

static int cs_decrypt_ds(L4::Ipc::Iostream &ios, char *addr) {
	unsigned long key_size = KEY_SIZE;
	unsigned long iv_size = IV_SIZE;
	unsigned long msg_size = 0;
	
	char key_buf[KEY_SIZE];
	char iv_buf[IV_SIZE];
	
	L4::Ipc::Buf_cp_in<char> key(key_buf, key_size);
	L4::Ipc::Buf_cp_in<char> iv(iv_buf, iv_size);
	ios >> key;
	ios >> iv;
	ios >> msg_size;

	crypto_aes_ctx_t ctx;
	aes_cipher_set_key(&ctx, key_buf, AES128_KEY_SIZE, 0);
	crypto_cbc_decrypt(aes_cipher_decrypt, &ctx, AES_BLOCK_SIZE,
		addr, addr + msg_size, iv_buf, msg_size);
	return L4_EOK;
}

static int cs_encrypt_ds(L4::Ipc::Iostream &ios, char *addr) {
	unsigned long key_size = KEY_SIZE;
	unsigned long iv_size = IV_SIZE;
	unsigned long msg_size = 0;
	
	char key_buf[KEY_SIZE];
	char iv_buf[IV_SIZE];
	
	L4::Ipc::Buf_cp_in<char> key(key_buf, key_size);
	L4::Ipc::Buf_cp_in<char> iv(iv_buf, iv_size);
	ios >> key;
	ios >> iv;
	ios >> msg_size;

	crypto_aes_ctx_t ctx;
	aes_cipher_set_key(&ctx, key_buf, AES128_KEY_SIZE, 0);
	crypto_cbc_encrypt(aes_cipher_encrypt, &ctx, AES_BLOCK_SIZE,
		addr, addr + msg_size, iv_buf, msg_size);
	return L4_EOK;
}

static int alloc_ds(size_t size, L4::Cap<L4Re::Dataspace> *ds, char **shm) {
	long r = -L4_ENOMEM;
	*shm = 0;

	*ds = L4Re::Util::cap_alloc.alloc<L4Re::Dataspace>();
	if (!(*ds).is_valid()) {
		puts("failed to get DS capability");
		goto err;
	}

	if ((r = L4Re::Env::env()->mem_alloc()->alloc(size, *ds, 0))) {
		puts("failed to allocate DS memory");
		goto err_ma;
	}

	if (L4Re::Env::env()->rm()->attach(shm, (*ds)->size(),
		L4Re::Rm::Search_addr, *ds))
	{
		puts("failed to attach DS memory");
		return L4_ENOMEM;
	}
	return 0;

err_ma:
	L4Re::Util::cap_alloc.free(*ds);
err:
	return -1;
}

static void cs_get_shm(L4::Ipc::Iostream &ios, char **shm) {
	unsigned long size;
	ios >> size;
	L4::Cap<L4Re::Dataspace> ds;
	if (alloc_ds(size, &ds, shm)) {
		puts("failed to allocate dataspace");
		return;
	}
	ios << ds;
}

static void cs_put_shm(L4::Ipc::Iostream &ios) {
		
}

static L4Re::Util::Registry_server<> server;

class crypto_server : public L4::Server_object
{
public:
  int dispatch(l4_umword_t obj, L4::Ipc::Iostream &ios);
protected:
  //I hate myself now. Should've probably used a hash table
  char *shm;
};

int
crypto_server::dispatch(l4_umword_t, L4::Ipc::Iostream &ios)
{
  l4_msgtag_t t;
  ios >> t;

  if (t.label() != Protocol::Crypto_AES)
    return -L4_EBADPROTO;

  L4::Opcode opcode;
  ios >> opcode;

  switch (opcode)
    {
	case Opcode::CS_AES_ENCRYPT_MBUF:
		cs_encrypt_mbuf(ios);
		return L4_EOK;
	case Opcode::CS_AES_DECRYPT_MBUF:
		cs_decrypt_mbuf(ios);
		return L4_EOK;
	case Opcode::CS_AES_ENCRYPT_DS:
		return cs_encrypt_ds(ios, shm);
	case Opcode::CS_AES_DECRYPT_DS:
		return cs_decrypt_ds(ios, shm);
	case Opcode::CS_GET_SHM:
		cs_get_shm(ios, &shm);
    default:
      return -L4_ENOSYS;
    }
}

int
main()
{
  static crypto_server crypto;

  // Register crypto server
  if (!server.registry()->register_obj(&crypto, CS_CAP_NAME).is_valid())
    {
      printf("Could not register crypto server\n");
      return 1;
    }

  printf("Crypto server started\n");

  // Wait for client requests
  server.loop();

  return 0;
}
