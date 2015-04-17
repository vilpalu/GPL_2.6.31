#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/proc_fs.h>
/*#include <linux/miscdevice.h>*/
#include <asm/types.h>
#include <asm/irq.h>
#include <asm/delay.h>
#include <asm/system.h>

#include <linux/mtd/mtd.h>
#include <linux/cdev.h>
#include <linux/irqreturn.h>

#include <atheros.h>

/*
 * IOCTL Command Codes
 */

#define AR7240_GPIO_IOCTL_BASE			0x01
#define AR7240_GPIO_IOCTL_CMD1      	AR7240_GPIO_IOCTL_BASE
#define AR7240_GPIO_IOCTL_CMD2      	AR7240_GPIO_IOCTL_BASE + 0x01
#define AR7240_GPIO_IOCTL_CMD3      	AR7240_GPIO_IOCTL_BASE + 0x02
#define AR7240_GPIO_IOCTL_CMD4      	AR7240_GPIO_IOCTL_BASE + 0x03
#define AR7240_GPIO_IOCTL_CMD5      	AR7240_GPIO_IOCTL_BASE + 0x04
#define AR7240_GPIO_IOCTL_CMD6      	AR7240_GPIO_IOCTL_BASE + 0x05
#define AR7240_GPIO_IOCTL_CMD7      	AR7240_GPIO_IOCTL_BASE + 0x06
#define AR7240_GPIO_IOCTL_MAX			AR7240_GPIO_IOCTL_CMD7

#define AR7240_GPIO_MAGIC 				0xB2
#define	AR7240_GPIO_BTN_READ			_IOR(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD1, int)
#define	AR7240_GPIO_LED_READ			_IOR(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD2, int)
#define	AR7240_GPIO_LED_WRITE			_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD3, int)

#define AR7240_GPIO_USB_LED1_WRITE		_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD4, int)

#define AR7240_GPIO_USB_1_LED1_WRITE	_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD5, int)
#define AR7240_GPIO_USB_POWER_WRITE		_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD6, int)
#define AR7240_GPIO_USB_1_POWER_WRITE	_IOW(AR7240_GPIO_MAGIC, AR7240_GPIO_IOCTL_CMD7, int)

#define gpio_major      				238
#define gpio_minor      				0

/*
 * GPIO assignment
 */

/*
 * RST_WPS Button		SYS_LED		WPS_LED		WIFI_RADIO_SW	USB_0_LED	USB_1_LED
 		16				   14		   15			17				11			12			
 */
 
/* reset default(WPS) button, default is GPIO16 */
#define RST_DFT_GPIO		CONFIG_GPIO_RESET_FAC_BIT
/* How long the user must press the button before Router rst, default is 5 */
#define RST_HOLD_TIME		CONFIG_GPIO_FAC_RST_HOLD_TIME

/* system LED, default is GPIO14 	*/
#define SYS_LED_GPIO		CONFIG_GPIO_READY_STATUS_BIT
/* system LED's value when off, default is 1 */
#define SYS_LED_OFF         (!CONFIG_GPIO_READY_STATUS_ON)

/* do not mux the RST&WPS button */
#ifndef CONFIG_MUX_RESET_WPS_BUTTON
#define WPS_BUTTON_GPIO		CONFIG_GPIO_WPSSTART_SW_BIT
#endif

/* QSS default button, default is None, we use RST&QSS MUX button */
/* QSS(WPS) LED, default is GPIO15 */
#define TRICOLOR_LED_GREEN_PIN  CONFIG_GPIO_JUMPSTART_LED_BIT
/* jump start LED's value when off */
#define OFF     (!CONFIG_GPIO_JUMPSTART_LED_ON)
/* jump start LED'S value when on, default is 0 */
#define ON      CONFIG_GPIO_JUMPSTART_LED_ON
/* WiFi switch, default is GPIO17 */
#define WIFI_RADIO_SW_GPIO	CONFIG_GPIO_WIFI_SWITCH_BIT

/* USB 0 by lyj, 31Aug11 */
#ifdef CONFIG_GPIO_USB_0_LED_BIT
/* USB 0 LED, default is GPIO11 */
#define AP_USB_LED_GPIO     CONFIG_GPIO_USB_0_LED_BIT
/* USB 0 LED's value when off */
#define USB_LED_OFF         (!CONFIG_GPIO_USB_0_LED_ON)
/* USB 0 LED's value when on, default is 0 */
#define USB_LED_ON          CONFIG_GPIO_USB_0_LED_ON

/* for USB 3G by lyj, 31Aug11 */
/* usb power switch for usb port 0 */
#define USB_POWER_SW_GPIO	CONFIG_GPIO_USB_0_SWITCHFOR3G_BIT
#define USB_POWER_ON		1
#define USB_POWER_OFF		(!USB_POWER_ON)

#else
#undef AP_USB_LED_GPIO
#undef USB_POWER_SW_GPIO
#endif

