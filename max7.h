/*
 * Multi-GNSS engine for GPS, GLONASS, Galileo and QZSS 
 *
 * Copyright (C) 2008-2010 Michael Hennerich, Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MAX7_H_
#define _MAX7_H_

#include <linux/types.h>

#define UBX_DDC_I2C_PORT_NO	0
#define UBX_UART1_PORT_NO	1
#define UBX_USB_PORT_NO		3
#define UBX_SPI_PORT_NO		4

#define PORT_COUNT		6 
#define NUM_OF_MSGS		3 /*No of msg configuration done*/

/*Refer ublox reciver description doc for list of config parameters & datatypes*/
struct navigation {
	struct {
        unsigned short dyn                : 1;
        unsigned short minEl              : 1;
        unsigned short posFixMode         : 1;
        unsigned short drLim              : 1;
        unsigned short posMask            : 1;
        unsigned short timeMask           : 1;
        unsigned short staticHoldMask     : 1;
        unsigned short dgpsMask           : 1;
        unsigned short reserved           : 8;
        }mask1;
	unsigned char dynModel;
	unsigned char fixMode;
	signed long fixedAlt;
	unsigned long fixedAltVar;
	signed char minElev;
	unsigned char drLimit;
	unsigned short pDop;
	unsigned short tDop;
	unsigned short pAcc;
	unsigned short tAcc;
	unsigned char staticHoldThresh;
	unsigned char dgpsTimeOut;
	unsigned char cnoThreshNumSVs;
	unsigned char cnoThresh;	
	unsigned short reserved2;
        unsigned long reserved3;
        unsigned long reserved4;      
};

struct poll_msg_cfg {
	unsigned char msgClass;
	unsigned char msgID;
};

struct set_get_msg_rates {
        unsigned char msgClass;
        unsigned char msgID;
        unsigned char rate[PORT_COUNT];
};

struct set_msg_rate {
        unsigned char msgClass;
        unsigned char msgID;
        unsigned char rate;
};

struct message {                        
         struct poll_msg_cfg cfg_req;   			/*Poll Message configuration request to ublox 7*/
         struct set_get_msg_rates cfg_req_res[NUM_OF_MSGS];   	/*Respose from ublox 7 for Poll message config request
								Or Request to ublox 7 to set Message configuration
								rate for multiple ports*/
         struct set_msg_rate cfg_cur_port;       		/*Request to ublox 7 to set Message configuration
                                                  		rate for current port*/
};
struct csl_mask{
        unsigned long ioport            : 1;
        unsigned long msgConf           : 1;
        unsigned long infMsf		: 1;
        unsigned long navConf           : 1;
        unsigned long rxmConf           : 1;
        unsigned long unUsed	        : 4;
        unsigned long rinvConf		: 1;
        unsigned long antConf           : 1;
        unsigned long unUsed1           : 21;
};

struct csl_deviceMask{
	unsigned char devBBR		:1;
	unsigned char devFlash		:1;
	unsigned char devEEPROM		:1;
	unsigned char unUsed		:1;
	unsigned char devSpiFlash	:1;
	unsigned char unUsed1		:3;
};

struct information {
        unsigned char protocolID ;
        unsigned char reserved0;
        unsigned short reserved1;/*define the mask if required at later point of time*/
        unsigned char infMsgMask[PORT_COUNT];
};

struct clear_load_save {//clear/save/load config
        struct csl_mask clearMask;
        struct csl_mask saveMask;
	struct csl_mask loadMask;
        struct csl_deviceMask deviceMask;
};

struct reset {
	unsigned short navBbrMask;/*define the mask if required at later point of time*/
	unsigned char resetMode;
	unsigned char reserved1;
};
/* Define all the max7 configuration here */
struct max7_cfg {
        struct navigation cfg_nav5;		 	/*UBX-CFG-NAV5*/
        struct message cfg_msg;				/*UBX-CFG-MSG*/
	struct information cfg_inf;			/*UBX-CFG-INF*/
	struct clear_load_save cfg_cfg;			/*UBX-CFG-CFG*/
	struct reset cfg_rst;				/*UBX-CFG-RST*/
};

/*ublox msg structure*/
struct ubx_msg {
        u8 sync1;
        u8 sync2;
        u8 msg_class;
        u8 msg_id;
        u16 payload_len;
        char *payload;
        u8 ck_a;
        u8 ck_b;
};

/*ublox ack-nak*/
struct ubx_ack_nak {
        u8 sync1;
        u8 sync2;
        u8 msg_class;
        u8 ack_nak;
        u16 payload_len;
        u8 clsID;
        u8 msgID;
        u8 ck_a;
        u8 ck_b;
};

struct max7_data {
        /* device lock */
        struct mutex            lock;
        struct i2c_client       *client;
        struct input_dev        *idev;
};

#if 0
struct max7_ioctl_t {
	u16 len;	//Length of buffer from user space
	void *buf;	//Buffer from user space
};
#endif
/*GPIO Related*/

struct gpio_desc 
{
	int gpio;
	int irq; // interrupt NO.
	int number; // NO. of relevant port pin
	char *name; // interrupt.s name
	char dir; // interrupt.s name
};

/*Function Prototype*/

static int max7_i2c_open(struct inode *, struct file *);
static int max7_i2c_release(struct inode *, struct file *);
static int max7_i2c_read(struct file *file, unsigned char __user *buf, size_t count);
static int max7_i2c_write(struct file *file, unsigned char __user *buf, size_t count);
//static int max7_i2c_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int max7_probe(struct i2c_client *client, const struct i2c_device_id *id);

static irqreturn_t gps_gpio_interrupt_handler(int irq, void *dev_id);
#endif

