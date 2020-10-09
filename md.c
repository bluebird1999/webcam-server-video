/*
 * md.c
 *
 *  Created on: Oct 2, 2020
 *      Author: ning
 */

/*
 * header
 */
//system header
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <rtscamkit.h>
#include <rtsavapi.h>
#include <rtsvideo.h>
#include <rtsbmp.h>
//program header
#include "../../tools/tools_interface.h"
#include "../../server/config/config_video_interface.h"
#include "../../server/miio/miio_interface.h"
//server header
#include "md.h"


/*
 * static
 */

//variable
static video_md_config_t	config;
static struct rts_video_md_attr *attr = NULL;
struct rts_video_md_result result;
md_bmp_data_t bmp;
static const int GRID_R = 72;
static const int GRID_C = 128;
//function
static int md_enable(int polling, int trig, unsigned int data_mode_mask, int width, int height, int sensitivity, md_bmp_data_t *bmp);
static int md_disable(void);
static int md_motioned(int idx, void *priv);
static int md_received(int idx, struct rts_video_md_result *result, void *priv);
static int md_save_data(md_bmp_data_t *bmp);
static int md_process_data(struct rts_video_md_result *result, md_bmp_data_t *bmp);
static int md_copy_data(unsigned char *psrc, int bpp, int w, int h, unsigned int bpl, unsigned char *pdst);
static int md_get_scheduler_time(char *input, scheduler_time_t *st, int *mode);

/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */
static int md_get_scheduler_time(char *input, scheduler_time_t *st, int *mode)
{
    char timestr[16];
    char start_hour_str[4]={0};
    char start_min_str[4]={0};
    char end_hour_str[4]={0};
    char end_min_str[4]={0};
    int start_hour = 0;
    int start_min = 0;
    int end_hour = 0;
    int end_min = 0;
    if(strlen(input) > 0)
    {
        memcpy(timestr,input,strlen(input));
		memcpy(start_hour_str,timestr,2);
        start_hour_str[2] = '\0';
		memcpy(start_min_str,timestr+3,2);
        start_min_str[2] = '\0';
        memcpy(end_hour_str,timestr+3+3,2);
        end_hour_str[2] = '\0';
		memcpy(end_min_str,timestr+3+3+3,2);
        end_min_str[2] = '\0';
        log_info("time:%s:%s-%s:%s\n",start_hour_str,start_min_str,end_hour_str,end_min_str);
        start_hour =  atoi(start_hour_str);
        start_min =  atoi(start_min_str);
        end_hour =  atoi(end_hour_str);
        end_min =  atoi(end_min_str);

        if(!start_hour&&!start_min&&!end_hour&&!end_min){
            *mode = 0;
        }
        else {
        	*mode = 1;
			st->start_hour = start_hour;
			st->start_min = start_min;
			st->start_sec= 0;
            if(end_hour > start_hour) {
    			st->stop_hour = end_hour;
    			st->stop_min = end_min;
    			st->stop_sec= 0;
            }
            else if(end_hour == start_hour) {
                if(end_min > start_min) {
        			st->stop_hour = end_hour;
        			st->stop_min = end_min;
        			st->stop_sec= 0;
                }
                else if(end_min == start_min) {
                    *mode = 0;
        			st->stop_hour = end_hour;
        			st->stop_min = end_min;
        			st->stop_sec= 0;
                }
                else {
        			st->stop_hour = 23;
        			st->stop_min = 59;
        			st->stop_sec= 0;
                }
            }
            else {
    			st->stop_hour = 23;
    			st->stop_min = 59;
    			st->stop_sec= 0;
            }
        }
    }
    else
    	*mode = 0;
    return 0;
}

static int md_copy_data(unsigned char *psrc, int bpp, int w, int h, unsigned int bpl, unsigned char *pdst)
{
	int i;
	int j;
	unsigned char *ptr = pdst;
	if (bpp == 8) {
		for (j = 0; j < h; j++) {
			memcpy(ptr, psrc + w * j, w);
			ptr += bpl;
		}
		return 0;
	}
	for (j = 0; j < h; j++) {
		for (i = 0; i < w; i++) {
			unsigned char val = 0;
			unsigned char mask = 0;
			int x;
			int y;
			int index = w * j + i;
			int coef = 0;
			y = (index * bpp) % 8;
			x = (index * bpp - y) / 8;
			switch (bpp) {
			case 1:
				mask = 0x1;
				coef = 255;
				break;
			case 2:
				mask = 0x3;
				coef = 85;
				break;
			case 4:
				mask = 0xf;
				coef = 17;
				break;
			}
			val = (psrc[x] >> y) & mask;
			ptr[i] = val * coef;
		}
		ptr += bpl;
	}
	return 0;
}