/* USB 1 LED by lyj, 31Aug11 */
#ifdef CONFIG_GPIO_USB_1_LED_BIT
/* USB 1 LED, default is GPIO11 */
#define AP_USB_1_LED_GPIO     CONFIG_GPIO_USB_1_LED_BIT
/* USB 1 LED's value when off */
#define USB_1_LED_OFF         (!CONFIG_GPIO_USB_1_LED_ON)
/* USB 1 LED's value when on, default is 0 */
#define USB_1_LED_ON          CONFIG_GPIO_USB_1_LED_ON

#define USB_1_POWER_SW_GPIO	CONFIG_GPIO_USB_1_SWITCHFOR3G_BIT
#define USB_1_POWER_ON		1
#define USB_1_POWER_OFF		(!USB_1_POWER_ON)

#else
#undef AP_USB_1_LED_GPIO
#undef USB_1_POWER_SW_GPIO
#endif


int counter = 0;
int jiff_when_press = 0;
int bBlockWps = 1;
struct timer_list rst_timer;

int g_usbLedBlinkCountDown = 1;
int wifiButtonStatus = 0;

#ifdef CONFIG_MUX_RESET_WPS_BUTTON
/* control params for reset button reuse, by zjg, 13Apr10 */
static int l_bMultiUseResetButton		=	0;
static int l_bWaitForQss				= 	1;
#endif

EXPORT_SYMBOL(g_usbLedBlinkCountDown);

/*
 * GPIO interrupt stuff
 */
typedef enum {
	INT_TYPE_EDGE,
	INT_TYPE_LEVEL,
} ath_gpio_int_type_t;

typedef enum {
	INT_POL_ACTIVE_LOW,
	INT_POL_ACTIVE_HIGH,
} ath_gpio_int_pol_t;

/*
** Simple Config stuff
*/
#if 0
#if !defined(IRQ_NONE)
#define IRQ_NONE
#define IRQ_HANDLED
#endif /* !defined(IRQ_NONE) */
#endif


/*changed by hujia.*/
typedef irqreturn_t(*sc_callback_t)(int, void *, struct pt_regs *, void *);

/*
 * Multiple Simple Config callback support
 * For multiple radio scenarios, we need to post the button push to
 * all radios at the same time.  However, there is only 1 button, so
 * we only have one set of GPIO callback pointers.
 *
 * Creating a structure that contains each callback, tagged with the
 * name of the device registering the callback.  The unregister routine
 * will need to determine which element to "unregister", so the device
 * name will have to be passed to unregister also
 */

typedef struct {
	char		*name;
	sc_callback_t	registered_cb;
	void		*cb_arg1;
	void		*cb_arg2;
} multi_callback_t;

/*
 * Specific instance of the callback structure
 */
static multi_callback_t sccallback[2];

/* not mux the RST&WPS Button */
#ifndef CONFIG_MUX_RESET_WPS_BUTTON
static int ignore_pushbutton = 1;
#endif

static struct proc_dir_entry *simple_config_entry = NULL;
static struct proc_dir_entry *simulate_push_button_entry = NULL;
static struct proc_dir_entry *tricolor_led_entry = NULL;
static struct proc_dir_entry *wifi_button_entry = NULL;

#ifdef CONFIG_MUX_RESET_WPS_BUTTON
/* added by zjg, 12Apr10 */
static struct proc_dir_entry *multi_use_reset_button_entry = NULL;
#endif

/* ZJin 100317: for 3g usb led blink feature, use procfs simple config. */
static struct proc_dir_entry *usb_led_blink_entry = NULL;

#ifndef CONFIG_NO_USB
/*added by  ZQQ<10.06.02 for usb power*/
static struct proc_dir_entry *usb_power_entry = NULL;
#endif

static int ignore_wifibutton = 1;

void ath_gpio_config_int(int gpio,
			 ath_gpio_int_type_t type,
			 ath_gpio_int_pol_t polarity)
{
	u32 val;

	/*
	 * allow edge sensitive/rising edge too
	 */
	if (type == INT_TYPE_LEVEL) {
		/* level sensitive */
		ath_reg_rmw_set(ATH_GPIO_INT_TYPE, (1 << gpio));
	} else {
		/* edge triggered */
		val = ath_reg_rd(ATH_GPIO_INT_TYPE);
		val &= ~(1 << gpio);
		ath_reg_wr(ATH_GPIO_INT_TYPE, val);
	}

	if (polarity == INT_POL_ACTIVE_HIGH) {
		ath_reg_rmw_set(ATH_GPIO_INT_POLARITY, (1 << gpio));
	} else {
		val = ath_reg_rd(ATH_GPIO_INT_POLARITY);
		val &= ~(1 << gpio);
		ath_reg_wr(ATH_GPIO_INT_POLARITY, val);
	}

	ath_reg_rmw_set(ATH_GPIO_INT_ENABLE, (1 << gpio));
}

void ath_gpio_config_output(int gpio)
{
#ifdef CONFIG_MACH_AR934x
	ath_reg_rmw_clear(ATH_GPIO_OE, (1 << gpio));
#else
	ath_reg_rmw_set(ATH_GPIO_OE, (1 << gpio));
#endif
}
EXPORT_SYMBOL(ath_gpio_config_output);

