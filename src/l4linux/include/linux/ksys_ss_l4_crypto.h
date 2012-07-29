#ifndef __KSYS_SS_L4_CRYPTO__
#define __KSYS_SS_L4_CRYPTO__

//FIXME: use AES headers
#define MAX_CRYPTO_SIZE (2048)
#define MAX_IV_SIZE (16)
#define MAX_KEY_SIZE (16)

struct ksys_crypto_request {
	void __user *iv;
	void __user *key;
	void __user *data;

	unsigned long iv_size;
	unsigned long key_size;
	unsigned long size;
};

#define KSYS_CRYPTO_MAGIC 'k'
#define KSYS_CRYPTO_AES_ENC _IOW(KSYS_CRYPTO_MAGIC, 0, struct ksys_crypto_request *)
#define KSYS_CRYPTO_AES_DEC _IOW(KSYS_CRYPTO_MAGIC, 1, struct ksys_crypto_request *)

#endif //__KSYS_SS_L4_CRYPTO__
