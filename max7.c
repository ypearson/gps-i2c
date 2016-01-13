/*
 * Copyright (c) 2011 Bosch Sensortec GmbH
 * Copyright (c) 2011 Unixphere
 *
 * This driver adds support for Bosch Sensortec's digital acceleration
 * sensors BMA150 and SMB380.
 * The SMB380 is fully compatible with BMA150 and only differs in packaging.
 *
 * The datasheet for the BMA150 chip can be found here:
 * http://www.bosch-sensortec.com/content/language1/downloads/BST-BMA150-DS000-07.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <asm/gpio.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/rtc.h>
#include "max7.h"


/*GPIOs*/
#define GPIO_EXPANDER_BASE		50
#define SOC_SC_BASE			0x9A //154
#define GPS_ANT_OPEN			0x00//0
#define GPS_ANT_OFF			0x01//1
#define GPS_ANT_SHORT			0x04//4
#define GPS_ANT_EXTINT			0x05//5
#define GPS_ANT_ON			0x06//6
#define GPS_GPIO_SOC_MAX7_RST		0x37 //SC[055]
#define GPS_GPIO_SOC_MAX7_TP		0x32 //SC[050]

#define INPUT				0x01
#define OUTPUT				0x00
int get_stream_len = 0;
#if 0
static struct  gpio_desc gps_gpio_pins[] = {
	{(GPIO_EXPANDER_BASE + GPS_ANT_OPEN),0, 1, "Ant_Open", INPUT},
	{(GPIO_EXPANDER_BASE + GPS_ANT_OFF),0, 0, "Ant_Off", OUTPUT},
	{(GPIO_EXPANDER_BASE + GPS_ANT_SHORT),0, 2, "Ant_Short", INPUT},
	{(GPIO_EXPANDER_BASE + GPS_ANT_EXTINT),0, 0, "Ant_ExtInt", OUTPUT},
	{(GPIO_EXPANDER_BASE + GPS_ANT_ON),0, 3, "Ant_On", INPUT},
	{(SOC_SC_BASE + GPS_GPIO_SOC_MAX7_RST),0, 0, "Max7_Rst", OUTPUT},
	{(SOC_SC_BASE + GPS_GPIO_SOC_MAX7_TP),0, 4, "Max7_Rst", INPUT},
};
#endif
#define SOC_GPS_ANT_OFF_PIN   51

static struct  gpio_desc gps_gpio_pins[] = {
        {(GPIO_EXPANDER_BASE + GPS_ANT_OPEN),0, 1, "Ant_Open", INPUT},
        {(GPIO_EXPANDER_BASE + GPS_ANT_SHORT),0, 2, "Ant_Short", INPUT},
        {(GPIO_EXPANDER_BASE + GPS_ANT_ON),0, 3, "Ant_On", INPUT},
        {(SOC_SC_BASE + GPS_GPIO_SOC_MAX7_TP),0, 4, "Max7_Tp", INPUT},
        {(GPIO_EXPANDER_BASE + GPS_ANT_OFF),0, 0, "Ant_Off", OUTPUT},
        {(GPIO_EXPANDER_BASE + GPS_ANT_EXTINT),0, 0, "Ant_ExtInt", OUTPUT},
        {(SOC_SC_BASE + GPS_GPIO_SOC_MAX7_RST),0, 0, "Max7_Rst", OUTPUT},
};
/*Global Variable Declaration*/
static int max7_device_Open = 0;  /* Is device open?  Used to prevent multiple
                                       access to the device */
static int max7_read_msg_stream(struct i2c_client *client, void *kbuf, int streamlen);
static int max7_read_msg_stream_len(struct i2c_client *client);

struct max7_data *max7;
/*ublox receivers DDC address*/
#define UBX_DDC_ADDR			0x42
#define UBX_CFG_ACK			0x01
#define UBX_CFG_NAK			0x00

/*ublox msg sync character*/
#define UBX_MSG_SYNC_CHAR_1             0xB5
#define UBX_MSG_SYNC_CHAR_2             0x62

/*NMEA msg class*/
#define NMEA_CLASS_STD_MSG		0XF0

/*NMEA msg ID*/
#define NMEA_CLASS_ID_GGA		0X00
#define NMEA_CLASS_ID_GLL               0X01
#define NMEA_CLASS_ID_GSA		0X02
#define NMEA_CLASS_ID_GSV               0X03
#define	NMEA_CLASS_ID_RMC		0X04
#define	NMEA_CLASS_ID_VTG		0X05
#define NMEA_CLASS_ID_ZDA		0x08

