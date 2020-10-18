/*
 * video.c
 *
 *  Created on: Aug 27, 2020
 *      Author: ning
 */

/*
 * header
 */
//system header
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <rtscamkit.h>
#include <rtsavapi.h>
#include <rtsvideo.h>
#include <malloc.h>
#include <dmalloc.h>
//program header
#include "../../manager/manager_interface.h"
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/recorder/recorder_interface.h"
//server header
#include "video.h"
#include "video_interface.h"
#include "video.h"
#include "white_balance.h"
#include "focus.h"
#include "exposure.h"
#include "isp.h"
#include "md.h"
#include "config.h"

/*
 * static
 */
//variable
static 	message_buffer_t	message;
static 	server_info_t 		info;
static	video_stream_t		stream;
static	video_config_t		config;


//function
//common
static void *server_func(void);
static int server_message_proc(void);
static int server_release(void);
static int server_restart(void);
static void task_default(void);
static void task_error(void);
static void task_start(void);
static void task_stop(void);
static void task_control(void);
static void task_control_ext(void);
static int send_message(int receiver, message_t *msg);
static int server_get_status(int type);
static int server_set_status(int type, int st);
//specific
static int write_video_buffer(struct rts_av_buffer *data, int id, int target);
static void video_mjpeg_func(void *priv, struct rts_av_profile *profile, struct rts_av_buffer *buffer);
static int video_snapshot(void);
static int *video_3acontrol_func(void *arg);
static int *video_osd_func(void *arg);
static int *video_md_func(void *arg);
static int stream_init(void);
static int stream_destroy(void);
static int stream_start(void);
static int stream_stop(void);
static int video_init(void);
static int video_main(void);
static int send_iot_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg,int size);
static int video_get_iot_config(video_iot_config_t *tmp);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int send_iot_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg, int size)
{
	int ret = 0;
    /********message body********/
	msg_init(msg);
	memcpy(&(msg->arg_pass), &(org_msg->arg_pass),sizeof(message_arg_t));
	msg->message = id | 0x1000;
	msg->sender = msg->receiver = SERVER_VIDEO;
	msg->result = result;
	msg->arg = arg;
	msg->arg_size = size;
	ret = send_message(receiver, msg);
	/***************************/
	return ret;
}

static int send_message(int receiver, message_t *msg)
{
	int st;
	switch(receiver) {
	case SERVER_DEVICE:
		break;
	case SERVER_KERNEL:
		break;
	case SERVER_REALTEK:
		break;
	case SERVER_MIIO:
		st = server_miio_message(msg);
		break;
	case SERVER_MISS:
		st = server_miss_message(msg);
		break;
	case SERVER_MICLOUD:
		break;
	case SERVER_AUDIO:
		st = server_audio_message(msg);
		break;
	case SERVER_RECORDER:
		break;
	case SERVER_PLAYER:
		break;
	case SERVER_MANAGER:
		st = manager_message(msg);
		break;
	}
	return st;
}

static int video_get_iot_config(video_iot_config_t *tmp)
{
	int ret = 0, st;
	memset(tmp,0,sizeof(video_iot_config_t));
	st = info.status;
	if( st <= STATUS_WAIT ) return -1;
	tmp->on = ( st == STATUS_RUN ) ? 1:0;
	if( config.h264.h264_attr.rotation == RTS_AV_ROTATION_0 ) tmp->image_roll = 0;
//	else if( config.h264.h264_attr.rotation == RTS_AV_ROTATION_90R ) tmp->image_roll = 90;
//	else if( config.h264.h264_attr.rotation == RTS_AV_ROTATION_90L ) tmp->image_roll = 270;
	else if( config.h264.h264_attr.rotation == RTS_AV_ROTATION_180 ) tmp->image_roll = 180;
	if( config.isp.smart_ir_mode == RTS_ISP_SMART_IR_MODE_AUTO) tmp->night = 0;
	else if( config.isp.smart_ir_mode == RTS_ISP_SMART_IR_MODE_DISABLE) tmp->night = 1;
	else if( config.isp.smart_ir_mode == RTS_ISP_SMART_IR_MODE_LOW_LIGHT_PRIORITY) tmp->night = 2;
	tmp->watermark = config.osd.enable;
	tmp->wdr = config.isp.wdr_mode;
	tmp->glimmer = 0;
	tmp->motion_switch = config.md.enable;
	tmp->motion_alarm = config.md.alarm_interval;
	tmp->motion_sensitivity = config.md.sensitivity;
	strcpy( tmp->motion_start, config.md.start);
	strcpy( tmp->motion_end, config.md.end);
	tmp->custom_warning_push = config.md.cloud_report;
	tmp->custom_distortion = config.isp.ldc;
	return ret;
}

