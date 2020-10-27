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
#include "config.h"

/*
 * define
 */
#define	THREAD_OSD			0
#define THREAD_3ACTRL		1
#define	THREAD_MD			2

#define	RUN_MODE_SAVE			0
#define RUN_MODE_SEND_MISS		1
#define	RUN_MODE_SEND_MICLOUD	2

#define		VIDEO_INIT_CONDITION_NUM			2
#define		VIDEO_INIT_CONDITION_CONFIG			0
#define		VIDEO_INIT_CONDITION_MIIO_TIME		1

/*
 * structure
 */

/*
 * function
 */

#endif /* SERVER_VIDEO_VIDEO_H_ */