/*ublox msg class*/
#define UBX_CLASS_NAV                   0x01
#define UBX_CLASS_RXM                   0x02
#define UBX_CLASS_INF                   0x04
#define UBX_CLASS_ACK                   0x05
#define UBX_CLASS_CFG                   0x06
#define UBX_CLASS_MON                   0x0A
#define UBX_CLASS_AID                   0x0B
#define UBX_CLASS_TIM                   0x0D
#define UBX_CLASS_LOG                   0x21

/*ublox msg ID*/
#define UBX_MSG_ID_CFG_MSG		0X01
#define UBX_MSG_ID_CFG_INF		0X02
#define UBX_MSG_ID_CFG_RST		0X04
#define UBX_MSG_ID_CFG_CFG		0X09
#define UBX_MSG_ID_CFG_NAV5		0X24

/*Register Definition*/
#define UBX_MSG_STREAM_LEN_HIGH         0xFD
#define UBX_MSG_STREAM_LEN_LOW          0xFE
#define UBX_MSG_STREAM_START            0xFF


/*IOCTL Command list to share with user space*/
#define READ_RESPONSE			0x01
#define GPS_SOFT_RESET			0X02
#define GPS_HARD_RESET                  0X03
#define READ_MSG_STREAM_LEN		0x04


static struct max7_cfg modify_cfg = {
        .cfg_nav5 = {
                .mask1 = {
                        .dyn            = 1,
                        .minEl          = 0,
			.posFixMode	= 0,    
			.drLim		= 0,         
			.posMask	= 0,       
			.timeMask	= 0,      
			.staticHoldMask	= 0,
			.dgpsMask	= 0,      
			.reserved	= 0,      
                },
                .dynModel  = 0,
         },
	.cfg_msg = {
/*Enable Only  GGA, GLL, ZDA, RMC, VTC */
		.cfg_req_res[0] = {
                        .msgClass       = NMEA_CLASS_STD_MSG,
                        .msgID          = NMEA_CLASS_ID_GSA,
                        .rate           = {0, 0, 0, 0, 0, 0},/* Disable GSA in All ports */
                },
		.cfg_req_res[1] = {
                        .msgClass       = NMEA_CLASS_STD_MSG,
                        .msgID          = NMEA_CLASS_ID_GSV,
                        .rate           = {0, 0, 0, 0, 0, 0},/* Disable GSV in All ports*/
                },
		.cfg_req_res[2] = {
                        .msgClass       = NMEA_CLASS_STD_MSG,
                        .msgID          = NMEA_CLASS_ID_ZDA,
                        .rate           = {1, 0, 0, 0, 0, 0},/* Enable ZDA in DDC port*/
                }
	},
	.cfg_inf = {
		.protocolID = 0,
		.infMsgMask = {15, 15, 15, 15, 15, 15}, // Enable Debug /Notice/ Warning / Error Messages in All ports		
	},
	.cfg_cfg = {
		.clearMask = {/*Load Default Configurations to Permanent Configurations in non-volatile memory*/
			.ioport		= 0, 
			.msgConf	= 0, 
			.infMsf		= 0,
			.navConf	= 0, 
			.rxmConf	= 0,
			.unUsed		= 0,
			.rinvConf	= 0,
			.antConf	= 0,
			.unUsed1	= 0,
		},
		.saveMask = {/*Save Mask will save the configurations to permanent cofiguration*/
                        .ioport         = 0,
                        .msgConf        = 1,
                        .infMsf         = 1,
                        .navConf        = 1,
                        .rxmConf        = 0,
                        .unUsed         = 0,
                        .rinvConf       = 0,
                        .antConf        = 0,
                        .unUsed1        = 0,
                },
		.loadMask = {/*Load Mask Will load the permanent configuration to current configuartion*/
                        .ioport         = 0,
                        .msgConf        = 1,
                        .infMsf         = 1,
                        .navConf        = 1,
                        .rxmConf        = 0,
                        .unUsed         = 0,
                        .rinvConf       = 0,
                        .antConf        = 0,
                        .unUsed1        = 0,
                },
		.deviceMask = {/* selects the devices for this command */
			.devBBR		= 1,
		}
	},
	.cfg_rst = {
                .navBbrMask             = 0,
                .resetMode              = 0, /*check the reset mode in datasheet*/
                .reserved1              = 0,
	}
};