static int video_process_direct_ctrl(message_t *msg)
{
	int ret=0;
	message_t send_msg;
	if( msg->arg_in.cat == VIDEO_CTRL_WDR_MODE ) {
		int temp = *((int*)(msg->arg));
		if( temp != config.isp.wdr_mode) {
			ret = isp_set_attr(RTS_VIDEO_CTRL_ID_WDR_MODE, temp);
			if(!ret) {
				config.isp.wdr_mode = *((int*)(msg->arg));
				log_info("changed the wdr = %d", config.isp.wdr_mode);
				config_video_set(CONFIG_VIDEO_ISP, &config.isp);
			}
		}
	}
	else if( msg->arg_in.cat == VIDEO_CTRL_NIGHT_SHOT ) {
		int temp = *((int*)(msg->arg));
		if( temp == 0) {	//automode
			ret = isp_set_attr(RTS_VIDEO_CTRL_ID_SMART_IR_MODE, RTS_ISP_SMART_IR_MODE_AUTO);
			if(!ret) {
				config.isp.smart_ir_mode = RTS_ISP_SMART_IR_MODE_AUTO;
			}
		}
		else if( temp == 1) {//close
			ret = isp_set_attr(RTS_VIDEO_CTRL_ID_SMART_IR_MODE, RTS_ISP_SMART_IR_MODE_DISABLE);
			if(!ret) {
				config.isp.smart_ir_mode = RTS_ISP_SMART_IR_MODE_DISABLE;
			}
		}
		else if( temp == 2) {//open
			ret = isp_set_attr(RTS_VIDEO_CTRL_ID_SMART_IR_MODE, RTS_ISP_SMART_IR_MODE_LOW_LIGHT_PRIORITY);
			if(!ret) {
				config.isp.smart_ir_mode = RTS_ISP_SMART_IR_MODE_LOW_LIGHT_PRIORITY;
			}
		}
		if(!ret) {
			log_info("changed the smart night mode = %d", config.isp.smart_ir_mode);
			config_video_set(CONFIG_VIDEO_ISP, &config.isp);
		    /********message body********/
/*			msg_init(&send_msg);
			send_msg.message = MSG_DEVICE_SET_PARA;
			send_msg.sender = send_msg.receiver = SERVER_VIDEO;
			send_msg.arg_in.cat = DEVICE_CTRL_IR_SWITCH;
			send_msg.arg = msg->arg;
			send_msg.arg_size = msg->arg_size;
			ret = server_device_message(&send_msg);
*/
			/***************************/
		}
	}
	else if( msg->arg_in.cat == VIDEO_CTRL_CUSTOM_DISTORTION ) {
		int temp = *((int*)(msg->arg));
		if( temp != config.isp.ldc ) {
			ret = isp_set_attr(RTS_VIDEO_CTRL_ID_LDC, temp );
			if(!ret) {
				config.isp.ldc = temp;
				log_info("changed the lens distortion correction = %d", config.isp.ldc);
				config_video_set(CONFIG_VIDEO_ISP, &config.isp);
			}
		}
	}
	ret = send_iot_ack(msg, &send_msg, MSG_VIDEO_CTRL_DIRECT, msg->receiver, ret, 0, 0);
	return ret;
}

static void video_mjpeg_func(void *priv, struct rts_av_profile *profile, struct rts_av_buffer *buffer)
{
    static unsigned long index;
    char filename[32];
    FILE *pfile = NULL;
    snprintf(filename, 32, "%s%s%d%s", JPG_LIBRARY_PATH, "snap_", index++, ".jpg");
    pfile = fopen(filename, "wb");
    if (!pfile) {
		log_err("open %s fail\n", filename);
		return;
    }
    fwrite(buffer->vm_addr, 1, buffer->bytesused, pfile);
    RTS_SAFE_RELEASE(pfile, fclose);
    return;
}

static int video_snapshot(void)
{
	struct rts_av_callback cb;
	int ret = 0;
	cb.func = video_mjpeg_func;
	cb.start = 0;
	cb.times = 1;
	cb.interval = 0;
	cb.type = RTS_AV_CB_TYPE_ASYNC;
	cb.priv = NULL;
	ret = rts_av_set_callback(stream.jpg, &cb, 0);
	if (ret) {
		log_err("set mjpeg callback fail, ret = %d\n", ret);
		return ret;
	}
	return ret;
}


static int *video_md_func(void *arg)
{
	video_md_config_t ctrl;
	scheduler_time_t  scheduler_time;
	int mode;
	int st;

    misc_set_thread_name("server_video_md");
    pthread_detach(pthread_self());
    //init
    memcpy( &ctrl, (video_md_config_t*)arg, sizeof(video_md_config_t) );
    md_init( &ctrl, config.profile.profile[config.profile.quality].video.width,
    		config.profile.profile[config.profile.quality].video.height,
			&scheduler_time, &mode);
    misc_set_bit(&info.thread_start, THREAD_MD, 1);
    while( 1 ) {
    	st = info.status;
    	if( info.exit ) break;
    	if( st != STATUS_START && st != STATUS_RUN )
    		break;
    	else if( st == STATUS_START )
    		continue;
    	usleep(10);
    	if( ctrl.enable && md_check_scheduler_time(&scheduler_time, &mode) )
    		md_proc();
    }
    //release
    log_info("-----------thread exit: server_video_md-----------");
    md_release();
    misc_set_bit(&info.thread_start, THREAD_MD, 0);
    pthread_exit(0);
}

