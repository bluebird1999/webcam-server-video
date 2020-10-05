/*
 * video.h
 *
 *  Created on: Aug 28, 2020
 *      Author: ning
 */

#ifndef SERVER_VIDEO_VIDEO_H_
#define SERVER_VIDEO_VIDEO_H_

/*
 * header
 */
#include <rtscamkit.h>
#include <rtsavapi.h>
#include <rtsvideo.h>
#include "../../server/config/config_video_interface.h"

/*
 * define
 */
#define	THREAD_OSD			0
#define THREAD_3ACTRL		1
#define	THREAD_MD			2

#define	RUN_MODE_SAVE			0
#define RUN_MODE_SEND_MISS		1
#define	RUN_MODE_SEND_MICLOUD	2
/*
 * structure
 */
typedef struct video_stream_t {
	//channel
	int isp;
	int h264;
	int	jpg;
	int osd;
	//data
	int	frame;
} video_stream_t;
/*
 * function
 */

#endif /* SERVER_VIDEO_VIDEO_H_ */