void ath_gpio_config_input(int gpio)
{
#ifdef CONFIG_MACH_AR934x
	ath_reg_rmw_set(ATH_GPIO_OE, (1 << gpio));
#else
	ath_reg_rmw_clear(ATH_GPIO_OE, (1 << gpio));
#endif
}

void ath_gpio_out_val(int gpio, int val)
{
	if (val & 0x1) {
		ath_reg_rmw_set(ATH_GPIO_OUT, (1 << gpio));
	} else {
		ath_reg_rmw_clear(ATH_GPIO_OUT, (1 << gpio));
	}
}

EXPORT_SYMBOL(ath_gpio_out_val);

int ath_gpio_in_val(int gpio)
{
	return ((1 << gpio) & (ath_reg_rd(ATH_GPIO_IN)));
}

static void
ath_gpio_intr_enable(unsigned int irq)
{
	ath_reg_rmw_set(ATH_GPIO_INT_MASK,
				(1 << (irq - ATH_GPIO_IRQ_BASE)));
}

static void
ath_gpio_intr_disable(unsigned int irq)
{
	ath_reg_rmw_clear(ATH_GPIO_INT_MASK,
				(1 << (irq - ATH_GPIO_IRQ_BASE)));
}

static unsigned int
ath_gpio_intr_startup(unsigned int irq)
{
	ath_gpio_intr_enable(irq);
	return 0;
}

static void
ath_gpio_intr_shutdown(unsigned int irq)
{
	ath_gpio_intr_disable(irq);
}

static void
ath_gpio_intr_ack(unsigned int irq)
{
	ath_gpio_intr_disable(irq);
}

static void
ath_gpio_intr_end(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		ath_gpio_intr_enable(irq);
}

static int
ath_gpio_intr_set_affinity(unsigned int irq, const struct cpumask *dest)
{
	/*
	 * Only 1 CPU; ignore affinity request
	 */
	return 0;
}

struct irq_chip /* hw_interrupt_type */ ath_gpio_intr_controller = {
	.name = "ATH GPIO",
	.startup = ath_gpio_intr_startup,
	.shutdown = ath_gpio_intr_shutdown,
	.enable = ath_gpio_intr_enable,
	.disable = ath_gpio_intr_disable,
	.ack = ath_gpio_intr_ack,
	.end = ath_gpio_intr_end,
	.eoi = ath_gpio_intr_end,
	.set_affinity = ath_gpio_intr_set_affinity,
};

void ath_gpio_irq_init(int irq_base)
{
	int i;

	for (i = irq_base; i < irq_base + ATH_GPIO_IRQ_COUNT; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		//irq_desc[i].chip = &ath_gpio_intr_controller;
		set_irq_chip_and_handler(i, &ath_gpio_intr_controller,
					 handle_percpu_irq);
	}
}

/*
 *  USB GPIO control
 */

void ap_usb_led_on(void)
{
	/* we do not need this function, and usb hub drivers will call this function to let led on by lyj, 26Sep11 */
#if 0
#ifdef AP_USB_LED_GPIO  
	ath_gpio_out_val(AP_USB_LED_GPIO, USB_LED_ON);
#endif
#endif
}

EXPORT_SYMBOL(ap_usb_led_on);

void ap_usb_led_off(void)
{
#if 0
#ifdef AP_USB_LED_GPIO
	ath_gpio_out_val(AP_USB_LED_GPIO, USB_LED_OFF);
#endif
#endif
}

EXPORT_SYMBOL(ap_usb_led_off);

/*
 * USB 1 GPIO Control
 */

void ap_usb_1_led_on(void)
{
#ifdef AP_USB_1_LED_GPIO  
	ath_gpio_out_val(AP_USB_1_LED_GPIO, USB_1_LED_ON);
#endif
}

EXPORT_SYMBOL(ap_usb_1_led_on);

void ap_usb_1_led_off(void)
{
#ifdef AP_USB_1_LED_GPIO
	ath_gpio_out_val(AP_USB_1_LED_GPIO, USB_1_LED_OFF);
#endif
}

EXPORT_SYMBOL(ap_usb_1_led_off);