static int *video_3acontrol_func(void *arg)
{
	video_3actrl_config_t ctrl;
	server_status_t st;
    misc_set_thread_name("server_video_3a_control");
    pthread_detach(pthread_self());
    //init
    memcpy( &ctrl, (video_3actrl_config_t*)arg, sizeof(video_3actrl_config_t));
    white_balance_init( &ctrl.awb_para);
    exposure_init(&ctrl.ae_para);
    focus_init(&ctrl.af_para);
    misc_set_bit(&info.thread_start, THREAD_3ACTRL, 1);
    while( 1 ) {
    	st = info.status;
    	if( info.exit ) break;
    	if( st != STATUS_START && st != STATUS_RUN )
    		break;
    	else if( st == STATUS_START )
    		continue;
    	white_balance_proc( &ctrl.awb_para,stream.frame);
    	exposure_proc(&ctrl.ae_para,stream.frame);
    	focus_proc(&ctrl.af_para,stream.frame);
    }
    //release
    log_info("-----------thread exit: server_video_3a_control-----------");
    white_balance_release();
    exposure_release();
    focus_release();
    misc_set_bit(&info.thread_start, THREAD_3ACTRL, 0);
    pthread_exit(0);
}

static int *video_osd_func(void *arg)
{
	int ret=0, st;
	video_osd_config_t ctrl;
    misc_set_thread_name("server_video_osd");
    pthread_detach(pthread_self());
    //init
    memcpy( &ctrl,(video_osd_config_t*)arg, sizeof(video_osd_config_t));
    ret = osd_init(&ctrl, stream.osd);
    if( ret != 0) {
    	log_err("osd init error!");
    	goto exit;
    }
    misc_set_bit(&info.thread_start, THREAD_OSD, 1);
    while( 1 ) {
    	if( info.exit ) break;
    	st = info.status;
    	if( st != STATUS_START && st != STATUS_RUN )
    		break;
    	else if( st == STATUS_START )
    		continue;
    	osd_proc(&ctrl,stream.frame);
    }
    //release
exit:
    log_info("-----------thread exit: server_video_osd-----------");
    osd_release();
    misc_set_bit(&info.thread_start, THREAD_OSD, 0);
    pthread_exit(0);
}

static int stream_init(void)
{
	stream.isp = -1;
	stream.h264 = -1;
	stream.jpg = -1;
	stream.osd = -1;
	stream.frame = 0;
}

static int stream_destroy(void)
{
	int ret = 0;
	if (stream.isp >= 0) {
		rts_av_destroy_chn(stream.isp);
		stream.isp = -1;
	}
	if (stream.h264 >= 0) {
		rts_av_destroy_chn(stream.h264);
		stream.h264 = -1;
	}
	if (stream.osd >= 0) {
		rts_av_destroy_chn(stream.osd);
		stream.osd = -1;
	}
	if (stream.jpg >= 0) {
		rts_av_destroy_chn(stream.jpg);
		stream.jpg = -1;
	}
	return ret;
}

static int stream_start(void)
{
	int ret=0;
	pthread_t isp_3a_id, osd_id, md_id;
	config.profile.profile[config.profile.quality].fmt = RTS_V_FMT_YUV420SEMIPLANAR;
	ret = rts_av_set_profile(stream.isp, &config.profile.profile[config.profile.quality]);
	if (ret) {
		log_err("set isp profile fail, ret = %d", ret);
		return -1;
	}
	if( stream.isp != -1 ) {
		ret = rts_av_enable_chn(stream.isp);
		if (ret) {
			log_err("enable isp fail, ret = %d", ret);
			return -1;
		}
	}
	else {
		return -1;
	}
	if( stream.h264 != -1 ) {
		ret = rts_av_enable_chn(stream.h264);
		if (ret) {
			log_err("enable h264 fail, ret = %d", ret);
			return -1;
		}
	}
	else {
		return -1;
	}
	if( config.osd.enable ) {
		if( stream.osd != -1 ) {
			ret = rts_av_enable_chn(stream.osd);
			if (ret) {
				log_err("enable osd fail, ret = %d", ret);
				return -1;
			}
		}
		else {
			return -1;
		}
	}
	if( config.jpg.enable ) {
		if( stream.jpg != -1 ) {
			ret = rts_av_enable_chn(stream.jpg);
			if (ret) {
				log_err("enable jpg fail, ret = %d", ret);
				return -1;
			}
		}
		else {
			return -1;
		}
	}
	stream.frame = 0;
    ret = rts_av_start_recv(stream.h264);
    if (ret) {
    	log_err("start recv h264 fail, ret = %d", ret);
    	return -1;
    }
    //start the 3a control thread
	ret = pthread_create(&isp_3a_id, NULL, video_3acontrol_func, (void*)&config.a3ctrl);
	if(ret != 0) {
		log_err("3a control thread create error! ret = %d",ret);
		return -1;
	 }
	else {
		log_info("3a control thread create successful!");
	}
	if( config.osd.enable && stream.osd != -1 ) {
		//start the osd thread
		ret = pthread_create(&osd_id, NULL, video_osd_func, (void*)&config.osd);
		if(ret != 0) {
			log_err("osd thread create error! ret = %d",ret);
		 }
		else {
			log_info("osd thread create successful!");
		}
	}
	if( config.md.enable ) {
		//start the osd thread
		ret = pthread_create(&md_id, NULL, video_md_func, (void*)&config.md);
		if(ret != 0) {
			log_err("md thread create error! ret = %d",ret);
		 }
		else {
			log_info("md thread create successful!");
		}
	}
    return 0;
}