static int calculate_checksum(char *msg, int msg_len)
{
        u16 i;
        char *msg_ptr = NULL;
	u8 a = 0;
	u8 b = 0;

	u16 len = (4 +((struct ubx_msg *)msg)->payload_len);// msg_class = 1 byt + msg_id = 1 byt + payload_len = 2 byt + payload

	//printk("Calculate checksum for the length = %d \n", len);
        msg_ptr = &((struct ubx_msg *)msg)->msg_class;
        for(i = 0; i < len; i++)
        {
                a = a + msg_ptr[i];
                b = b + a;
        }
	msg[msg_len - 2] = a;
	msg[msg_len - 1] = b;

	//printk("%s: ck_a = 0x%x ## ck_b = 0x%x \n",__func__, msg[msg_len - 2], msg[msg_len - 1]);
	//printk("%s: a = 0x%x ## b = 0x%x \n",__func__, a, b);
	return 0;
}
static char *frame_ubx_msg(u8 class, u8 id, u16 len, void *payload, int msg_len)
{
 char *msg;
 char *ptr = NULL;
 int i;

	msg = kzalloc(msg_len, GFP_KERNEL);
        if (!msg) {
                printk(KERN_ERR "%s: Out of memory whilst attempting "
                        "to kmalloc(%d, GFP_KERNEL)\n", __func__, msg_len);
                return 0;
        }
        memset(msg, 0, msg_len);
        ((struct ubx_msg *)msg)->sync1 = UBX_MSG_SYNC_CHAR_1;
        ((struct ubx_msg *)msg)->sync2 = UBX_MSG_SYNC_CHAR_2;
        ((struct ubx_msg *)msg)->msg_class = class;
        ((struct ubx_msg *)msg)->msg_id = id;
        ((struct ubx_msg *)msg)->payload_len = len;
        //memcpy(&((struct ubx_msg *)msg)->payload, (char *)payload, len);
        memcpy((msg + 6), (char *)payload, len); // sync1 = 1 byt + sync2 = 1 byt + msg_class = 1 byt + msg_id = 1 byt + payload_len = 2 byt
        calculate_checksum(msg, msg_len);

	ptr = (char *)msg;
	//printk("Print UBLOX Message With Payload length = %d \n", len);
#if 1
	for(i = 0 ;i < msg_len; i++)
        {
		printk("0x%x \n", ptr[i]);
	}
#endif	
	return msg;
}
#if 1
static int max7_write(struct i2c_client *client, void *msg, int len)
{
        int ret = 0;
	u8 cmd = UBX_MSG_STREAM_START;
	u8 ubx_buf[500];
	u8 i;
	u8 *pmsg = (u8 *)msg;

	if(len >= 500)
	{
		printk("%s: ubx msg to write > 500. return without write \n", __func__);
		return 0;
	}
//flush the stream before read
	ret = max7_read_msg_stream_len(client);
	if(ret > 500)	
	{
		max7_read_msg_stream(client, ubx_buf, 500);	
	}
	else if(ret > 0)
	{
		max7_read_msg_stream(client, ubx_buf, ret);
	}

//flush the stream before read
	memset(ubx_buf, '\0', sizeof(ubx_buf));

	ubx_buf[0] = cmd;
//	printk("ubx_buf[0] = 0x%x \n",ubx_buf[0]);
	for(i = 0; i < len; i++)
	{
		ubx_buf[i+1] = *pmsg++;
//		printk("ubx_buf[%d] = 0x%x \n", (i+1), ubx_buf[i+1]);
	}

	ret = i2c_master_send(client, ubx_buf, (len + 1));
	printk(KERN_ERR "%s: len to write = %d ## Length actually written = %d \n", __func__, (len + 1), ret);
        if (ret != (len + 1)) {
                dev_err(&client->dev, "Couldn't send I2C command.\n");
		return 0;
        }

        return len;
}
#endif

static int max7_transfer(struct i2c_client *client, u8 cmd, void *buf, u16 len)
{
	struct i2c_msg msgs[] = {
				{
					.addr = client->addr,
					.flags = 0,
					.len = 1,
					.buf = &cmd,
				}, {
					.addr = client->addr,
					.flags = I2C_M_RD,
					.len = len,
					.buf = buf,
				}
			};
	
			//printk(KERN_ERR "cmd = 0x%x ## client->addr = 0x%x ## i2c_msg[0].addr = 0x%x \n", cmd, client->addr, msgs[0].addr);
	
			if (i2c_transfer(client->adapter, msgs, 2) == 2)
			{
				return 0;
			}
			
	
			return -EIO;
}