static int md_save_data(md_bmp_data_t *bmp)
{
	struct rts_bmp_encin encin;
	char filename[64];
	static int index;
	if (!bmp || !bmp->vm_addr)
		return 0;
	encin.psrc = bmp->vm_addr;
	encin.length = bmp->length;
	encin.fmt = RTS_PIX_FMT_GRAY_8;
	encin.width = bmp->width;
	encin.height = bmp->height;
	encin.align = RTS_BMP_BITS_DATA_CONTINUOUS;
	snprintf(filename, sizeof(filename), "%d.bmp", index++);
	rts_bmp_save(&encin, filename);
	return 0;
}

static int md_process_data(struct rts_video_md_result *result, md_bmp_data_t *bmp)
{
	unsigned int i;
	unsigned int bytesused = 0;
	if (!result)
		return -1;
	if (!bmp)
		return -1;
	if (!bmp->vm_addr)
		return -1;
	memset(bmp->vm_addr, 0x0, bmp->length);
	log_info("----count = %d----\n", result->count);
	for (i = 0; i < result->count; i++) {
		struct rts_video_md_type_data *md_data = result->results + i;
		struct rts_video_md_data *data;
		if (!md_data->data)
			continue;
		data = md_data->data;
		if (bytesused + GRID_C * GRID_R > bmp->length)
			return -1;
		md_copy_data(data->vm_addr, data->bpp, GRID_C, GRID_R, bmp->width,
				bmp->vm_addr + bytesused);
		bytesused += GRID_C * (GRID_R + 1);
	}
	md_save_data(bmp);
	return 0;
}

static int md_motioned(int idx, void *priv)
{
	log_info("motion detected\n");
	return 0;
}

static int md_received(int idx, struct rts_video_md_result *result, void *priv)
{
	int ret = 0;
	int now = 0;
	static int last_report=0;
	if (!result)
		return -1;
	log_err("motion data received\n");
	ret = md_process_data(result, priv);
	if(!ret) {
		if( config.cloud_report ) {
			now = time_get_now_ms();
			if( config.alarm_interval < 1)
				config.alarm_interval = 1;
			if( ( now - last_report) >= config.alarm_interval * 60000 ) {
				message_t msg;
				/********message body********/
				msg_init(&msg);
				msg.message = MSG_MICLOUD_WARNING_NOTICE;
				msg.sender = msg.receiver = SERVER_VIDEO;
				msg.arg_in.cat = 0;
				msg.arg_in.dog = 0;
				msg.arg = bmp.vm_addr;
				msg.arg_size = bmp.length;
//				ret = server_micloud_message(msg);
				/********message body********/
				last_report = now;
			}
		}
	}
	return 0;
}

static int md_enable(int polling, int trig, unsigned int data_mode_mask, int width, int height, int sensitivity, md_bmp_data_t *bmp)
{
	int i;
	int enable = 0;
	int ret;
	for (i = 0; i < attr->number; i++) {
		struct rts_video_md_block *block = attr->blocks + i;
		unsigned int detect_mode;
		int len;
		if (trig)
			detect_mode = RTS_VIDEO_MD_DETECT_USER_TRIG;
		else
			detect_mode = RTS_VIDEO_MD_DETECT_HW;
		block->enable = 0;
		if (i > 0)
			continue;
		log_info("%x %x %d\n",
				block->supported_data_mode,
				block->supported_detect_mode,
				block->supported_grid_num);
		data_mode_mask &= block->supported_data_mode;
		if (!RTS_CHECK_BIT(block->supported_detect_mode, detect_mode)) {
			log_err("detect mode %x is not support\n", detect_mode);
			continue;
		}
		if (GRID_R * GRID_C > block->supported_grid_num) {
			log_err("grid size (%d,%d) is out of range\n", GRID_R, GRID_C);
			continue;
		}
		len = RTS_DIV_ROUND_UP(GRID_R * GRID_C, 8);
		block->data_mode_mask = data_mode_mask;
		block->detect_mode = detect_mode;
		block->area.start.x = 0;
		block->area.start.y = 0;
		block->area.cell.width = width / GRID_C;
		block->area.cell.height = height / GRID_R;
		block->area.size.rows = GRID_R;
		block->area.size.columns = GRID_C;
		memset(block->area.bitmap.vm_addr, 0xff, len);
		block->sensitivity = sensitivity;
		block->percentage = 30;
		block->frame_interval = 5;
		if (bmp) {
			unsigned int length = 0;
			unsigned int count;
			count = rts_memweight((uint8_t *)&data_mode_mask,
								  sizeof(data_mode_mask));
			length = GRID_C * (GRID_R + 1) * count;
			bmp->vm_addr = rts_calloc(1, length);
			if (bmp->vm_addr) {
					bmp->length = length;
					bmp->width = GRID_C;
					bmp->height = (GRID_R + 1) * count;
			}
		}
		if (!polling) {
			block->ops.motion_detected = md_motioned;
			block->ops.motion_received = md_received;
			block->ops.priv = bmp;
		}
		block->enable = 1;
		enable++;
	}
	ret = rts_av_set_isp_md(attr);
	if (ret)
		return ret;
	if (!enable)
		return -1;
	return 0;
}