/*changed by hujia.*/
/*void register_simple_config_callback (void *callback, void *arg)*/
int32_t register_simple_config_callback(char *cbname, void *callback, void *arg1, void *arg2)
{
    printk("SC Callback Registration for %s\n", cbname);
	if (!sccallback[0].name) {
		sccallback[0].name = (char*)kmalloc(strlen(cbname), GFP_KERNEL);
		strcpy(sccallback[0].name, cbname);
		sccallback[0].registered_cb = (sc_callback_t) callback;
		sccallback[0].cb_arg1 = arg1;
		sccallback[0].cb_arg2 = arg2;
	} else if (!sccallback[1].name) {
		sccallback[1].name = (char*)kmalloc(strlen(cbname), GFP_KERNEL);
		strcpy(sccallback[1].name, cbname);
		sccallback[1].registered_cb = (sc_callback_t) callback;
		sccallback[1].cb_arg1 = arg1;
		sccallback[1].cb_arg2 = arg2;
	} else {
		printk("@@@@ Failed SC Callback Registration for %s\n", cbname);
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(register_simple_config_callback);

int32_t unregister_simple_config_callback(char *cbname)
{
	if (sccallback[1].name && strcmp(sccallback[1].name, cbname) == 0) {
		kfree(sccallback[1].name);
		sccallback[1].name = NULL;
		sccallback[1].registered_cb = NULL;
		sccallback[1].cb_arg1 = NULL;
		sccallback[1].cb_arg2 = NULL;
	} else if (sccallback[0].name && strcmp(sccallback[0].name, cbname) == 0) {
		kfree(sccallback[0].name);
		sccallback[0].name = NULL;
		sccallback[0].registered_cb = NULL;
		sccallback[0].cb_arg1 = NULL;
		sccallback[0].cb_arg2 = NULL;
	} else {
		printk("!&!&!&!& ERROR: Unknown callback name %s\n", cbname);
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(unregister_simple_config_callback);

/* not mux the RST&WPS Button */
#ifndef CONFIG_MUX_RESET_WPS_BUTTON
/*
 * Irq for front panel SW wps start switch
 * Connected to XSCALE through GPIO4
 */
irqreturn_t wpsStart_irq(int cpl, void *dev_id)
{
		if (ignore_pushbutton) {
			ath_gpio_config_int(WPS_BUTTON_GPIO, INT_TYPE_LEVEL,
						INT_POL_ACTIVE_HIGH);
			ignore_pushbutton = 0;
			return IRQ_HANDLED;
		}

    ath_gpio_config_int (WPS_BUTTON_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);
		ignore_pushbutton = 1;

    printk ("Wps start button pressed.\n");

	/* dual band */
	if (sccallback[0].registered_cb) {
			sccallback[0].registered_cb (cpl, sccallback[0].cb_arg1, NULL, sccallback[0].cb_arg2);
	}
	if (sccallback[1].registered_cb) {
		sccallback[1].registered_cb (cpl, sccallback[1].cb_arg1, NULL, sccallback[1].cb_arg2);
	}
	
    return IRQ_HANDLED;
}
#endif

static int ignore_rstbutton = 1;

/* irq handler for reset button */
static irqreturn_t rst_irq(int cpl, void *dev_id)
{
    if (ignore_rstbutton)
	{
        ath_gpio_config_int(RST_DFT_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_HIGH);
        ignore_rstbutton = 0;

		mod_timer(&rst_timer, jiffies + RST_HOLD_TIME * HZ);

        return IRQ_HANDLED;
	}

    ath_gpio_config_int (RST_DFT_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);
    ignore_rstbutton = 1;

	printk("Reset button pressed.\n");

	/* mux the RST&WPS Button */
#ifdef CONFIG_MUX_RESET_WPS_BUTTON	
	/* mark reset status, by zjg, 12Apr10 */
	if (!bBlockWps && l_bMultiUseResetButton && l_bWaitForQss) 
	{
		if (sccallback[0].registered_cb) {
				sccallback[0].registered_cb (cpl, sccallback[0].cb_arg1, NULL, sccallback[0].cb_arg2);
		}
		if (sccallback[1].registered_cb) {
			sccallback[1].registered_cb (cpl, sccallback[1].cb_arg1, NULL, sccallback[1].cb_arg2);
		}	
	}
#endif

	return IRQ_HANDLED;
}

void check_rst(unsigned long nothing)
{
	if (!ignore_rstbutton)
	{
		printk ("restoring factory default...\n");
    	counter ++;

#ifdef CONFIG_MUX_RESET_WPS_BUTTON
		if (l_bMultiUseResetButton)
		{
			/* to mark reset status, forbid QSS, added by zjg, 12Apr10 */
			l_bWaitForQss	= 0;
		}
#endif
	}
}


/* irq handler for wifi switch */
static irqreturn_t wifi_sw_irq(int cpl, void *dev_id)
{
    if (ignore_wifibutton)
	{
		ath_gpio_config_int (WIFI_RADIO_SW_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_HIGH);
		ignore_wifibutton = 0;
		wifiButtonStatus = 0; /*open wifi*/
		return IRQ_HANDLED;
	}

    ath_gpio_config_int (WIFI_RADIO_SW_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);
    ignore_wifibutton = 1;
	wifiButtonStatus = 1; /*close wifi*/

	printk("WIFI button pressed.\n");

	return IRQ_HANDLED;
}

#ifdef CONFIG_MUX_RESET_WPS_BUTTON
static int multi_use_reset_button_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", l_bMultiUseResetButton);
}

static int multi_use_reset_button_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    u_int32_t val;

	if (sscanf(buf, "%d", &val) != 1)
        return -EINVAL;

	/* only admit "0" or "1" */
	if ((val < 0) || (val > 1))
		return -EINVAL;	

	l_bMultiUseResetButton = val;
	
	return count;

}
#endif

static int push_button_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return 0;
}

static int push_button_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    if (sccallback[0].registered_cb) {
        sccallback[0].registered_cb (0, sccallback[0].cb_arg1, 0, sccallback[0].cb_arg2);
    }
    if (sccallback[1].registered_cb) {
        sccallback[1].registered_cb (0, sccallback[1].cb_arg1, 0, sccallback[1].cb_arg2);
    }
    return count;
}

