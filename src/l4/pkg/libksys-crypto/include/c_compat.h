#ifndef __KSYS_CRYPTO_C_COMPAT_H__
#define __KSYS_CRYPTO_C_COMPAT_H__

#define CS_CAP_NAME "crypto_srv"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

extern int ksys_aes_encrypt(
	l4_cap_idx_t server_cap,
	char *iv, unsigned long iv_size,
	char *key, unsigned long key_size,
	char *data, char *out, unsigned long size
);

extern int ksys_aes_decrypt(
	l4_cap_idx_t server_cap,
	char *iv, unsigned long iv_size,
	char *key, unsigned long key_size,
	char *data, char *out, unsigned long size
);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //__KSYS_CRYPTO_C_COMPAT_H__
