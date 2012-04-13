#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <pthread.h>
#include <pthread-l4.h>

#include <drv_reg.h>

#include <l4/re/c/dataspace.h>
#include <l4/re/c/mem_alloc.h>
#include <l4/re/c/rm.h>
#include <l4/re/c/namespace.h>
#include <l4/re/c/util/cap_alloc.h>
#include <l4/re/event_enums.h>
#include <l4/io/io.h>
#include <l4/sys/icu.h>
#include <l4/sys/irq.h>
#include <l4/sys/factory.h>
#include <l4/sys/thread.h>
#include <l4/sys/debugger.h>
#include <l4/util/util.h>
#include <l4/vbus/vbus.h>
#include <l4/vbus/vbus_gpio.h>
#include <l4/vbus/vbus_mcspi.h>

#include "tsc-omap3.h"


static char *omap_dev_name = "OMAP_TSC";

static Input_handler tsc_handler;
static void *tsc_priv;
static pthread_t _pthread;

static l4_cap_idx_t vbus = L4_INVALID_CAP;
static l4vbus_device_handle_t tsc_handle;
static l4vbus_device_handle_t gpio_handle;
static l4vbus_device_handle_t mcspi_handle;

unsigned long gpio4_base;
unsigned long mcspi1_base;

volatile char need_sync = 0;
unsigned char empty_cnt = 0;
unsigned char valid_cnt = 0;

#define MAX_SAMPLES 8

int bufx[MAX_SAMPLES];
int bufy[MAX_SAMPLES];


static unsigned long l4io_remap(unsigned long phys_addr, size_t size)
{
	int i;
	l4_addr_t reg_start, reg_len;
	void *addr;
	unsigned long offset;

	if (!l4io_has_resource(L4IO_RESOURCE_MEM, phys_addr, phys_addr + size - 1)) {
		printf("ERROR: IO-memory (%lx+%zx) not available\n", phys_addr, size);
		return NULL;
	}

	if ((i = l4io_search_iomem_region(phys_addr, size, &reg_start, &reg_len))) {
		printf("ioremap: No region found for %lx: %d\n", phys_addr, i);
		return NULL;
	}

	if ((i = l4io_request_iomem(reg_start, reg_len, 0, (l4_addr_t *)&addr))) {
		printf("ERROR: l4io_request_iomem error(%lx+%lx): %d\n", reg_start, reg_len, i);
		return NULL;
	}

	printf("[TSC OMAP3] %s: Mapping physaddr %08lx [0x%zx Bytes, %08lx+%06lx] to %08lx+%06lx\n",
	       __func__, phys_addr, size, reg_start, reg_len, (unsigned long)addr, offset
	);

	offset = 0;
	offset += phys_addr - reg_start;

	return (unsigned long)addr + offset;
}

static unsigned short ads7846_wr(unsigned char d)
{
	int tmp0, tmp1;
    unsigned short res = 0;
    unsigned int r;

	*(volatile unsigned int*)(mcspi1_base + 0x38) = d;

	int i = 0;
	while ( !(*(volatile unsigned int*)(mcspi1_base + 0x30) & (1<<2)) )
	{
		i++;
		if (i > 500)
		{
            printf("[TSC OMAP3] ads7846_wr timeout.\n");
			return -1;
		}
		l4_usleep(50);
	}

	l4_usleep(200);

	tmp0 = *(volatile unsigned int*)(mcspi1_base + 0x3C);

	*(volatile unsigned int*)(mcspi1_base + 0x38) = 0;
	while ( !(*(volatile unsigned int*)(mcspi1_base + 0x30) & (1<<2)) )
		;

	tmp0 = *(volatile unsigned int*)(mcspi1_base + 0x3C);

	*(volatile unsigned int*)(mcspi1_base + 0x38) = 0;
	while ( !(*(volatile unsigned int*)(mcspi1_base + 0x30) & (1<<2)) )
		;

	tmp1 = *(volatile unsigned int*)(mcspi1_base + 0x3C);

    res = (((tmp0 << 8) + tmp1) >> 4) & 0x0fff;

	return res;
}