static int usb_led_blink_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", g_usbLedBlinkCountDown);
}

static int usb_led_blink_write (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    u_int32_t val;

	if (sscanf(buf, "%d", &val) != 1)
        return -EINVAL;

	if ((val < 0) || (val > 1))
		return -EINVAL;

	g_usbLedBlinkCountDown = val;

	return count;
}

static int wifi_button_read (char *page, char **start, off_t off,
                               int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", wifiButtonStatus);
}

static int wifi_button_wirte (struct file *file, const char *buf,
                                        unsigned long count, void *data)
{
    u_int32_t val;

	if (sscanf(buf, "%d", &val) != 1)
        return -EINVAL;

	if ((val < 0) || (val > 1))
		return -EINVAL;

	wifiButtonStatus = val;

	return count;
}




/* for usb 3G by lyj, 31Aug11 */
#ifndef CONFIG_NO_USB
/*added by ZQQ,10.06.02*/
static int usb_power_read (char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	return 0;
}

static int usb_power_write (struct file *file, const char *buf,
				unsigned long count, void *data)
{
    u_int32_t val = 0;

	if (sscanf(buf, "%d", &val) != 1)
        return -EINVAL;

	if ((val < 0) || (val > 1))
		return -EINVAL;

	printk("%s %d: write gpio:value = %d\r\n",__FUNCTION__,__LINE__,val);

	#ifdef USB_POWER_SW_GPIO
	if (USB_POWER_ON == val)
	{
		ath_gpio_out_val(USB_POWER_SW_GPIO, USB_POWER_ON);
	}
	else
	{
		ath_gpio_out_val(USB_POWER_SW_GPIO, USB_POWER_OFF);
	}
	#endif
	
	return count;
}
#endif

typedef enum {
        LED_STATE_OFF   =       1,
        LED_STATE_GREEN =       0,
        LED_STATE_YELLOW =      2,
        LED_STATE_ORANGE =      3,
        LED_STATE_MAX =         4
} led_state_e;

static led_state_e gpio_tricolorled = LED_STATE_OFF;

static int gpio_tricolor_led_read (char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
    return sprintf (page, "%d\n", gpio_tricolorled);
}

static int gpio_tricolor_led_write (struct file *file, const char *buf,
					unsigned long count, void *data)
{
    u_int32_t val, green_led_onoff = 0, yellow_led_onoff = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

    if (val >= LED_STATE_MAX)
        return -EINVAL;

    if (val == gpio_tricolorled)
		return count;

    switch (val) {
        case LED_STATE_OFF :
                green_led_onoff = OFF;   /* both LEDs OFF */
                yellow_led_onoff = OFF;
                break;

        case LED_STATE_GREEN:
                green_led_onoff = ON;    /* green ON, Yellow OFF */
                yellow_led_onoff = OFF;
                break;

        case LED_STATE_YELLOW:
                green_led_onoff = OFF;   /* green OFF, Yellow ON */
                yellow_led_onoff = ON;
                break;
    
        case LED_STATE_ORANGE:
                green_led_onoff = ON;    /* both LEDs ON */
                yellow_led_onoff = ON;
                break;
	}

    ath_gpio_out_val (TRICOLOR_LED_GREEN_PIN, green_led_onoff);
    //ar7240_gpio_out_val (TRICOLOR_LED_YELLOW_PIN, yellow_led_onoff);
    gpio_tricolorled = val;

    return count;
}