static int max7_read_msg_stream(struct i2c_client *client, void *kbuf, int streamlen)
{
	u8 cmd = UBX_MSG_STREAM_START;	
	
	if(max7_transfer(client, cmd, kbuf, streamlen) != 0)
	{
		printk(KERN_ERR "%s: max7_transfer error\n", __func__);
		dev_err(&client->dev,"%s: max7_transfer error\n", __func__);
		return -1;
	}
	return 0;
}


static int max7_read_msg_stream_len(struct i2c_client *client)
{
	
	u8 cmd = UBX_MSG_STREAM_LEN_HIGH;
	u8 buf[2];
	int streamlen = 0, retry = 0;
	struct timeval time;


	while((streamlen == 0) && (retry < 5))
	{
		if(max7_transfer(client, cmd, buf, 2) != 0)
		{
			printk(KERN_ERR "%s: max7_transfer error for retry %d. streamlen = %d\n", __func__,  retry, streamlen);
			return -1;
		}
		streamlen = ((buf[0] << 8) | buf[1]);
		//do_gettimeofday(&time);
		//printk("Sec = %ld ## uSec = %ld \n", time.tv_sec, time.tv_usec);
		retry++;
		//udelay(170);
		mdelay(200);
	}
	printk("%s: retry %d streamlen = %d \n", __func__, retry, streamlen);	
	if(streamlen == 0)
	{
		printk("%s: Invalid Stream length after retries\n",__func__);
		return -1;
	}
	else
		return streamlen;
}

static int send_ublox_request(struct i2c_client *client, u8 msg_class, u16 msg_ID, void *config_data, u16 len)
{
 char *msg = NULL;
 int msg_len = (8 + len);// sync1 = 1 byt + sync2 = 1 byt + msg_class = 1 byt + msg_id = 1 byt + payload_len = 2 byt + ck_a = 1 byt + ck_b = 1 byt + payload(len)

	msg = frame_ubx_msg(msg_class, msg_ID, len, config_data, msg_len);
	if(!max7_write(client, msg, msg_len))
	{
		dev_err(&client->dev, "failed to send ublox request\n");
		kfree(msg);
		return 0;
	}
//	printk("send_ublox_request success....\n");
	kfree(msg);
	return 1;
}

static int process_ublox_ack(struct i2c_client *client, u8 msg_class, u16 msg_ID)
{
	struct ubx_ack_nak ubx_res;
	int streamlen = 0;
	char *ptr =(char *) &ubx_res;
	int i;
        
	printk("Reading Ack_Nak streamlen..\n");
	streamlen = max7_read_msg_stream_len(client);
        printk("streamlen = %d ## sizeof(ubx_res) = %d\n", streamlen, sizeof(ubx_res));
	if(streamlen <= 0)
	{		
		return -1;
	}
	if(streamlen > sizeof(ubx_res))
		streamlen = sizeof(ubx_res);
	printk("Reading Ack....\n");	
	if(max7_read_msg_stream(client, &ubx_res, streamlen) < 0)
	{
		printk("%s: max7_read_msg_stream error\n", __func__);
		return -1;
	}

for(i = 0; i< streamlen; i++)
{
printk("res[%d] = 0x%x\n", i, *ptr++);	
}
	if((ubx_res.msg_class == UBX_CLASS_ACK) &&
		(ubx_res.clsID == msg_class) &&
		(ubx_res.msgID == msg_ID))
		{
			if(ubx_res.ack_nak == UBX_CFG_ACK)
			{
				printk("Received ACK for Message Class = 0x%x ##  Message Class ID  = 0x%x ##  Message ID  = 0x%x\n", ubx_res.msg_class, ubx_res.clsID, ubx_res.msgID);
				return UBX_CFG_ACK;
			}
			else if(ubx_res.ack_nak == UBX_CFG_NAK)
			{
				printk("Received NACK for Message Class = 0x%x ##  Message Class ID  = 0x%x ##  Message ID  = 0x%x\n", ubx_res.msg_class, ubx_res.clsID, ubx_res.msgID);
				return UBX_CFG_NAK;
			}
			else
				return -1;
		}
		else
		{
			printk("Invalid Response from ublox\n");
			return -1;	
		}
}