static int tsc_init(void)
{
	unsigned int r;

	/* Get resources */
	vbus = l4re_get_env_cap("vbus");
	if (l4_is_invalid_cap(vbus))
	{
        printf("[TSC OMAP3] Failed to query vbus\n");
		return -1;
	}

	if (l4vbus_get_device_by_hid(vbus, 0, &tsc_handle, omap_dev_name, 0, 0))
	{
        printf("[TSC OMAP3] Cannot find TSC device\n");
		return -L4_ENODEV;
	}

	if (l4vbus_get_device_by_hid(vbus, 0, &gpio_handle, "gpio", 0, 0))
	{
        printf("[TSC OMAP3] Cannot find GPIO bus\n");
		return -L4_ENODEV;
	}

	if (l4vbus_get_device_by_hid(vbus, 0, &mcspi_handle, "mcspi", 0, 0))
	{
        printf("[TSC OMAP3] Cannot find McSPI bus\n");
		return -L4_ENODEV;
	}


	/* Configure GPIO pin */
	// Remap GPIO resources
	gpio4_base = l4io_remap(0x49054000, 4*1024);

	// debouncing
	*(volatile unsigned int*)(gpio4_base + 0x54) = 100;
	*(volatile unsigned int*)(gpio4_base + 0x50) = (1<<18);

	// irq
	*(volatile unsigned int*)(gpio4_base + 0x4C) = (1<<18); // falling detect
	*(volatile unsigned int*)(gpio4_base + 0x1C) = (1<<18);

#if 0
	// check
    printf("----------------------------------------------------------------------\n");
	r = (*(volatile unsigned int*)(gpio4_base + 0x30));
    printf("[TSC OMAP3] GPIO_CTRL = %x\n", r);

	r = (*(volatile unsigned int*)(gpio4_base + 0x1C));
    printf("[TSC OMAP3] IRQENABLE1 = %x\n", r);

	r = (*(volatile unsigned int*)(gpio4_base + 0x2C));
    printf("[TSC OMAP3] IRQENABLE2 = %x\n", r);

	r = (*(volatile unsigned int*)(gpio4_base + 0x34));
    printf("[TSC OMAP3] GPIO_OE = %x\n", r);

	r = (*(volatile unsigned int*)(gpio4_base + 0x40));
    printf("[TSC OMAP3] LEVELDETECT0 = %x\n", r);

	r = (*(volatile unsigned int*)(gpio4_base + 0x44));
    printf("[TSC OMAP3] LEVELDETECT1 = %x\n", r);

	r = (*(volatile unsigned int*)(gpio4_base + 0x50));
    printf("[TSC OMAP3] DEBOUNCE = %x\n", r);

    printf("----------------------------------------------------------------------\n");
#endif

	/* Configure SPI module */
	mcspi1_base = l4io_remap(0x48098000, 4*1024);

	// reset module
	r = *(volatile unsigned int*)(mcspi1_base + 0x10);
	r |= (1 << 1);
	*(volatile unsigned int*)(mcspi1_base + 0x10) = r;

	int i = 0;
	while ( !(*(volatile unsigned int*)(mcspi1_base + 0x14) & 1) )
	{
		i++;
		if (i > 500)
		{
			printf("[TSC OMAP3] McSPI1 reset timeout.\n");
			return -1;
		}
		l4_usleep(500);
	}

	// disable channels
	*(volatile unsigned int*)(mcspi1_base + 0x34) = 0;
	*(volatile unsigned int*)(mcspi1_base + 0x34 + 0x14) = 0;
	*(volatile unsigned int*)(mcspi1_base + 0x34 + 0x28) = 0;
	*(volatile unsigned int*)(mcspi1_base + 0x34 + 0x3C) = 0;

	// disable interrupts
	*(volatile unsigned int*)(mcspi1_base + 0x1C) = 0;
	//*(volatile unsigned int*)(mcspi1_base + 0x18) = 0xFFFFFFFF;


	r = ( (1<<16) | (1<<6) | (0x5<<2) | (0x7<<7) );
	*(volatile unsigned int*)(mcspi1_base + 0x2c + 0x14*0) = r;

	// enable
	*(volatile unsigned int*)(mcspi1_base + 0x28) = 0x01;

	r = *(volatile unsigned int*)(mcspi1_base + 0x2c + 0x14*0);
	r |= (1<<20);
	*(volatile unsigned int*)(mcspi1_base + 0x2c + 0x14*0) = r;

	r = *(volatile unsigned int*)(mcspi1_base + 0x34 + 0x14*0);
	r |= 1;
	*(volatile unsigned int*)(mcspi1_base + 0x34 + 0x14*0) = r;


#if 0
	// check
	r = (*(volatile unsigned int*)(mcspi1_base + 0x10));
	printf("%s: MCSPI_SYSCONFIG = %08x\n", __func__, r);

	r = (*(volatile unsigned int*)(mcspi1_base + 0x14)) & 1;
	printf("%s: RESETDONE = %i\n", __func__, r);

	r = *(volatile unsigned int*)(mcspi1_base + 0x1C);
	printf("%s: IRQENABLE = %08x\n", __func__, r);

	r = *(volatile unsigned int*)(mcspi1_base + 0x24);
	printf("%s: SYST = %08x\n", __func__, r);

	r = *(volatile unsigned int*)(mcspi1_base + 0x28);
	printf("%s: MODULECTRL = %08x\n", __func__, r);

	r = *(volatile unsigned int*)(mcspi1_base + 0x2c + 0x14*0);
	printf("%s: CH0CONF = %08x\n", __func__, r);
#endif

	// enable ads7846
	r = ads7846_wr(ADS7846E_S);

	return 0;
}

