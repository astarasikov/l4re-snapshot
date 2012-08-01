#ifndef __KSYS_CRYPTO_C_COMPAT_H__
#define __KSYS_CRYPTO_C_COMPAT_H__

#define CS_CAP_NAME "crypto_srv"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

extern int L4_CV ksys_aes_encrypt(
	l4_cap_idx_t server_cap,
	char *iv, unsigned iv_size,
	char *key, unsigned key_size,
	char *data, char *out, unsigned size
);

extern int L4_CV ksys_aes_decrypt(
	l4_cap_idx_t server_cap,
	char *iv, unsigned iv_size,
	char *key, unsigned key_size,
	char *data, char *out, unsigned size
);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif //__KSYS_CRYPTO_C_COMPAT_H__