static int stream_stop(void)
{
	int ret=0;
	if(stream.h264!=-1)
		ret = rts_av_stop_recv(stream.h264);
	if(stream.jpg!=-1)
		ret = rts_av_disable_chn(stream.jpg);
	if(stream.osd!=-1)
		ret = rts_av_disable_chn(stream.osd);
	if(stream.h264!=-1)
		ret = rts_av_disable_chn(stream.h264);
	if(stream.isp!=-1)
		ret = rts_av_disable_chn(stream.isp);
	return ret;
}

static int video_init(void)
{
	int ret;
	stream_init();
	stream.isp = rts_av_create_isp_chn(&config.isp.isp_attr);
	if (stream.isp < 0) {
		log_err("fail to create isp chn, ret = %d", stream.isp);
		return -1;
	}
	log_info("isp chnno:%d", stream.isp);
	stream.h264 = rts_av_create_h264_chn(&config.h264.h264_attr);
	if (stream.h264 < 0) {
		log_err("fail to create h264 chn, ret = %d", stream.h264);
		return -1;
	}
	log_info("h264 chnno:%d", stream.h264);
	config.profile.profile[config.profile.quality].fmt = RTS_V_FMT_YUV420SEMIPLANAR;
	ret = rts_av_set_profile(stream.isp, &config.profile.profile[config.profile.quality]);
	if (ret) {
		log_err("set isp profile fail, ret = %d", ret);
		return -1;
	}
	if( config.osd.enable ) {
        stream.osd = rts_av_create_osd_chn();
        if (stream.osd < 0) {
        	log_err("fail to create osd chn, ret = %d\n", stream.osd);
        	return -1;
        }
        log_info("osd chnno:%d", stream.osd);
        ret = rts_av_bind(stream.isp, stream.osd);
    	if (ret) {
    		log_err("fail to bind isp and osd, ret %d", ret);
    		return -1;
    	}
    	ret = rts_av_bind(stream.osd, stream.h264);
    	if (ret) {
    		log_err("fail to bind osd and h264, ret %d", ret);
    		return -1;
    	}
	}
	else {
    	ret = rts_av_bind(stream.isp, stream.h264);
    	if (ret) {
    		log_err("fail to bind isp and h264, ret %d", ret);
    		return -1;
    	}
	}
	if(config.jpg.enable) {
        stream.jpg = rts_av_create_mjpeg_chn(&config.jpg.jpg_ctrl);
        if (stream.jpg < 0) {
                log_err("fail to create jpg chn, ret = %d\n", stream.jpg);
                return -1;
        }
        log_info("jpg chnno:%d", stream.jpg);
    	ret = rts_av_bind(stream.isp, stream.jpg);
    	if (ret) {
    		log_err("fail to bind isp and jpg, ret %d", ret);
    		return -1;
    	}
	}
	isp_init(&config.isp);
	return 0;
}

static int video_main(void)
{
	int ret = 0;
	struct rts_av_buffer *buffer = NULL;
	usleep(1000);
	if (rts_av_poll(stream.h264))
		return 0;
	if (rts_av_recv(stream.h264, &buffer))
		return 0;
	if (buffer) {
		if( misc_get_bit(info.status2, RUN_MODE_SEND_MISS) ) {
			if( write_video_buffer(buffer, MSG_MISS_VIDEO_DATA, SERVER_VIDEO) != 0 )
				log_err("Miss ring buffer push failed!");
		}
		if( misc_get_bit(info.status2, RUN_MODE_SAVE) ) {
			if( write_video_buffer(buffer, MSG_RECORDER_VIDEO_DATA, SERVER_RECORDER) != 0 )
				log_err("Recorder ring buffer push failed!");
		}
		if( misc_get_bit(info.status2, RUN_MODE_SEND_MICLOUD) ) {
/*	wait for other server
 * 			if( write_video_buffer(buffer, MSG_MICLOUD_VIDEO_DATA, SERVER_MICLOUD) != 0 )
				log_err("Micloud ring buffer push failed!");
*/
		}
		stream.frame++;
		rts_av_put_buffer(buffer);
	}
    return ret;
}