static void tsc_get_pen_position(int *x, int *y, int* Rt)
{
	unsigned short ts_y, ts_x, ts_z1, ts_z2;
    long unsigned int ts_rt;

	//read y
    ts_y = ads7846_wr(ADS7846E_S | ADS_PD10_ADC_ON | ADS_PD10_REF_ON | ADS7846E_ADD_DFR_Y);
    //read x
    ts_x = ads7846_wr(ADS7846E_S | ADS_PD10_ADC_ON | ADS_PD10_REF_ON | ADS7846E_ADD_DFR_X);
	//read z1
    ts_z1 = ads7846_wr(ADS7846E_S | ADS_PD10_ADC_ON | ADS_PD10_REF_ON | ADS7846E_ADD_DFR_Z1);
	//read z2
    ts_z2 = ads7846_wr(ADS7846E_S | ADS_PD10_ADC_ON | ADS_PD10_REF_ON | ADS7846E_ADD_DFR_Z2);

    if ( (ts_z1 > 0) && (ts_x > 0)) {
        /* compute touch pressure resistance using equation #2 */
        ts_rt = ts_z2;
        ts_rt -= ts_z1;
        ts_rt *= ts_x;
        ts_rt *= 180;
        ts_rt /= ts_z1;
        ts_rt /= 4096;
    }
    else {
        ts_rt = -1;
    }

	//printf ("[TSC OMAP3] Info: (ts_x,ts_y,ts_z1,ts_z2,ts_rt)=(%d, %d, %d, %d, %d)\n", ts_x, ts_y, ts_z1, ts_z2, ts_rt);
    if (ts_rt < 160)
    {
        *x = ts_x;
        *y = ts_y;
        *Rt = ts_rt;

    } else {
        *x = -1;
        *y = -1;
        //printf ("[TSC OMAP3] Info: ts_rt=%d\n", ts_rt);
    }
}

static void create_motion_event(void)
{
	int x = 0, y = 0, Rt = 0, i;

    int sumx, sumy;

    tsc_get_pen_position(&x, &y, &Rt);

    if (x==-1 || y==-1)
    {
        empty_cnt++;
        return;
    }

    l4_usleep(1000);
    if (valid_cnt<MAX_SAMPLES)
    {
        // todo: check
        bufx[valid_cnt] = x;
        bufy[valid_cnt] = y;
        valid_cnt++;
        return;
    }
    else {
        // shift buffers
        for(i = 1; i != MAX_SAMPLES; i++)
        {
            bufx[i-1] = bufx[i];
            bufy[i-1] = bufy[i];
        }
        bufx[MAX_SAMPLES-1] = x;
        bufy[MAX_SAMPLES-1] = y;
    }

    sumx = 0; sumy = 0;
    for(i = 0; i != MAX_SAMPLES; i++)
    {
        sumx += bufx[i];
        sumy += bufy[i];
    }
    x = sumx/MAX_SAMPLES;
    y = sumy/MAX_SAMPLES;

    //printf ("[TSC OMAP3] create_motion_event ts_x=%d, ts_y=%d Rt=%d\n", x, y, Rt);

    Input_event ev_x = { L4RE_EV_ABS, L4RE_ABS_X, x };
    tsc_handler(ev_x, tsc_priv);
    Input_event ev_y = { L4RE_EV_ABS, L4RE_ABS_Y, y };
    tsc_handler(ev_y, tsc_priv);

    if (need_sync == 1)
    {
        // generate touch start event;
        Input_event ev = { L4RE_EV_KEY, L4RE_BTN_TOUCH, 1 };
        tsc_handler(ev, tsc_priv);
        need_sync = 0;
    }
}

l4_cap_idx_t get_icu(void);

