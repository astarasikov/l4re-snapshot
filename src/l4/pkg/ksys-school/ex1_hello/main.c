#include <stdio.h>
#include <unistd.h>

#include <l4/re/env.h>
#include <l4/sys/ipc.h>
#include <l4/sys/types.h>
#include <l4/sys/utcb.h>
#include <l4/sys/vcon.h>

#define STR "hello world ^_^\n"
#define ROUND_TO_WORD(x) ((sizeof((x)) + sizeof(unsigned) - 1) / sizeof(unsigned))

int
main(void)
{
	puts("ex1_hello starting");

	l4re_env_t *env = l4re_env();
	l4_msg_regs_t *mr = l4_utcb_mr();

	while (1) {
		mr->mr[0] = L4_VCON_WRITE_OP;
		mr->mr[1] = sizeof(STR);
		memcpy(mr->mr + 2, STR, sizeof(STR));

		l4_msgtag_t tag, ret;
		tag = l4_msgtag(L4_PROTO_LOG, 2 + ROUND_TO_WORD(sizeof(STR)), 0, 0);
		ret = l4_ipc_send(env->log, l4_utcb(), tag, L4_IPC_NEVER);

		unsigned err;
		if ((err = l4_msgtag_has_error(ret))) {
			printf("error sending message: %x", err);
			break;
		}

		usleep(250 * 100);
	}
}