static int create_simple_config_led_proc_entry(void)
{
	if (simple_config_entry != NULL) {
		printk("Already have a proc entry for /proc/simple_config!\n");
		return -ENOENT;
	}

	simple_config_entry = proc_mkdir("simple_config", NULL);
	if (!simple_config_entry)
		return -ENOENT;

	simulate_push_button_entry = create_proc_entry("push_button", 0644,
							simple_config_entry);
	if (!simulate_push_button_entry)
		return -ENOENT;

	simulate_push_button_entry->write_proc = push_button_write;
	simulate_push_button_entry->read_proc = push_button_read;

#ifdef CONFIG_MUX_RESET_WPS_BUTTON
	/* added by zjg, 12Apr10 */
	multi_use_reset_button_entry = create_proc_entry ("multi_use_reset_button", 0644,
                                                      simple_config_entry);
    if (!multi_use_reset_button_entry)
        return -ENOENT;

    multi_use_reset_button_entry->write_proc	= multi_use_reset_button_write;
    multi_use_reset_button_entry->read_proc 	= multi_use_reset_button_read;
	/* end added */
#endif

    tricolor_led_entry = create_proc_entry ("tricolor_led", 0644,
							simple_config_entry);
    if (!tricolor_led_entry)
		return -ENOENT;

    tricolor_led_entry->write_proc = gpio_tricolor_led_write;
    tricolor_led_entry->read_proc = gpio_tricolor_led_read;

	/* for usb led blink */
	usb_led_blink_entry = create_proc_entry ("usb_blink", 0666,
                                                      simple_config_entry);
	if (!usb_led_blink_entry)
		return -ENOENT;
	
    usb_led_blink_entry->write_proc = usb_led_blink_write;
    usb_led_blink_entry->read_proc = usb_led_blink_read;


	wifi_button_entry = create_proc_entry ("wifi_button", 0644,
							simple_config_entry);
	if(!wifi_button_entry)
		return -ENOENT;

	wifi_button_entry->write_proc = wifi_button_wirte;
	wifi_button_entry->read_proc = wifi_button_read;

	/* if no usb, no need to init usb power proc entry */
#ifndef CONFIG_NO_USB	
	/*added by ZQQ, 10.06.02 for usb power*/
	usb_power_entry = create_proc_entry("usb_power", 0666, simple_config_entry);
	if(!usb_power_entry)
		return -ENOENT;

	usb_power_entry->write_proc = usb_power_write;
	usb_power_entry->read_proc = usb_power_read;	
	/*end added*/
#endif

	/* configure gpio as outputs */
    ath_gpio_config_output (TRICOLOR_LED_GREEN_PIN); 
    //ar7240_gpio_config_output (TRICOLOR_LED_YELLOW_PIN); 

	/* switch off the led */
	/* TRICOLOR_LED_GREEN_PIN is poll up, so ON is OFF modified by tiger 09/07/15 */
    ath_gpio_out_val(TRICOLOR_LED_GREEN_PIN, ON);
    //ar7240_gpio_out_val(TRICOLOR_LED_YELLOW_PIN, OFF);

	return 0;
}