static int tsc_irq_func(void)
{
	int irqno = 32;
	int err;
	unsigned int r;

	l4_cap_idx_t irqcap, icucap;
	l4_msgtag_t tag;
	l4_cap_idx_t thread_cap = pthread_getl4cap(_pthread);

	l4_debugger_set_object_name(thread_cap, "tsc-omap3.irq");

	icucap = l4re_get_env_cap("icu");
	if (l4_is_invalid_cap(icucap))
	{
        printf("[TSC OMAP3] Failed to query icucap\n");
		return -1;
	}

	/* Get another free capaiblity slot for the corresponding IRQ object*/
	if (l4_is_invalid_cap(irqcap = l4re_util_cap_alloc()))
	{
        printf("[TSC OMAP3] Failed to l4re_util_cap_alloc\n");
		return -1;
	}

	/* Create IRQ object */
	if (l4_error(tag = l4_factory_create_irq(l4re_global_env->factory, irqcap)))
	{
        printf("[TSC OMAP3] Could not create IRQ object: %lx\n", l4_error(tag));
		return -1;
	}

	/*
	 * Bind the recently allocated IRQ object to the IRQ number irqno
	 * as provided by the ICU.
	 */
	if (l4_error(l4_icu_bind(icucap, irqno, irqcap)))
	{
        printf("[TSC OMAP3] Binding IRQ%d to the ICU failed\n", irqno);
		return 1;
	}

	// was L4_IRQ_F_LEVEL_HIGH
	tag = l4_irq_attach(irqcap, 0, thread_cap);
	if ((err = l4_error(tag)))
	{
        printf("[TSC OMAP3] Error attaching to IRQ %d: %d\n", irqno, err);
		return 1;
	}

	while (1)
	{

		tag = l4_irq_receive(irqcap, L4_IPC_NEVER);
		if ((err = l4_ipc_error(tag, l4_utcb())))
		{
            printf("[TSC OMAP3]  Error: Receive irq failed %d\n", err);
			continue;
		}

		*(volatile unsigned int*)(gpio4_base + 0x1C) = 0;

        if (!(*(volatile unsigned int*)(gpio4_base + 0x38) & (1<<18)) && 1)
		{
            need_sync = 1;
            empty_cnt = 0;
            valid_cnt = 0;

            // pwr on
            ads7846_wr(ADS7846E_S | ADS7846E_ADD_DFR_X |  ADS_PD10_ADC_ON | ADS_PD10_REF_ON);
            l4_usleep(5000);

            while(1) {
				create_motion_event();
                if (empty_cnt > 10)
                {
                    // pwr down
                    ads7846_wr(ADS7846E_S);
                    l4_usleep(50000);

                    if ( (*(volatile unsigned int*)(gpio4_base + 0x38) & (1<<18)) && 1 )
                        break;

                    need_sync = 1;
                    empty_cnt = 0;
                    valid_cnt = 0;
                }
            };

			// generate touch end event;
            if (need_sync == 0)
            {
                Input_event ev2 = { L4RE_EV_KEY, L4RE_BTN_TOUCH, 0 };
                tsc_handler(ev2, tsc_priv);
            }
		}

		// clear flag and enable interrupt
		(*(volatile unsigned int*)(gpio4_base + 0x18)) = (1<<18);
        *(volatile unsigned int*)(gpio4_base + 0x1C) = (1<<18);
	}
}

static void* __irq_func(void *data)
{
	int ret = tsc_irq_func();
    printf("[TSC OMAP3]  Warning: irq handler returned with:%d\n", ret);
	l4_sleep_forever();
}

static const char *tsc_get_info(void)
{
	return "Gumstix Overo TSC";
}

static int tsc_probe(const char *name)
{
	if (strcmp(omap_dev_name, name)) {
        printf("[TSC OMAP3]  I'm not the right driver for [%s]\n", name);
		return 0;
	}
	return !l4io_lookup_device(omap_dev_name, NULL, 0, 0);
}

static void tsc_attach(Input_handler handler, void *priv)
{
	tsc_handler = handler;
	tsc_priv = priv;
	pthread_attr_t thread_attr;

	int err;
	if ((err = pthread_attr_init(&thread_attr)) != 0)
        printf("[TSC OMAP3]  Error: Initializing pthread attr: %d", err);

	struct sched_param sp;
	sp.sched_priority = 0x20;
	pthread_attr_setschedpolicy(&thread_attr, SCHED_L4);
	pthread_attr_setschedparam(&thread_attr, &sp);
	pthread_attr_setinheritsched(&thread_attr, PTHREAD_EXPLICIT_SCHED);

	err = pthread_create(&_pthread, &thread_attr, __irq_func, 0);
	if (err != 0)
        printf("[TSC OMAP3]  Error: Creating thread");
}

static void tsc_enable(void)
{
	if (tsc_init())
	{
        printf("[TSC OMAP3]  Init failed!\n");
		return;
	}
}

static void tsc_disable(void)
{}

static struct arm_input_ops arm_tsc_ops_omap3 = {
	.get_info           = tsc_get_info,
	.probe              = tsc_probe,
	.attach             = tsc_attach,
	.enable             = tsc_enable,
	.disable            = tsc_disable,
};

arm_input_register(&arm_tsc_ops_omap3);