static int max7_i2c_open(struct inode *inode, struct file *file)
{
	if(max7_device_Open)
	{
		printk(KERN_ERR "%s:Device Busy \n", __func__);
		return -EBUSY;
	}
	else
	{
#if 0
	printk("Hard reset Max7...\n");
        if ( gpio_request((SOC_SC_BASE + GPS_GPIO_SOC_MAX7_RST), "RST") < 0 )
	{
        printk("GPS_GPIO_SOC_MAX7_RST GPIO request failed \n");
	}

        if ( gpio_direction_output((SOC_SC_BASE + GPS_GPIO_SOC_MAX7_RST), 0) < 0 )
	{
          printk("GPS_GPIO_SOC_MAX7_RST GPIO dir set failed \n");
	}
	udelay(10);
	if ( gpio_direction_output((SOC_SC_BASE + GPS_GPIO_SOC_MAX7_RST), 1) < 0 )
	{
          printk("GPS_GPIO_SOC_MAX7_RST GPIO dir set failed \n");
	}
	printk("End of Max7 Hard reset ...\n");
#endif
	//printk("Sending Soft Reset....\n");
	/*Send ublox configuration request to Reset the Receiver*/
        //send_ublox_request(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_RST, &modify_cfg.cfg_rst, sizeof(modify_cfg.cfg_rst))
		max7_device_Open++;
	}
	//mdelay(100);
	printk(KERN_ERR "%s: file open success \n", __func__); 
	return 0;
}

static int max7_i2c_release(struct inode *inode, struct file *file)
{
        if(max7_device_Open)
	{
		printk(KERN_ERR "%s: file release success \n", __func__);
		max7_device_Open--;                
	}

        return 0;
}

static int max7_i2c_write(struct file *file, unsigned char __user *buf, size_t count)
{
        struct i2c_client *client = max7->client;
        unsigned char *kbuf = NULL;
	int i;
	unsigned char read_buf[500];
	size_t len = 0;	

	//return count;
	printk(KERN_ERR "%s Write buf count from user space = %zd \n", __func__, count);
        kbuf = kzalloc(count, GFP_KERNEL);
        if (!kbuf) {
                printk(KERN_ERR "%s: Out of memory whilst attempting "
                        "to kmalloc(%zd, GFP_KERNEL)\n", __func__, count);
                return -ENOMEM;
        }

	memset(kbuf, 0, count);
	if(copy_from_user(kbuf, (void __user *)buf, count)){
                dev_err(&client->dev, "can't copy %zd bytes to kbuf\n", count);
                kfree(kbuf);
                return -EFAULT;
        }

        printk(KERN_ERR "%s: Write Message From User Space = ",__func__);
	for(i = 0; i < count; i++)
        {
                printk(KERN_ERR "0x%x ", kbuf[i]);
        }
	
	if(kbuf[0] != 0xb5)
	{
		printk("Invalid U-blox Write(0x%x)\n", kbuf[0]);
		kfree(kbuf);
		return 0;
	}
#if 0
//	max7_i2c_read(file, read_buf, len);
        if(max7_write(client, kbuf, count) != count){
		printk(KERN_ERR "%s: Device Write Error ",__func__);
                kfree(kbuf);
                return 0;
        }
#endif
        kfree(kbuf);
        return count;
}

static int max7_i2c_read(struct file *file, unsigned char __user *buf, size_t count)
{
	struct i2c_client *client = max7->client;
	unsigned char *kbuf = NULL;
	int streamlen = 0, i;
/*To Get TimeStamp*/
struct timeval time;
unsigned long local_time;
struct rtc_time tm;

do_gettimeofday(&time);
printk("Sec = %ld ## uSec = %ld \n", time.tv_sec, time.tv_usec);
//local_time = (u32)(time.tv_sec - (sys_tz.tz_minuteswest * 60));
//rtc_time_to_tm(local_time, &tm);

//printk(" @@@@@@@@@@@ (%04d-%02d-%02d %02d:%02d:%02d)\n", tm.tm_year + 1900, tm.tm_mon + 1,tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
/*To Get TimeStamp*/

	
	//mdelay(1000);
	printk("%s: length from user space  = %zd \n", __func__, count);
	streamlen = max7_read_msg_stream_len(client);
//	printk("%s: stream length in max7= %zd \n", __func__, streamlen);
        if(streamlen <= 0)
        {
		printk("%s:STREAM_LEN <= 0\n",__func__);
                return 0;
        }
#if 0	
	if(streamlen > count)
	{
		printk("Reading only %d bytes out of %d bytes\n", count, streamlen);
		streamlen = count;
	}
#endif
	kbuf = kzalloc(streamlen, GFP_KERNEL);
	if (!kbuf) {
		printk(KERN_ERR "%s: Out of memory whilst attempting "
			"to kmalloc(%d, GFP_KERNEL)\n", __func__, streamlen);
                return 0;
	}
	
	memset(kbuf, 0, streamlen);

	if(max7_read_msg_stream(client, kbuf, streamlen) < 0)
	{
		printk("%s: max7_read_msg_stream error\n", __func__);
		//return -1;
                return 0;
	}
#if 1	
	if(count < streamlen)
		streamlen = count;
#endif
#if 1
	for(i = 0; i < 25; i++)
	{
		printk("%s: Message = %c(0x%x)  ",__func__, kbuf[i], kbuf[i]);
	}
#endif
	printk("\nUblox Responded : %s\n", kbuf);

	if(copy_to_user((void __user *)buf, kbuf, streamlen)){
                dev_err(&client->dev, "can't copy %d bytes to buf\n", streamlen);
		kfree(kbuf);
                //return -EFAULT;
                return 0;
        }
	kfree(kbuf);
        return streamlen;
}

