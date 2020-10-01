/*
 * vedio_interface.h
 *
 *  Created on: Aug 27, 2020
 *      Author: ning
 */
#ifndef SERVER_VIDEO_VIDEO_INTERFACE_H_
#define SERVER_VIDEO_VIDEO_INTERFACE_H_

/*
 * header
 */
#include "../../manager/global_interface.h"
#include "../../manager/manager_interface.h"

/*
 * define
 */
#define		MSG_VIDEO_BASE						(SERVER_VIDEO<<16)
#define		MSG_VIDEO_SIGINT					MSG_VIDEO_BASE | 0x0000
#define		MSG_VIDEO_SIGINT_ACK				MSG_VIDEO_BASE | 0x1000
#define		MSG_VIDEO_BUFFER_MISS				MSG_VIDEO_BASE | 0x0001
#define		MSG_VIDEO_BUFFER_MISS_ACK			MSG_VIDEO_BASE | 0x1001
//video control message
#define		MSG_VIDEO_START						MSG_VIDEO_BASE | 0x0010
#define		MSG_VIDEO_START_ACK					MSG_VIDEO_BASE | 0x1010
#define		MSG_VIDEO_STOP						MSG_VIDEO_BASE | 0x0011
#define		MSG_VIDEO_STOP_ACK					MSG_VIDEO_BASE | 0x1011
#define		MSG_VIDEO_CTRL						MSG_VIDEO_BASE | 0x0020
#define		MSG_VIDEO_CTRL_ACK					MSG_VIDEO_BASE | 0x1020
#define		MSG_VIDEO_CTRL_EXT					MSG_VIDEO_BASE | 0x0021
#define		MSG_VIDEO_CTRL_EXT_ACK				MSG_VIDEO_BASE | 0x1021
#define		MSG_VIDEO_CTRL_DIRECT				MSG_VIDEO_BASE | 0x0022
#define		MSG_VIDEO_CTRL_DIRECT_ACK			MSG_VIDEO_BASE | 0x1022
#define		MSG_VIDEO_GET_PARA					MSG_VIDEO_BASE | 0x0023
#define		MSG_VIDEO_GET_PARA_ACK				MSG_VIDEO_BASE | 0x1023
//video control command from miio
//standard camera
#define		VIDEO_CTRL_SWITCH						0x0000
#define 	VIDEO_CTRL_IMAGE_ROLLOVER				0x0001
#define 	VIDEO_CTRL_NIGHT_SHOT               	0x0002
#define 	VIDEO_CTRL_TIME_WATERMARK           	0x0003
#define 	VIDEO_CTRL_WDR_MODE                 	0x0004
#define 	VIDEO_CTRL_GLIMMER_FULL_COLOR       	0x0005
#define 	VIDEO_CTRL_RECORDING_MODE           	0x0006
#define 	VIDEO_CTRL_MOTION_TRACKING          	0x0007
//standard motion detection
#define 	VIDEO_CTRL_MOTION_SWITCH          		0x0010
#define 	VIDEO_CTRL_MOTION_ALARM_INTERVAL    	0x0011
#define 	VIDEO_CTRL_MOTION_SENSITIVITY 			0x0012
#define 	VIDEO_CTRL_MOTION_START		          	0x0013
#define 	VIDEO_CTRL_MOTION_END		          	0x0014
//qcy custom
#define 	VIDEO_CTRL_CUSTOM_DISTORTION          	0x0100
//video control command others
#define		VIDEO_CTRL_QUALITY						0x10000


/*
 * structure
 */
typedef struct osd_text_info_t {
    char *text;
	int cnt;
	uint32_t x;
	uint32_t y;
	uint8_t *pdata;
	uint32_t len;
} osd_text_info_t;

/*
 * function
 */
int server_video_start(void);
int server_video_message(message_t *msg);

#endif