static int write_video_buffer(struct rts_av_buffer *data, int id, int target)
{
	int ret=0;
	message_t msg;
	av_data_info_t	info;
    /********message body********/
	msg_init(&msg);
	msg.message = id;
	msg.extra = data->vm_addr;
	msg.extra_size = data->bytesused;
	info.flag = data->flags;
	info.frame_index = data->frame_idx;
	info.index = data->index;
	info.timestamp = data->timestamp;
	info.fps = config.profile.profile[config.profile.quality].video.denominator;
	info.width = config.profile.profile[config.profile.quality].video.width;
	info.height = config.profile.profile[config.profile.quality].video.height;
	msg.arg = &info;
	msg.arg_size = sizeof(av_data_info_t);
	if( target == SERVER_VIDEO )
		ret = server_miss_video_message(&msg);
//	else if( target == MSG_MICLOUD_VIDEO_DATA )
//		ret = server_micloud_video_message(&msg);
	else if( target == MSG_RECORDER_VIDEO_DATA )
		ret = server_recorder_video_message(&msg);
	/****************************/
	return ret;
}

static void server_thread_termination(int sign)
{
    /********message body********/
	message_t msg;
	msg_init(&msg);
	msg.message = MSG_VIDEO_SIGINT;
	msg.sender = msg.receiver = SERVER_VIDEO;
	manager_message(&msg);
	/****************************/
}

static int server_restart(void)
{
	int ret = 0;
	stream_stop();
	stream_destroy();
	return ret;
}

static int server_release(void)
{
	int ret = 0;
	stream_stop();
	stream_destroy();
	msg_buffer_release(&message);
	msg_free(&info.task.msg);
	memset(&info,0,sizeof(server_info_t));
	memset(&config,0,sizeof(video_config_t));
	memset(&stream,0,sizeof(video_stream_t));
	return ret;
}

static int server_set_status(int type, int st)
{
	int ret=-1;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS) info.status = st;
	else if(type==STATUS_TYPE_EXIT) info.exit = st;
	else if(type==STATUS_TYPE_CONFIG) config.status = st;
	else if(type==STATUS_TYPE_THREAD_START) info.thread_start = st;
	else if(type==STATUS_TYPE_THREAD_EXIT) info.thread_exit = st;
	else if(type==STATUS_TYPE_MESSAGE_LOCK) info.msg_lock = st;
	else if(type==STATUS_TYPE_STATUS2) info.status2 = st;
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return ret;
}