static const struct file_operations max7_fops = {
        .owner		= THIS_MODULE,
	.open		= max7_i2c_open,
	.release	= max7_i2c_release,
	.read		= max7_i2c_read,
	.write		= max7_i2c_write,
//	.unlocked_ioctl	= max7_i2c_ioctl,
};

static irqreturn_t gps_gpio_interrupt_handler(int irq, void *dev_id)
{
	struct gpio_desc *gps_gpio_irqs = (struct gpio_desc *)dev_id;
	static int time_pulse_count = 0;
//	int reportVal;
//	reportVal = button_irqs->number;
	
	switch(gps_gpio_irqs->number)
	{
		case 1: // Antenna Open`
			printk("%s: GPS Ant Open detected \n",__func__);
			gpio_direction_output((GPIO_EXPANDER_BASE + GPS_ANT_OFF), 1);
		break;
		case 2: // Antenna Short
			printk("%s: GPS Ant Short detected \n",__func__);
			gpio_direction_output((GPIO_EXPANDER_BASE + GPS_ANT_OFF), 1);
		break;
		case 3://Antenna ON
			printk("%s: GPS Ant ON detected \n",__func__);
		break;
		case 4://Time Pulse
			time_pulse_count++; //Expected 1 pulse per second
			printk("%s: GPS Time Pulse detected %d \n",__func__, time_pulse_count);
		break;
		default:
		break;
	}
	return IRQ_HANDLED;
}

static struct miscdevice max7_gps_miscdevice = {
        .minor  = MISC_DYNAMIC_MINOR,
        .name   = "Max_7",
        .fops   = &max7_fops
};

static int send_ubx_cfg_msg_poll(struct i2c_client *client)
{
 struct poll_msg_cfg poll_req;
 int streamlen = 0;
 char res_buf[500];
 char *ptr = res_buf;
 int i;

printk("Sending UBX_MSG_ID_CFG_MSG POLL Request \n");
	
	memset(&poll_req, 0, sizeof(poll_req));
        
	/*Send ublox configuration request to poll the message Configuration. 
        This is required for testing purpose only. This can be removed @ later point of time*/

        poll_req.msgClass =  NMEA_CLASS_STD_MSG;
        poll_req.msgID  =  NMEA_CLASS_ID_GGA;
        if(send_ublox_request(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_MSG, &poll_req, sizeof(poll_req))){
                /*Read the Ublox Response message and print it*/
                streamlen = max7_read_msg_stream_len(client);
                printk("streamlen of Poll_req response = %d \n", streamlen);
                if(streamlen <= 0)
                {
                        printk("No Response for UBX_MSG_ID_CFG_MSG...\n");
                        return -1;
                }
                if(streamlen > sizeof(res_buf))
                        streamlen = sizeof(res_buf);
                printk("Reading Response for UBX_MSG_ID_CFG_MSG....\n");
                if(max7_read_msg_stream(client, &res_buf, streamlen) < 0)
                {
                        printk("%s: max7_read_msg_stream error while Reading Response for UBX_MSG_ID_CFG_MSG \n", __func__);
                        return -1;
                }

                for(i = 0; i< streamlen; i++)
                {
                        printk("res[%d] = 0x%x\n", i, *ptr++);
                }
        }
	return 0;
}

