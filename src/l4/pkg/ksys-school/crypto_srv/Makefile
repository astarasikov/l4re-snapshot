PKGDIR          ?= ..
L4DIR           ?= $(PKGDIR)/../..

TARGET                   = crypto_srv-server crypto_srv-client
SRC_CC_crypto_srv-server = server.cc
SRC_CC_crypto_srv-client = client.cc
REQUIRES_LIBS            = cxx_libc_io cxx_io libsigma0 l4re_c-util libksys-crypto

LIBS			= -lcrypto_pad -lcrypto_aes_linux.o -lcrypto_modes -lksys-crypto

include $(L4DIR)/mk/prog.mk