/******* begin ioctl stuff **********/
#ifdef CONFIG_GPIO_DEBUG
void print_gpio_regs(char* prefix)
{
	printk("\n-------------------------%s---------------------------\n", prefix);
	printk("AR7240_GPIO_OE:%#X\n", ar7240_reg_rd(AR7240_GPIO_OE));
	printk("AR7240_GPIO_IN:%#X\n", ar7240_reg_rd(AR7240_GPIO_IN));
	printk("AR7240_GPIO_OUT:%#X\n", ar7240_reg_rd(AR7240_GPIO_OUT));
	printk("AR7240_GPIO_SET:%#X\n", ar7240_reg_rd(AR7240_GPIO_SET));
	printk("AR7240_GPIO_CLEAR:%#X\n", ar7240_reg_rd(AR7240_GPIO_CLEAR));
	printk("AR7240_GPIO_INT_ENABLE:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_ENABLE));
	printk("AR7240_GPIO_INT_TYPE:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_TYPE));
	printk("AR7240_GPIO_INT_POLARITY:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_POLARITY));
	printk("AR7240_GPIO_INT_PENDING:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_PENDING));
	printk("AR7240_GPIO_INT_MASK:%#X\n", ar7240_reg_rd(AR7240_GPIO_INT_MASK));
	printk("\n-------------------------------------------------------\n");
	}
#endif

/* ioctl for reset default detection and system led switch*/
int ar7240_gpio_ioctl(struct inode *inode, struct file *file,  unsigned int cmd, unsigned long arg)
{
	int i;
	int* argp = (int *)arg;

	if (_IOC_TYPE(cmd) != AR7240_GPIO_MAGIC ||
		_IOC_NR(cmd) < AR7240_GPIO_IOCTL_BASE ||
		_IOC_NR(cmd) > AR7240_GPIO_IOCTL_MAX)
	{
		printk("type:%d nr:%d\n", _IOC_TYPE(cmd), _IOC_NR(cmd));
		printk("ar7240_gpio_ioctl:unknown command\n");
		return -1;
	}

	switch (cmd)
	{
	case AR7240_GPIO_BTN_READ:
		*argp = counter;
		counter = 0;
		break;
			
	case AR7240_GPIO_LED_READ:
		printk("\n\n");
		for (i = 0; i < ATH_GPIO_COUNT; i ++)
		{
			printk("pin%d: %d\n", i, ath_gpio_in_val(i));
		}
		printk("\n");

	#ifdef CONFIG_GPIO_DEBUG
		print_gpio_regs("");
	#endif
			
		break;
			
	case AR7240_GPIO_LED_WRITE:
		if (unlikely(bBlockWps))
			bBlockWps = 0;
		ath_gpio_out_val(SYS_LED_GPIO, *argp);	/* PB92 use gpio 1 to config switch */
		break;

	case AR7240_GPIO_USB_LED1_WRITE:
			#ifdef AP_USB_LED_GPIO
			ath_gpio_out_val(AP_USB_LED_GPIO, *argp);
			#endif
			break;
			
	case AR7240_GPIO_USB_1_LED1_WRITE:
			#ifdef AP_USB_1_LED_GPIO
			ath_gpio_out_val(AP_USB_1_LED_GPIO, *argp);
			#endif
			break;
			
	case AR7240_GPIO_USB_POWER_WRITE:
			#ifdef USB_POWER_SW_GPIO
			ath_gpio_out_val(USB_POWER_SW_GPIO, *argp);
			#endif
			break;

	case AR7240_GPIO_USB_1_POWER_WRITE:
			#ifdef USB_1_POWER_SW_GPIO
			ath_gpio_out_val(USB_1_POWER_SW_GPIO, *argp);
			#endif
			break;
			
	default:
		printk("command not supported\n");
		return -1;
	}


	return 0;
}


int ar7240_gpio_open (struct inode *inode, struct file *filp)
{
	int minor = iminor(inode);
	int devnum = minor; //>> 1;
	struct mtd_info *mtd;

	if ((filp->f_mode & 2) && (minor & 1))
{
		printk("You can't open the RO devices RW!\n");
		return -EACCES;
}

	mtd = get_mtd_device(NULL, devnum);   
	if (!mtd)
{
		printk("Can not open mtd!\n");
		return -ENODEV;	
	}
	filp->private_data = mtd;
	return 0;

}

/* struct for cdev */
struct file_operations gpio_device_op =
{
	.owner = THIS_MODULE,
	.ioctl = ar7240_gpio_ioctl,
	.open = ar7240_gpio_open,
};

/* struct for ioctl */
static struct cdev gpio_device_cdev =
{
	.owner  = THIS_MODULE,
	.ops	= &gpio_device_op,
};
/******* end  ioctl stuff **********/

#if 0
static void ath_config_eth_led(void)
{
	uint32_t reg_value;

	/*WAN, GPIO19, DS3*/
	ath_gpio_config_output(19);
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<24);
	reg_value = ath_reg_rd(ATH_GPIO_OUT_FUNCTION4);
	reg_value = reg_value | (41 << 24);
	ath_reg_wr(ATH_GPIO_OUT_FUNCTION4, reg_value);
	printk("ATH_GPIO_OUT_FUNCTION4: 0x%x\n", ath_reg_rd(ATH_GPIO_OUT_FUNCTION4));

	/* LAN1, GPIO20, DS4 */
	ath_gpio_config_output(20);
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION5, 0xff<<0);
	reg_value = ath_reg_rd(ATH_GPIO_OUT_FUNCTION5);
	reg_value = reg_value | (42 << 0);
	ath_reg_wr(ATH_GPIO_OUT_FUNCTION5, reg_value);
	/* it's ok */

	/* LAN2, GPIO21, DS5 */
	ath_gpio_config_output(21);
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION5, 0xff<<8);
	reg_value = ath_reg_rd(ATH_GPIO_OUT_FUNCTION5);
	reg_value = reg_value | (43 << 8);
	ath_reg_wr(ATH_GPIO_OUT_FUNCTION5, reg_value);
	printk("ATH_GPIO_OUT_FUNCTION5: 0x%x\n", ath_reg_rd(ATH_GPIO_OUT_FUNCTION5));

	/* LAN3, GPIO12, DS6 */
	ath_gpio_config_output(12);
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION3, 0xff<<0);
	reg_value = ath_reg_rd(ATH_GPIO_OUT_FUNCTION3);
	reg_value = reg_value | (44 << 0);
	ath_reg_wr(ATH_GPIO_OUT_FUNCTION3, reg_value);
	/* it's ok */
	

	/* LAN4, GPIO18, DS7 */
	ath_gpio_config_output(18);
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<16);
	reg_value = ath_reg_rd(ATH_GPIO_OUT_FUNCTION4);
	reg_value = reg_value | (45 << 16);
	ath_reg_wr(ATH_GPIO_OUT_FUNCTION4, reg_value);
	printk("ATH_GPIO_OUT_FUNCTION4: 0x%x\n", ath_reg_rd(ATH_GPIO_OUT_FUNCTION4));


}
#endif

