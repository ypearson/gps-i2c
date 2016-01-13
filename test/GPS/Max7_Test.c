#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */

#define PORT_COUNT		6
#define DEVICE_READ						0x01
#define DEVICE_WRITE					0x02
#define DEVICE_IOCTL					0x03

/*IOCTL Command list to share with user space*/
#define READ_RESPONSE					0x01
#define GPS_SOFT_RESET					0X02
#define GPS_HARD_RESET                  0X03
#define READ_MSG_STREAM_LEN				0x04

/*ublox msg class*/
#define UBX_CLASS_CFG                   0x06
/*ublox msg ID*/
#define UBX_MSG_ID_CFG_MSG				0X01

/*NMEA msg class*/
#define NMEA_CLASS_STD_MSG		0XF0

/*NMEA msg ID*/
#define NMEA_CLASS_ID_GGA		0X00
#define NMEA_CLASS_ID_GLL       0X01
#define NMEA_CLASS_ID_GSA		0X02
#define NMEA_CLASS_ID_GSV       0X03
#define	NMEA_CLASS_ID_RMC		0X04
#define	NMEA_CLASS_ID_VTG		0X05
#define	NMEA_STD_GRS 			0X06
#define	NMEA_STD_GST			0X07		
#define NMEA_CLASS_ID_ZDA		0x08

/*ublox msg sync character*/
#define UBX_MSG_SYNC_CHAR_1             0xB5
#define UBX_MSG_SYNC_CHAR_2             0x62


struct max7_ioctl_t {
	unsigned int len;	//Length of buffer from user space
	void *buf;	//Buffer from user space
};

/*ublox 7 set Message configuration rate for multiple ports*/
struct set_msg_config {
        unsigned char msgClass;
        unsigned char msgID;
        unsigned char rate[PORT_COUNT];
};

/*ublox msg structure*/
struct ubx_msg {
        unsigned char sync1;
        unsigned char sync2;
        unsigned char msg_class;
        unsigned char msg_id;
        unsigned int payload_len;
        void *payload;
        unsigned int ck_a;
        unsigned int ck_b;
};

static int calculate_checksum(const struct ubx_msg *msg,  unsigned int ck_a,  unsigned int ck_b)
{
        unsigned int i;
        const unsigned char *msg_ptr = NULL;

        unsigned int len = (sizeof(msg->msg_class)+
                   sizeof(msg->msg_id)+
                   sizeof(msg->payload_len)+
                   msg->payload_len);

        msg_ptr = &msg->msg_class;
        for(i = 0; i < len; i++)
        {
                ck_a = ck_a + msg_ptr[i];
                ck_b = ck_b + ck_a;
        }
	return 0;
}

static int frame_ubx_msg(struct ubx_msg *msg, unsigned char class, unsigned char id, unsigned int len, void *payload)
{
        unsigned int ck_sum_a = 0;
        unsigned int ck_sum_b = 0;

        memset(msg, 0, sizeof(msg));
        msg->sync1 = UBX_MSG_SYNC_CHAR_1;
        msg->sync2 = UBX_MSG_SYNC_CHAR_2;
        msg->msg_class = class;
        msg->msg_id = id;
        msg->payload_len = len;
        msg->payload = payload;
        calculate_checksum(msg, ck_sum_a, ck_sum_b);
        msg->ck_a = ck_sum_a;
        msg->ck_b = ck_sum_b;
	return 0;
}


static int send_ublox_request(int file_desc, unsigned char msg_class, unsigned int msg_ID, void *config_data, unsigned int len)
{
	struct ubx_msg ubx_msg1;

	frame_ubx_msg(&ubx_msg1, msg_class, msg_ID, len, config_data);
	if(write(file_desc, &ubx_msg1, sizeof(ubx_msg1)))
	{
		printf("failed to send ublox request\n");
		return 0;
	}
	return 1;
}


main()
{
  int file_desc = 0, ret_val, dev_op;
  unsigned char nmea_msg, msg_status;
  char msg[100] = "\0";
  char file[100];
  struct max7_ioctl_t get_data;
  struct set_msg_config set_msg;

  memset(file,'\0', sizeof(file));
  printf("Enter the Device file name:\n");
  fgets(file, sizeof(file), stdin);
  printf("Open the file %s\n", file);

  file_desc = open(file, 0);
  if (file_desc < 0) {
    printf ("Can't open device file: %s\n", file);
    exit(-1);
  }

	while(1){
	memset(msg,'\0', sizeof(msg));
	printf("Enter the device operation: \n");
	printf("1. Read \n ");
	printf("2. Write \n");
	printf("3. Quit \n");
	scanf("%d", &dev_op);
	printf("dev_op = %d \n", dev_op);
	if(dev_op == DEVICE_READ)
	{
		ret_val = read(file_desc, msg, sizeof(msg));
		printf("NMEA Read = %s \n", msg);
	}
	else if(dev_op == DEVICE_WRITE)
	{
		printf("Enter the Message to Enable / Disable:\n");
		printf("0.NMEA_STD_GGA \n");
		printf("1.NMEA_STD_GLL \n");
		printf("2.NMEA_STD_GSA \n");
		printf("3.NMEA_STD_GSV \n");
		printf("4.NMEA_STD_RMC \n");
		printf("5.NMEA_STD_VTG \n");
		printf("6.NMEA_STD_GRS \n");
		printf("7.NMEA_STD_GST \n");
		printf("8.NMEA_STD_ZDA \n");
		scanf("%d",&nmea_msg);
		printf("Do You want to Enable / Disable the NMEA Message \n");
		printf("1. Enable \n");
		printf("2. Disable \n");
		scanf("%d", &msg_status);
		set_msg.msgClass = NMEA_CLASS_STD_MSG;
		set_msg.msgID = nmea_msg;
		set_msg.rate[0] = msg_status;
		ret_val = send_ublox_request(file_desc, UBX_CLASS_CFG, UBX_MSG_ID_CFG_MSG, &set_msg, sizeof(set_msg));
	}	
	else if(dev_op == 3)
	{
		exit(0);
	}
	}
  close(file_desc); 
}