static int send_ubx_cfg_inf(struct i2c_client *client)
{
 u8 ret;
	
	printk("Sending UBX_MSG_ID_CFG_INF \n");
        
	/*Send ublox configuration request to UBX_CFG_INF */
        if(send_ublox_request(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_INF, &modify_cfg.cfg_inf, sizeof(modify_cfg.cfg_inf))){
                /*Process the Ublox ACK / NAck message and print it*/
                ret = process_ublox_ack(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_INF);
                if(ret == UBX_CFG_NAK){
                        printk("Ublox INF Configuration (UBX-CFG-INF) Not Acknowledged \n");
                }
                else if(ret == -1){
                        printk("Ublox INF Configuration (UBX-CFG-INF) Failed \n");
                }
        }
	return 0;
}
static int max7_initialise(struct i2c_client *client)
{
 u8 ret;

	printk("Sending UBX_MSG_ID_CFG_NAV5 \n");
        /*Send ublox configuration request to navigation group*/
        if(send_ublox_request(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_NAV5, &modify_cfg.cfg_nav5, sizeof(modify_cfg.cfg_nav5))){
		udelay(100);
	        /*Process the Ublox ACK / NAck message and print it*/
		ret = process_ublox_ack(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_NAV5);
        	if(ret == UBX_CFG_NAK){
               		printk("Ublox Navigation Setting (UBX-CFG-NAV5) Not Acknowledged \n"); 
        	}
        	else if(ret == -1){
               		printk("Ublox Navigation Setting (UBX-CFG-NAV5) Failed \n"); 
        	}
	}

	send_ubx_cfg_msg_poll(client);//Added for testing. can be removed.
	send_ubx_cfg_inf(client);//Added for testing. can be removed.
#if 0//Enable this for Message configuration
	/*Send ublox message configuration request*/
	for(i=0; i < NUM_OF_MSGS; i++)
	{
		if(send_ublox_request(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_MSG, &modify_cfg.cfg_msg.cfg_req_res[i], sizeof(modify_cfg.cfg_msg.cfg_req_res[i]))){
        		/*Process the Ublox ACK / NAck message and print it*/
        		ret = process_ublox_ack(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_MSG);
        		if(ret == UBX_CFG_NAK){
                		printk("Ublox Message Configuration Setting (UBX-CFG-MSG) Not Acknowledged \n");
        		}
	        	else if(ret == -1){
                		printk("Ublox Message Configuration Setting (UBX-CFG-MSG) Failed \n");
        		}
		}
	}

	
#endif
#if 0
printk("Sending UBX_MSG_ID_CFG_CFG \n");
printk("sizeof(modify_cfg.cfg_cfg) = %d ## clearMask = %d ## deviceMask = %d\n", sizeof(modify_cfg.cfg_cfg), sizeof(modify_cfg.cfg_cfg.clearMask), sizeof(modify_cfg.cfg_cfg.deviceMask));
        /*Send ublox configuration request to clear/save/load configuration changes*/
        if(send_ublox_request(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_CFG, &modify_cfg.cfg_cfg, 13)){
		udelay(100);
        	/*Process the Ublox ACK / NAck message and print it*/
		ret = process_ublox_ack(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_CFG);
        	if(ret == UBX_CFG_NAK){
                	printk("Ublox CSL Configuration (UBX-CFG-CFG) Not Acknowledged \n");
        	}
        	else if(ret == -1){
                	printk("Ublox CSL Configuration (UBX-CFG-CFG) Failed \n");
        	}
	}
#endif
	/*Save & Load is sufficient to persist & work with new configuration changes. So No reset required */
	//printk("Sending Soft Reset....\n");
	/*Send ublox configuration request to Reset the Receiver*/
        //send_ublox_request(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_RST, &modify_cfg.cfg_rst, sizeof(modify_cfg.cfg_rst))
#if 0
	{
		udelay(100);
        	/*Process the Ublox ACK / NAck message and print it*/
        	ret = process_ublox_ack(client, UBX_CLASS_CFG, UBX_MSG_ID_CFG_RST);
        	if(ret == UBX_CFG_NAK){
                	printk("Ublox  Reset (UBX_MSG_ID_CFG_RST) Not Acknowledged \n");
        	}
        	else if(ret == -1){
                	 printk("Ublox Reset(UBX_MSG_ID_CFG_RST) Failed \n");
        	}
	}
#endif
	printk("End of max7_initialise\n");	
	return 0;
}
static int max7_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int error;
	dev_err(&client->dev, "Inside max7 probe slave addr = 0x%x\n", client->addr);
	printk(KERN_ERR "Inside max7 probe slave addr = 0x%x\n", client->addr);
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c_check_functionality error\n");
                return -EIO;
	}

	max7 = kzalloc(sizeof(struct max7_data), GFP_KERNEL);
        if (!max7)
                return -ENOMEM;

        max7->client = client;
	mutex_init(&max7->lock);

	max7_initialise(client);

	i2c_set_clientdata(client, max7);
	printk("Calling misc_register()....\n");
        error = misc_register(&max7_gps_miscdevice);
        if (error) {
                dev_err(&client->dev, "Miscdevice register failed\n");
                return -1;
        }
