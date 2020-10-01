/*
 * isp.h
 *
 *  Created on: Sep 8, 2020
 *      Author: ning
 */

#ifndef SERVER_VIDEO_ISP_H_
#define SERVER_VIDEO_ISP_H_

/*
 * header
 */
#include "../../server/config/config_video_interface.h"

/*
 * define
 */


/*
 * structure
 */

/*
 * function
 */
int isp_get_attr(unsigned int id);
int isp_set_attr(unsigned int id, int value);
int isp_init(video_isp_config_t *ctrl);

#endif /* SERVER_VIDEO_ISP_H_ */