static int server_get_status(int type)
{
	int st;
	int ret;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_err("add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS) st = info.status;
	else if(type== STATUS_TYPE_EXIT) st = info.exit;
	else if(type==STATUS_TYPE_CONFIG) st = config.status;
	else if(type==STATUS_TYPE_THREAD_START) st = info.thread_start;
	else if(type==STATUS_TYPE_THREAD_EXIT) st = info.thread_exit;
	else if(type==STATUS_TYPE_MESSAGE_LOCK) st = info.msg_lock;
	else if(type==STATUS_TYPE_STATUS2) st = info.status2;
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_err("add unlock fail, ret = %d", ret);
	return st;
}

static int server_message_proc(void)
{
	int ret = 0, ret1 = 0;
	message_t msg,send_msg;
	video_iot_config_t tmp;
	msg_init(&msg);
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	if( info.msg_lock ) {
		ret1 = pthread_rwlock_unlock(&message.lock);
		return 0;
	}
	ret = msg_buffer_pop(&message, &msg);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1) {
		log_err("add message unlock fail, ret = %d\n", ret1);
	}
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1)
		return 0;
	switch(msg.message) {
		case MSG_VIDEO_START:
			if( msg.sender == SERVER_MISS) misc_set_bit(&info.status2, RUN_MODE_SEND_MISS, 1);
			if( msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_SEND_MICLOUD, 1);
			if( msg.sender == SERVER_RECORDER) misc_set_bit(&info.status2, RUN_MODE_SAVE, 1);
			if( info.status == STATUS_RUN ) {
				ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_START, msg.receiver, 0, 0, 0);
				break;
			}
			info.task.func = task_start;
			info.task.start = info.status;
			memcpy(&info.task.msg, &msg,sizeof(message_t));
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_STOP:
			if( msg.sender == SERVER_MISS) misc_set_bit(&info.status2, RUN_MODE_SEND_MISS, 0);
			if( msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_SEND_MICLOUD, 0);
			if( msg.sender == SERVER_RECORDER) misc_set_bit(&info.status2, RUN_MODE_SAVE, 0);
			if( info.status != STATUS_RUN ) {
				ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_STOP, msg.receiver, 0, 0, 0);
				break;
			}
			if( info.status2 > 0 ) {
				ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_STOP, msg.receiver, 0, 0, 0);
				break;
			}
			info.task.func = task_stop;
			info.task.start = info.status;
			msg_deep_copy(&info.task.msg, &msg);
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_CTRL:
			if(msg.arg_in.cat == VIDEO_CTRL_QUALITY) {
				int temp = *((int*)(msg.arg));
				if( temp == config.profile.quality) {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL, msg.receiver, 0, 0, 0 );
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_CTRL_MOTION_SWITCH) {
				int temp = *((int*)(msg.arg));
				if( temp == config.md.enable ) {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL, msg.receiver, 0, 0, 0 );
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_CTRL_MOTION_ALARM_INTERVAL) {
				int temp = *((int*)(msg.arg));
				if( temp == config.md.alarm_interval ) {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL, msg.receiver, 0, 0, 0 );
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_CTRL_MOTION_SENSITIVITY) {
				int temp = *((int*)(msg.arg));
				if( temp == config.md.sensitivity ) {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL, msg.receiver, 0, 0, 0 );
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_CTRL_MOTION_START) {
				char *temp = (char*)(msg.arg);
				if( !strcmp( temp, config.md.start) ) {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL, msg.receiver, 0, 0, 0 );
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_CTRL_MOTION_END) {
				char *temp = (char*)(msg.arg);
				if( !strcmp( temp, config.md.end) ) {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL, msg.receiver, 0, 0, 0 );
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_CTRL_CUSTOM_WARNING_PUSH) {
				int temp = (char*)(msg.arg);
				if( temp == config.md.cloud_report ) {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL, msg.receiver, 0, 0, 0 );
					break;
				}
			}
			info.task.func = task_control;
			info.task.start = info.status;
			msg_deep_copy(&info.task.msg, &msg);
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_CTRL_EXT:
			if( msg.arg_in.cat == VIDEO_CTRL_TIME_WATERMARK ) {
				int temp = *((int*)(msg.arg));
				if( temp  == config.osd.enable) {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL_EXT, msg.receiver, 0, 0, 0 );
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_CTRL_IMAGE_ROLLOVER ) {
				int temp =  *((int*)(msg.arg));
				if( ( (temp == 0) && (config.h264.h264_attr.rotation == RTS_AV_ROTATION_0) ) ||
					( ( temp == 180) && (config.h264.h264_attr.rotation == RTS_AV_ROTATION_180) )	) {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL_EXT, msg.receiver, 0, 0, 0 );
					break;
				}
				else {
					ret = send_iot_ack(&msg, &send_msg, MSG_VIDEO_CTRL_EXT, msg.receiver, -1, 0, 0 );
					break;
				}

			}
			info.task.func = task_control_ext;
			info.task.start = info.status;
			msg_deep_copy(&info.task.msg, &msg);
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_CTRL_DIRECT:
			video_process_direct_ctrl(&msg);
			break;
		case MSG_VIDEO_GET_PARA:
			ret = video_get_iot_config(&tmp);
			send_iot_ack(&msg, &send_msg, MSG_VIDEO_GET_PARA, msg.receiver, ret,
					&tmp, sizeof(video_iot_config_t));
			break;
		case MSG_MANAGER_EXIT:
			info.exit = 1;
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_MIIO_TIME_SYNCHRONIZED:
			misc_set_bit( &info.thread_exit, VIDEO_INIT_CONDITION_MIIO_TIME, 1);
			break;
		default:
			log_err("not processed message = %d", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

static int heart_beat_proc(void)
{
	int ret = 0;
	message_t msg;
	long long int tick = 0;
	tick = time_get_now_stamp();
	if( (tick - info.tick) > 10 ) {
		info.tick = tick;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_HEARTBEAT;
		msg.sender = msg.receiver = SERVER_VIDEO;
		msg.arg_in.cat = info.status;
		msg.arg_in.dog = info.thread_start;
		ret = manager_message(&msg);
		/***************************/
	}
	return ret;
}

/*
 * task
 */
/*
 * task error: error->5 seconds->shut down server->msg manager
 */
static void task_error(void)
{
	unsigned int tick=0;
	switch( info.status ) {
		case STATUS_ERROR:
			log_err("!!!!!!!!error in video, restart in 5 s!");
			info.tick = time_get_now_stamp();
			info.status = STATUS_NONE;
			break;
		case STATUS_NONE:
			tick = time_get_now_stamp();
			if( (tick - info.tick) > 5 ) {
				info.exit = 1;
				info.tick = tick;
			}
			break;
	}
	usleep(1000);
	return;
}
/*
 * task control: restart->wait->change->setup->start->run
 */
static void task_control_ext(void)
{
	static int para_set = 0;
	message_t msg;
	int ret;
	switch(info.status){
		case STATUS_RUN:
			if( !para_set ) info.status = STATUS_RESTART;
			else
				goto success_exit;
			break;
		case STATUS_IDLE:
			if( !para_set ) info.status = STATUS_RESTART;
			else {
				if( info.task.start == STATUS_IDLE ) goto success_exit;
				else info.status = STATUS_START;
			}
			break;
		case STATUS_WAIT:
			if( info.task.msg.arg_in.cat == VIDEO_CTRL_TIME_WATERMARK ) {
				config.osd.enable = *((int*)(info.task.msg.arg));
				log_info("changed the osd switch = %d", config.osd.enable);
				config_video_set(CONFIG_VIDEO_OSD,  &config.osd);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_CTRL_IMAGE_ROLLOVER ) {
				int temp = *((int*)(info.task.msg.arg));
				if( temp == 0 ) config.h264.h264_attr.rotation = RTS_AV_ROTATION_0;
//				else if( temp == 90 ) config.h264.h264_attr.rotation = RTS_AV_ROTATION_90R;
//				else if( temp == 270 ) config.h264.h264_attr.rotation = RTS_AV_ROTATION_90L;
				else if( temp == 180 ) config.h264.h264_attr.rotation = RTS_AV_ROTATION_180;
				log_info("changed the rotation = %d", config.h264.h264_attr.rotation );
				config_video_set(CONFIG_VIDEO_H264,  &config.osd);
			}
			para_set = 1;
			if( info.task.start == STATUS_WAIT ) goto success_exit;
			else info.status = STATUS_SETUP;
			break;
		case STATUS_START:
			if( stream_start()==0 ) info.status = STATUS_RUN;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_SETUP:
			if( video_init() == 0) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_RESTART:
			server_restart();
			info.status = STATUS_WAIT;
			break;
		case STATUS_ERROR:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_VIDEO_CTRL_EXT, info.task.msg.receiver, -1, 0, 0);
			goto exit;
			break;
	}
	usleep(1000);
	return;
success_exit:
	ret = send_iot_ack(&info.task.msg, &msg, MSG_VIDEO_CTRL_EXT, info.task.msg.receiver, 0,0,0);
exit:
	para_set = 0;
	info.task.func = &task_default;
	info.msg_lock = 0;
	msg_free(&info.task.msg);
	return;
}
/*
 * task control: stop->idle->change->start->run
 */