int __init ar7240_simple_config_init(void)
{
    int req;

	/* restore factory default and system led */
	dev_t dev;
    int rt;
	int current_wifi_value;
    int ar7240_gpio_major = gpio_major;
    int ar7240_gpio_minor = gpio_minor;

	init_timer(&rst_timer);
	rst_timer.function = check_rst;

	/* config gpio 11, 12, 14, 15, 16, 17 as normal gpio function */
	/* gpio11 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION2, 0xff<<24);
	/* gpio12 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION3, 0xff<<0);
	/* gpio14 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION3, 0xff<<16);
	/* gpio15 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION3, 0xff<<24);
	/* gpio16 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<0);
	/* gpio17 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<8);


#ifndef CONFIG_MUX_RESET_WPS_BUTTON
	/* This is NECESSARY, lsz 090109 */
	ath_gpio_config_input(WPS_BUTTON_GPIO);

    /* configure JUMPSTART_GPIO as level triggered interrupt */
    ath_gpio_config_int (WPS_BUTTON_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);

    req = request_irq (ATH_GPIO_IRQn(WPS_BUTTON_GPIO), wpsStart_irq, 0,
                       "SW_WPSSTART", NULL);
    if (req != 0)
	{
        printk (KERN_ERR "unable to request IRQ for SWWPSSTART GPIO (error %d)\n", req);
    }
#endif

    create_simple_config_led_proc_entry ();

	ath_gpio_config_input(RST_DFT_GPIO);

	/* configure GPIO RST_DFT_GPIO as level triggered interrupt */
    ath_gpio_config_int (RST_DFT_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);

    rt = request_irq (ATH_GPIO_IRQn(RST_DFT_GPIO), rst_irq, 0,
                       "RESTORE_FACTORY_DEFAULT", NULL);
    if (rt != 0)
	{
        printk (KERN_ERR "unable to request IRQ for RESTORE_FACTORY_DEFAULT GPIO (error %d)\n", rt);
    }

	/* wifi switch! */
	ath_gpio_config_input(WIFI_RADIO_SW_GPIO);
	current_wifi_value = ath_reg_rd(ATH_GPIO_IN) & (1 << WIFI_RADIO_SW_GPIO);
	
	/* configure GPIO RST_DFT_GPIO as level triggered interrupt */
	if(current_wifi_value == 0)
	{
		ignore_wifibutton = 1;
		ath_gpio_config_int (WIFI_RADIO_SW_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_LOW);
	}
	else
	{
		ignore_wifibutton =0;
		ath_gpio_config_int (WIFI_RADIO_SW_GPIO, INT_TYPE_LEVEL, INT_POL_ACTIVE_HIGH);
	}

    req = request_irq (ATH_GPIO_IRQn(WIFI_RADIO_SW_GPIO), wifi_sw_irq, 0,
                       "WIFI_RADIO_SWITCH", NULL);
    if (req != 0)
	{
        printk (KERN_ERR "unable to request IRQ for WIFI_RADIO_SWITCH GPIO (error %d)\n", req);
    }

    if (ar7240_gpio_major)
	{
        dev = MKDEV(ar7240_gpio_major, ar7240_gpio_minor);
        rt = register_chrdev_region(dev, 1, "ar7240_gpio_chrdev");
    }
	else
	{
        rt = alloc_chrdev_region(&dev, ar7240_gpio_minor, 1, "ar7240_gpio_chrdev");
        ar7240_gpio_major = MAJOR(dev);
    }

    if (rt < 0)
	{
        printk(KERN_WARNING "ar7240_gpio_chrdev : can`t get major %d\n", ar7240_gpio_major);
        return rt;
	}

    cdev_init (&gpio_device_cdev, &gpio_device_op);
    rt = cdev_add(&gpio_device_cdev, dev, 1);
	
    if (rt < 0) 
		printk(KERN_NOTICE "Error %d adding ar7240_gpio_chrdev ", rt);

	#ifdef AP_USB_LED_GPIO
	ath_gpio_config_output(AP_USB_LED_GPIO);
	#endif
	#ifdef AP_USB_1_LED_GPIO
	ath_gpio_config_output(AP_USB_1_LED_GPIO);
	#endif
	ath_gpio_config_output(SYS_LED_GPIO);
	
	/* for USB 3G by lyj, 31Aug11 */
	#ifdef USB_POWER_SW_GPIO
	ath_gpio_config_output(USB_POWER_SW_GPIO);
	#endif
	#ifdef USB_1_POWER_SW_GPIO
	ath_gpio_config_output(USB_1_POWER_SW_GPIO);
	#endif

	/* s27 will use the gpio 18 19 as ethernet led */
	#ifdef CONFIG_SUPPORT_S17
	/* GPIO18-19 by lyj, 27Sep11 */
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<16);
	ath_reg_rmw_clear(ATH_GPIO_OUT_FUNCTION4, 0xff<<24);
	/* config GPIO18-19 as output by lyj, 27Sep11 */
	ath_gpio_config_output(18);
	ath_gpio_config_output(19);

	/* set GPIO18-19, for AR9344 art (LNA) by lyj, 27Sep11 */
	ath_reg_rmw_set(ATH_GPIO_OUT_FUNCTION4, 0x2f2e0000);
	#endif
	
	ath_gpio_out_val(SYS_LED_GPIO, SYS_LED_OFF);
	#ifdef AP_USB_LED_GPIO
	ath_gpio_out_val(AP_USB_LED_GPIO, USB_LED_OFF);
	#endif
	#ifdef AP_USB_1_LED_GPIO
	ath_gpio_out_val(AP_USB_1_LED_GPIO, USB_1_LED_OFF);
	#endif
	ath_gpio_out_val (TRICOLOR_LED_GREEN_PIN, OFF);
	
	/* for USB 3G by lyj, 31Aug11 */
	#ifdef USB_POWER_SW_GPIO
	ath_gpio_out_val(USB_POWER_SW_GPIO, USB_POWER_ON);
	#endif
	#ifdef USB_1_POWER_SW_GPIO
	ath_gpio_out_val(USB_1_POWER_SW_GPIO, USB_1_POWER_ON);
	#endif

	return 0;
}

subsys_initcall(ar7240_simple_config_init);