#if 1
	if ( gpio_request(SOC_GPS_ANT_OFF_PIN, "ANT") < 0 )
        printk("SOC_GPS_ANT_OFF_PIN GPIO request failed \n");

         if ( gpio_direction_output(SOC_GPS_ANT_OFF_PIN,0) < 0 )
          printk("SOC_GPS_ANT_OFF_PIN GPIO dir set failed \n");
#endif
#if 0
	/*GPIO Initialisation*/
	for (i = 0; i < sizeof(gps_gpio_pins)/sizeof(gps_gpio_pins[0]); i++)
	{
		printk("checking gpio %d \n", gps_gpio_pins[i].gpio);
		if (!gpio_is_valid(gps_gpio_pins[i].gpio))
		{
			printk(KERN_ERR "%s :GPIO is invalid\n",__func__);
                        return -1;
		}
		printk("Requesting gpio %d   -   %s \n", gps_gpio_pins[i].gpio, gps_gpio_pins[i].name);
		error = gpio_request(gps_gpio_pins[i].gpio, gps_gpio_pins[i].name);
		if (error < 0){
                	printk(KERN_ERR "%s :GPIO Request is invalid\n",__func__); 
			return error;
		}

		if(gps_gpio_pins[i].dir == INPUT){
			error = gpio_direction_input(gps_gpio_pins[i].gpio);
         		if (error < 0){
                 		printk(KERN_ERR "%s :GPIO Direction Input failed \n",__func__);
				return error;
			}
 			printk("gpio_direction_input of gpio %d success \n", gps_gpio_pins[i].gpio);
			gps_gpio_pins[i].irq = gpio_to_irq(gps_gpio_pins[i].gpio);
         		if (gps_gpio_pins[i].irq < 0){
				 printk(KERN_ERR "%s :GPIO To IRQ failed \n",__func__);
                                return gps_gpio_pins[i].irq;
                        }	
			
 			printk("gpio_to_irq of gpio %d is %d \n", gps_gpio_pins[i].gpio, gps_gpio_pins[i].irq);
			error = request_irq(gps_gpio_pins[i].irq, gps_gpio_interrupt_handler, IRQ_TYPE_EDGE_BOTH,
				gps_gpio_pins[i].name, (void *)&gps_gpio_pins[i]);
			if (error){
				printk(KERN_ERR "Could not request IRQ_INT!\n");
				//return error;
			}
			printk("request_irq of gpio %d is success \n", gps_gpio_pins[i].gpio);
		}
		else{
			if(gps_gpio_pins[i].gpio == (GPIO_EXPANDER_BASE + GPS_ANT_OFF)){ 
				gpio_direction_output(gps_gpio_pins[i].gpio, 0); //default state 0 & trigger state is 1
				{
					printk("Out 0 in GPS_ANT_OFF \n");
				}
			}
			else{
				gpio_direction_output(gps_gpio_pins[i].gpio, 1); //default state 1 & trigger state is 0
			}
		}
	}
#endif
	return error;
}

void gpio_cleanup()
{
int i;
        printk("Inside %s \n", __func__);
	
	for (i = 0; i < sizeof(gps_gpio_pins)/sizeof(gps_gpio_pins[0]); i++)
        {
		if(gps_gpio_pins[i].dir == INPUT)
        	{
			printk("%s: free_irq = %d \n", __func__, gps_gpio_pins[i].irq);
                	free_irq(gps_gpio_pins[i].irq, NULL);
        	}
                printk("%s: gpio_free for GPIO = %d\n", __func__, gps_gpio_pins[i].gpio);
                gpio_free(gps_gpio_pins[i].gpio);
	}

}

static int max7_remove(struct i2c_client *client)
{
	misc_deregister(&max7_gps_miscdevice);
        kfree(max7);
//	gpio_cleanup();
	printk(KERN_ERR "max7_remove success\n");
	return 0;
}

static const struct i2c_device_id max7_id[] = {
        { "max7", 0 },
        { }
};

MODULE_DEVICE_TABLE(i2c, max7_id);

static struct i2c_driver max7_driver = {
        .driver = {
                .owner  = THIS_MODULE,
                .name   = "max7",
        },
        .id_table       = max7_id,
        .probe          = max7_probe,
        .remove         = max7_remove,
};

module_i2c_driver(max7_driver);

MODULE_AUTHOR("HCL Technologies");
MODULE_DESCRIPTION("Max7 Driver");
MODULE_LICENSE("GPL");