static void task_control(void)
{
	static int para_set = 0;
	message_t msg;
	int ret;
	switch(info.status){
		case STATUS_RUN:
			if( !para_set ) info.status = STATUS_STOP;
			else goto success_exit;
			break;
		case STATUS_STOP:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_IDLE:
			if( info.task.msg.arg_in.cat == VIDEO_CTRL_QUALITY ) {
				config.profile.quality = *((int*)(info.task.msg.arg));
				log_info("changed the quality = %d", config.profile.quality);
				config_video_set(CONFIG_VIDEO_PROFILE, &config.profile);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_CTRL_MOTION_SWITCH ) {
				config.md.enable = *((int*)(info.task.msg.arg));
				log_info("changed the motion switch = %d", config.md.enable);
				config_video_set(CONFIG_VIDEO_MD, &config.md);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_CTRL_MOTION_ALARM_INTERVAL ) {
				config.md.alarm_interval = *((int*)(info.task.msg.arg));
				log_info("changed the motion detection alarm interval = %d", config.md.alarm_interval);
				config_video_set(CONFIG_VIDEO_MD, &config.md);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_CTRL_MOTION_SENSITIVITY ) {
				config.md.sensitivity = *((int*)(info.task.msg.arg));
				log_info("changed the motion detection sensitivity = %d", config.md.sensitivity);
				config_video_set(CONFIG_VIDEO_MD, &config.md);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_CTRL_MOTION_START ) {
				strcpy( config.md.start, (char*)(info.task.msg.arg) );
				log_info("changed the motion detection start = %s", config.md.start);
				config_video_set(CONFIG_VIDEO_MD, &config.md);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_CTRL_MOTION_END ) {
				strcpy( config.md.end, (char*)(info.task.msg.arg) );
				log_info("changed the motion detection end = %s", config.md.end);
				config_video_set(CONFIG_VIDEO_MD, &config.md);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_CTRL_CUSTOM_WARNING_PUSH ) {
				config.md.cloud_report = *((int*)(info.task.msg.arg));
				log_info("changed the motion detection cloud push = %d", config.md.cloud_report);
				config_video_set(CONFIG_VIDEO_MD, &config.md);
			    /********message body********/
/*	wait for other server
				msg_init(&msg);
				msg.message = MSG_MICLOUD_SET_PARA;
				msg.sender = msg.receiver = SERVER_VIDEO;
				msg.arg_in.cat = MICLOUD_CTRL_WARNING_PUSH_SWITCH;
				msg.arg = info.task.msg.arg;
				msg.arg_size = info.task.msg.arg_size;
				ret = server_micloud_message(&send_msg);
				*/
				/***************************/
			}
			para_set = 1;
			if( info.task.start == STATUS_IDLE ) goto success_exit;
			else info.status = STATUS_START;
			break;
		case STATUS_START:
			if( stream_start()==0 ) info.status = STATUS_RUN;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_VIDEO_CTRL, info.task.msg.receiver, -1, 0, 0);
			if( !ret ) goto exit;
	}
	usleep(1000);
	return;