static int md_disable(void)
{
	int i;
	for (i = 0; i < attr->number; i++) {
		struct rts_video_md_block *block = attr->blocks + i;
		block->enable = 0;
	}
	return rts_av_set_isp_md(attr);
}

/*
 * interface
 */
int md_check_scheduler_time(scheduler_time_t *st, int *mode)
{
	int ret = 0;
    time_t timep;
    struct tm  *tv;
    int	start, end, now;

	if( *mode==0 ) return 1;
    timep = time(NULL);
    tv = localtime(&timep);
    start = st->start_hour * 3600 + st->start_min * 60 + st->start_sec;
    end = st->stop_hour * 3600 + st->stop_min * 60 + st->stop_sec;
    now = tv->tm_hour * 3600 + tv->tm_min * 60 + tv->tm_sec;
    if( now >= start && now <= end ) return 1;
    return ret;
}

int md_proc(void)
{
	int ret;
	static int status = 0;
	if (!status) {
		if (config.trig) {
			ret = rts_av_trig_isp_md(attr, 0);
			if (ret)
				return 0;
			if (config.polling)
				status = 1;
		} else if (config.polling) {
			status = rts_av_check_isp_md_status(attr, 0);
		}
		return 0;
	}
	ret = rts_av_get_isp_md_result(attr, 0, &result);
	if (ret)
		return 0;
	log_info("get data\n");
	md_process_data(&result, &bmp);
	status = 0;
	return 0;
}

int md_init(video_md_config_t *md_config, int width, int height, scheduler_time_t *st, int *md)
{
	int ret;
	int mask;
	memset(&bmp, 0, sizeof(bmp));
	memcpy(&config, md_config, sizeof(video_md_config_t) );
	md_get_scheduler_time(config.end, st, md);
	ret = rts_av_query_isp_md(&attr, width, height);
	if (ret) {
		log_err("query isp md attr fail, ret = %d\n", ret);
		md_release();
	}
	mask = RTS_VIDEO_MD_DATA_TYPE_AVGY |
           RTS_VIDEO_MD_DATA_TYPE_RLTPRE |
           RTS_VIDEO_MD_DATA_TYPE_RLTCUR |
           RTS_VIDEO_MD_DATA_TYPE_BACKY |
           RTS_VIDEO_MD_DATA_TYPE_BACKF |
           RTS_VIDEO_MD_DATA_TYPE_BACKC;
	ret = md_enable(config.polling, config.trig, mask, width, height, config.sensitivity, &bmp);
	if (ret) {
		log_err("enable md fail\n");
		md_release();
	}
	if (config.polling) {
		int i;
		unsigned int mask = attr->blocks->data_mode_mask;
		rts_av_init_md_result(&result, mask);
		log_info("%d\n", result.count);
		for (i = 0; i < result.count; i++) {
			struct rts_video_md_type_data *pdata;
			pdata = result.results + i;
			log_info("0x%x\n", pdata->type);
		}
	}
}

int md_release(void)
{
	if( config.polling)
		rts_av_uninit_md_result(&result);
	if( attr )
		md_disable();
	RTS_SAFE_RELEASE(attr, rts_av_release_isp_md);
    RTS_SAFE_DELETE(bmp.vm_addr);
    bmp.length = 0;
}
