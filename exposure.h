/*
 * exposure.h
 *
 *  Created on: Sep 8, 2020
 *      Author: ning
 */

#ifndef SERVER_VIDEO_EXPOSURE_H_
#define SERVER_VIDEO_EXPOSURE_H_

/*
 * header
 */
#include "../../server/config/config_video_interface.h"

/*
 * define
 */
#define	AE_FRAME_INTERVAL		60

/*
 * structure
 */

/*
 * function
 */
int exposure_int(isp_ae_para_t *ctrl);
int exposure_release(void);
int exposure_proc(isp_ae_para_t *ctrl, int frame);

#endif /* SERVER_VIDEO_EXPOSURE_H_ */
