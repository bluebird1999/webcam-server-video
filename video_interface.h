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
#define		SERVER_VIDEO_VERSION_STRING		"alpha-3.1"

#define		MSG_VIDEO_BASE						(SERVER_VIDEO<<16)
#define		MSG_VIDEO_SIGINT					MSG_VIDEO_BASE | 0x0000
#define		MSG_VIDEO_SIGINT_ACK				MSG_VIDEO_BASE | 0x1000
//video control message
#define		MSG_VIDEO_START						MSG_VIDEO_BASE | 0x0010
#define		MSG_VIDEO_START_ACK					MSG_VIDEO_BASE | 0x1010
#define		MSG_VIDEO_STOP						MSG_VIDEO_BASE | 0x0011
#define		MSG_VIDEO_STOP_ACK					MSG_VIDEO_BASE | 0x1011
#define		MSG_VIDEO_CTRL						MSG_VIDEO_BASE | 0x0012
#define		MSG_VIDEO_CTRL_ACK					MSG_VIDEO_BASE | 0x1012
#define		MSG_VIDEO_CTRL_EXT					MSG_VIDEO_BASE | 0x0013
#define		MSG_VIDEO_CTRL_EXT_ACK				MSG_VIDEO_BASE | 0x1013
#define		MSG_VIDEO_CTRL_DIRECT				MSG_VIDEO_BASE | 0x0014
#define		MSG_VIDEO_CTRL_DIRECT_ACK			MSG_VIDEO_BASE | 0x1014
#define		MSG_VIDEO_GET_PARA					MSG_VIDEO_BASE | 0x0015
#define		MSG_VIDEO_GET_PARA_ACK				MSG_VIDEO_BASE | 0x1015
//video control command from miio
//standard camera
#define		VIDEO_CTRL_SWITCH						0x0000
#define 	VIDEO_CTRL_IMAGE_ROLLOVER				0x0001
#define 	VIDEO_CTRL_NIGHT_SHOT               	0x0002
#define 	VIDEO_CTRL_TIME_WATERMARK           	0x0003
#define 	VIDEO_CTRL_WDR_MODE                 	0x0004
#define 	VIDEO_CTRL_GLIMMER_FULL_COLOR       	0x0005
#define 	VIDEO_CTRL_RECORDING_MODE           	0x0006
//standard motion detection
#define 	VIDEO_CTRL_MOTION_SWITCH          		0x0010
#define 	VIDEO_CTRL_MOTION_ALARM_INTERVAL    	0x0011
#define 	VIDEO_CTRL_MOTION_SENSITIVITY 			0x0012
#define 	VIDEO_CTRL_MOTION_START		          	0x0013
#define 	VIDEO_CTRL_MOTION_END		          	0x0014
//qcy custom
#define 	VIDEO_CTRL_CUSTOM_LOCAL_SAVE          	0x0100
#define 	VIDEO_CTRL_CUSTOM_CLOUD_SAVE          	0x0101
#define 	VIDEO_CTRL_CUSTOM_WARNING_PUSH         	0x0102
#define 	VIDEO_CTRL_CUSTOM_DISTORTION          	0x0103
//video control command others
#define		VIDEO_CTRL_QUALITY						0x1000


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

typedef struct video_iot_config_t {
	int 	on;
	int		image_roll;
	int		night;
	int		watermark;
	int		wdr;
	int		glimmer;
	int		recording;
	int		motion_switch;
	int		motion_alarm;
	int		motion_sensitivity;
	char	motion_start[MAX_SYSTEM_STRING_SIZE];
	char	motion_end[MAX_SYSTEM_STRING_SIZE];
	int		custom_local_save;
	int		custom_cloud_save;
	int		custom_warning_push;
	int		custom_distortion;
} video_iot_config_t;

/*
 * function
 */
int server_video_start(void);
int server_video_message(message_t *msg);

#endif
