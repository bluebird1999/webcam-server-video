/*
 * osd.h
 *
 *  Created on: Sep 9, 2020
 *      Author: ning
 */

#ifndef SERVER_VIDEO_OSD_H_
#define SERVER_VIDEO_OSD_H_

/*
 * header
 */
#include <rtscamkit.h>
#include <rtsavapi.h>
#include <rtsvideo.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "../../server/config/config_video_interface.h"

/*
 * define
 */
#define	OSD_FRAME_INTERVAL			5

/*
 * structure
 */
typedef struct osd_run_t {
    int							stream;
	struct rts_video_osd2_attr 	*osd_attr;
	unsigned char				*ipattern;
	unsigned char				*image2222;
	unsigned char				*image8888;
	FT_Library 					library;
    FT_Face 					face;
    int							color;
    int							rotate;
    int							alpha;
    int							pixel_size;
    int							offset;
} osd_run_t;
/*
 * function
 */
int osd_init(video_osd_config_t *ctrl, int stream);
int osd_release(void);
int osd_proc(video_osd_config_t *ctrl, int frame);


#endif /* SERVER_VIDEO_OSD_H_ */