success_exit:
	ret = send_iot_ack(&info.task.msg, &msg, MSG_VIDEO_CTRL, info.task.msg.receiver, 0,0,0);
exit:
	para_set = 0;
	info.task.func = &task_default;
	info.msg_lock = 0;
	msg_free(&info.task.msg);
	return;
}
/*
 * task start: idle->start
 */
static void task_start(void)
{
	message_t msg;
	int ret;
	switch(info.status){
		case STATUS_RUN:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_VIDEO_START, info.task.msg.receiver, 0, 0, 0);
			goto exit;
			break;
		case STATUS_IDLE:
			info.status = STATUS_START;
			break;
		case STATUS_START:
			if( stream_start()==0 ) info.status = STATUS_RUN;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_VIDEO_START, info.task.msg.receiver, -1 ,0 ,0);
			goto exit;
			break;
	}
	usleep(1000);
	return;
exit:
	info.task.func = &task_default;
	info.msg_lock = 0;
	msg_free(&info.task.msg);
	return;
}
/*
 * task start: run->stop->idle
 */
static void task_stop(void)
{
	message_t msg;
	int ret;
	switch(info.status){
		case STATUS_IDLE:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_VIDEO_STOP, info.task.msg.receiver, 0, 0, 0);
			goto exit;
			break;
		case STATUS_RUN:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			ret = send_iot_ack(&info.task.msg, &msg, MSG_VIDEO_STOP, info.task.msg.receiver, -1,0 ,0);
			break;
	}
	usleep(1000);
	return;
exit:
	info.task.func = &task_default;
	info.msg_lock = 0;
	msg_free(&info.task.msg);
	return;
}
/*
 * default task: none->run
 */
static void task_default(void)
{
	int ret = 0;
	switch( info.status){
		case STATUS_NONE:
			ret = config_video_read(&config);
			if( !ret && misc_full_bit(config.status, CONFIG_VIDEO_MODULE_NUM) ) {
				misc_set_bit(&info.thread_exit, VIDEO_INIT_CONDITION_CONFIG, 1);
			}
			else {
				info.status = STATUS_ERROR;
				break;
			}
			info.status = STATUS_WAIT;
			break;
		case STATUS_WAIT:
			if( misc_full_bit(info.thread_exit, VIDEO_INIT_CONDITION_NUM))
				info.status = STATUS_SETUP;
			else usleep(1000);
			break;
		case STATUS_SETUP:
			if( video_init() == 0) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_IDLE:
//			info.status = STATUS_START;
			break;
		case STATUS_START:
			if( stream_start()==0 ) info.status = STATUS_RUN;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_RUN:
			if(video_main()!=0) info.status = STATUS_STOP;
			break;
		case STATUS_STOP:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			info.task.func = task_error;
			break;
		}
	usleep(1000);
	return;
}

/*
 * server entry point
 */
static void *server_func(void)
{
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
	misc_set_thread_name("server_video");
	pthread_detach(pthread_self());
	//default task
	info.task.func = task_default;
	info.task.start = STATUS_NONE;
	info.task.end = STATUS_RUN;
	while( !info.exit ) {
		info.task.func();
		server_message_proc();
		heart_beat_proc();
	}
	if( info.exit ) {
		while( info.thread_start ) {
		}
	    /********message body********/
		message_t msg;
		msg_init(&msg);
		msg.message = MSG_MANAGER_EXIT_ACK;
		msg.sender = SERVER_VIDEO;
		manager_message(&msg);
		/***************************/
	}
	server_release();
	log_info("-----------thread exit: server_video-----------");
	pthread_exit(0);
}

/*
 * internal interface
 */

/*
 * external interface
 */
int server_video_start(void)
{
	int ret=-1;
	msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	pthread_rwlock_init(&info.lock, NULL);
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_err("video server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_err("video server create successful!");
		return 0;
	}
}

int server_video_message(message_t *msg)
{
	int ret=0,ret1;
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_err("add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	log_info("push into the video message queue: sender=%d, message=%d, ret=%d", msg->sender, msg->message, ret);
	if( ret!=0 )
		log_err("message push in video error =%d", ret);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_err("add message unlock fail, ret = %d\n", ret1);
	return ret;
}
