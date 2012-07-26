#pragma once

#include <l4/crypto/aes.h>
#include <l4/crypto/cbc.h>

#define HC_BUF_SIZE 64
#define CS_CAP_NAME "crypto_srv"

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
