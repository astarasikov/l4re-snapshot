#ifndef __KSYS_CRYPTO_H__
#define __KSYS_CRYPTO_H__

#include <l4/crypto/aes.h>
#include <l4/crypto/cbc.h>

#include "c_compat.h"

#define HC_BUF_SIZE 64

#define KEY_SIZE ((unsigned long)AES128_KEY_SIZE)
#define IV_SIZE ((unsigned long)16)

namespace Opcode {
	enum Opcodes {
	  CS_AES_ENCRYPT_MBUF,
	  CS_AES_DECRYPT_MBUF,

	  CS_AES_ENCRYPT_DS,
	  CS_AES_DECRYPT_DS,

	  CS_GET_SHM,
	  CS_FREE_SHM,
	};
};

namespace Protocol {
	enum Protocols {
		Crypto_AES,
	};
};

namespace Ksys {

class ICrypto {
public:
	virtual int encrypt(char *data, char *result, unsigned long size) = 0;
	virtual int decrypt(char *data, char *result, unsigned long size) = 0;
	virtual ~ICrypto() {};
};

class MessageBufferCrypto : public ICrypto {
public:
	MessageBufferCrypto(L4::Cap<void> const &server,
		char *iv, unsigned long iv_size,
		char *key, unsigned long key_size) :
		server(server), iv(iv), iv_size(iv_size),
		key(key), key_size(key_size) {}
	
	virtual int encrypt(char *data, char *result, unsigned long size);
	virtual int decrypt(char *data, char *result, unsigned long size);
protected:
	L4::Cap<void> const &server;
	char *iv;
	unsigned long iv_size;

	char *key;
	unsigned long key_size;
};

class DataspaceBufferCrypto : public ICrypto {
public:
	DataspaceBufferCrypto(L4::Cap<void> const &server,
		char *iv, unsigned long iv_size,
		char *key, unsigned long key_size) :
		server(server), iv(iv), iv_size(iv_size),
		key(key), key_size(key_size) {}
	
	virtual int encrypt(char *data, char *result, unsigned long size);
	virtual int decrypt(char *data, char *result, unsigned long size);

protected:
	int freeShm(char *addr, L4::Cap<L4Re::Dataspace> &ds);
	int getShm(unsigned long size, char **addr, L4::Cap<L4Re::Dataspace> &ds);

	L4::Cap<void> const &server;
	char *iv;
	unsigned long iv_size;
	
	char *key;
	unsigned long key_size;
};

} //namespace Ksys

#endif //__KSYS_CRYPTO_H__
