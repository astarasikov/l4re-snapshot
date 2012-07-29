/*
 * This driver provides the userspace interface
 * for the cryptographic server running under
 * L4 for the l4linux paravirtualized kernel
 * (Ksys Labs Summer School 2012 home assignment #3)
 *
 * Copyright (C) Alexander Tarasikov <alexander.tarasikov@gmail.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#include <linux/ksys_ss_l4_crypto.h>

#include <l4/re/c/util/cap_alloc.h>
#include <l4/re/c/rm.h>
#include <l4/re/c/mem_alloc.h>
#include <l4/re/c/namespace.h>

#include <l4/libksys-crypto/c_compat.h>

#define LOG_TAG "[KSYS] L4crypto: "

//for L4 linkage
#include <asm/generic/l4lib.h>
L4_EXTERNAL_FUNC(ksys_aes_encrypt);
L4_EXTERNAL_FUNC(ksys_aes_decrypt);

struct crypto_state {
	void *iv;
	void *key;
	void *buffer;
	struct mutex mutex;
	struct file *file;
};

static l4_cap_idx_t crypto_server_cap = L4_INVALID_CAP;

static int crypto_open(struct inode *inode, struct file *file) {
	struct crypto_state *state= 0;
	
	state = kzalloc(sizeof(struct crypto_state), GFP_KERNEL);
	if (!state) {
		pr_err(LOG_TAG "out of memory\n");
		goto fail;
	}
	
	state->iv = kzalloc(MAX_IV_SIZE, GFP_KERNEL);
	if (!state->iv) {
		pr_err(LOG_TAG "no memory for the IV\n");
		goto fail;
	}
	
	state->key = kzalloc(MAX_KEY_SIZE, GFP_KERNEL);
	if (!state->key) {
		pr_err(LOG_TAG "no memory for the key\n");
		goto fail;
	}

	state->buffer = kzalloc(MAX_CRYPTO_SIZE, GFP_KERNEL);
	if (!state->buffer) {
		pr_err(LOG_TAG "no memory for the buffer\n");
		goto fail;
	}

	state->file = file;

	mutex_init(&state->mutex);

	return 0;

fail:
	if (state) {
		if (state->iv) {
			kfree(state->iv);
		}

		if (state->key) {
			kfree(state->key);
		}

		if (state->buffer) {
			kfree(state->buffer);
		}

		kfree(state);
	}
	return -1;
}

static long crypto_ioctl(struct file *file,
	unsigned int code, unsigned long arg)
{
	int rc = 0;
	unsigned long size = 0;
	unsigned long iv_size = 0;
	unsigned long key_size = 0;

	struct crypto_state *state =
		container_of(&file,	struct crypto_state, file);
	
	struct ksys_crypto_request req = {};

	mutex_lock(&state->mutex);
	switch (code) {
		case KSYS_CRYPTO_AES_ENC:
		case KSYS_CRYPTO_AES_DEC:
			break;

		default:
			rc = -EINVAL;
			goto done;
	}

	if (copy_from_user(&req, (void*) arg, sizeof(req))) {
		pr_err(LOG_TAG "invalid request\n");
		rc = -EFAULT;
		goto done;
	}

	size = req.size;
	iv_size = req.iv_size;
	key_size = req.key_size;
	if (iv_size > MAX_IV_SIZE) {
		pr_err(LOG_TAG "IV is too large\n");
		rc = -EINVAL;
		goto done;
	}

	if (key_size > MAX_KEY_SIZE) {
		pr_err(LOG_TAG "key is too large\n");
		rc = -EINVAL;
		goto done;
	}

	if (size > MAX_CRYPTO_SIZE) {
		pr_err(LOG_TAG "payload is too large\n");
		rc = -EINVAL;
		goto done;
	}

	if (copy_from_user(state->iv, req.iv, iv_size)) {
		pr_err(LOG_TAG "failed to copy the IV from userspace\n");
		rc = -EFAULT;
		goto done;
	}
	
	if (copy_from_user(state->key, req.key, key_size)) {
		pr_err(LOG_TAG "failed to copy the key from userspace\n");
		rc = -EFAULT;
		goto done;
	}
	
	if (copy_from_user(state->buffer, req.data, size)) {
		pr_err(LOG_TAG "failed to copy the data from userspace\n");
		rc = -EFAULT;
		goto done;
	}
	
	switch (code) {
		case KSYS_CRYPTO_AES_ENC:
			if (ksys_aes_encrypt
				(
					crypto_server_cap,
					state->iv, iv_size,
					state->key, key_size,
					state->buffer, state->buffer, size
				))
			{
				pr_err(LOG_TAG "failed to encrypt the data\n");
				rc = -EFAULT;
				goto done;
			}
			break;
		case KSYS_CRYPTO_AES_DEC:
			if (ksys_aes_decrypt
				(
					crypto_server_cap,
					state->iv, iv_size,
					state->key, key_size,
					state->buffer, state->buffer, size
				))
			{
				pr_err(LOG_TAG "failed to decrypt the data\n");
				rc = -EFAULT;
				goto done;
			}
			break;
	}

	if (copy_to_user(req.data, state->buffer, size)) {
		pr_err(LOG_TAG "failed to copy the data to userspace\n");
		rc = -EFAULT;
		goto done;
	}

done:
	mutex_unlock(&state->mutex);
	return rc;
}

static int crypto_release(struct inode *inode, struct file *file) {
	struct crypto_state *state =
		container_of(&file,	struct crypto_state, file);
	//mutex_destroy(&state->mutex);
	kfree(state->iv);
	kfree(state->key);
	kfree(state->buffer);
	kfree(state);
	return 0;
}

static struct file_operations crypto_fops = {
	.owner = THIS_MODULE,
	.open = crypto_open,
	.release = crypto_release,
	.unlocked_ioctl = crypto_ioctl,
};

static struct miscdevice crypto_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "l4_crypto",
	.fops = &crypto_fops,
};

static int init_l4_server(void) {
	l4re_namespace_t crypto_ns = L4_INVALID_CAP;

	crypto_ns = l4re_get_env_cap(CS_CAP_NAME);
	if (l4_is_invalid_cap(crypto_ns)) {
		pr_err(LOG_TAG "%s: failed to get server capability\n", __func__);
		return -ENODEV;
	}

	crypto_server_cap = crypto_ns;
	pr_info(LOG_TAG "%s: OK\n", __func__);

	return 0;
}

static void exit_l4_server(void) {
	pr_info(LOG_TAG "+%s\n", __func__);
}

static int __init ksys_crypto_init(void) {
	int ret;

	if ((ret = init_l4_server()) < 0) {
		pr_err(LOG_TAG "failed to start L4 server");
		goto fail_l4;
	}

	if ((ret = misc_register(&crypto_misc)) < 0) {
		pr_err(LOG_TAG "failed to register the device");
		goto fail_misc;
	}

	pr_info(LOG_TAG "registered L4 crypto interface");

	return 0;

fail_misc:
	exit_l4_server();
fail_l4:
	return ret;
}

static void __exit ksys_crypto_exit(void) {
	pr_info(LOG_TAG "unregistering");
	misc_deregister(&crypto_misc);
	exit_l4_server();
}

module_init(ksys_crypto_init);
module_exit(ksys_crypto_exit);
