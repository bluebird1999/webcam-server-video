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
#include <miss.h>
//program header
#include "../../manager/manager_interface.h"
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/miio/miio_interface.h"
#include "../../server/recorder/recorder_interface.h"
#include "../../server/device/device_interface.h"
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
static	video_md_run_t		md_run;

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
static int server_set_status(int type, int st, int value);
static void server_thread_termination(int sign);
//specific
static int write_video_buffer(struct rts_av_buffer *data, int id, int target, int type);
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
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int send_message(int receiver, message_t *msg)
{
	int st = 0;
	switch(receiver) {
		case SERVER_DEVICE:
			st = server_device_message(msg);
			break;
		case SERVER_KERNEL:
	//		st = server_kernel_message(msg);
			break;
		case SERVER_REALTEK:
			st = server_realtek_message(msg);
			break;
		case SERVER_MIIO:
			st = server_miio_message(msg);
			break;
		case SERVER_MISS:
			st = server_miss_message(msg);
			break;
		case SERVER_MICLOUD:
	//		st = server_micloud_message(msg);
			break;
		case SERVER_VIDEO:
			st = server_video_message(msg);
			break;
		case SERVER_AUDIO:
			st = server_audio_message(msg);
			break;
		case SERVER_RECORDER:
			st = server_recorder_message(msg);
			break;
		case SERVER_PLAYER:
			st = server_player_message(msg);
			break;
		case SERVER_SPEAKER:
			st = server_speaker_message(msg);
			break;
		case SERVER_VIDEO2:
			st = server_video2_message(msg);
			break;
		case SERVER_SCANNER:
//			st = server_scanner_message(msg);
			break;
		case SERVER_MANAGER:
			st = manager_message(msg);
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "unknown message target! %d", receiver);
			break;
	}
	return st;
}

static int video_get_property(message_t *msg)
{
	int ret = 0, st;
	int temp;
	message_t send_msg;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_VIDEO;
	send_msg.arg_in.cat = msg->arg_in.cat;
	send_msg.result = 0;
	/****************************/
	st = info.status;
	if( st < STATUS_WAIT ) {
		send_msg.result = -1;
		send_message( msg->receiver, &send_message);
	}
	else {
		if( send_msg.arg_in.cat == VIDEO_PROPERTY_SWITCH) {
			temp = ( st == STATUS_RUN ) ? 1:0;
			send_msg.arg = (void*)(&temp);
			send_msg.arg_size = sizeof(temp);
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_IMAGE_ROLLOVER) {
			if( config.isp.flip == 0 && config.isp.mirror == 0) temp = 0;
			else if( config.isp.flip == 1 && config.isp.mirror == 1) temp = 180;
			else{
				send_msg.result = -1;
			}
			send_msg.arg = (void*)(&temp);
			send_msg.arg_size = sizeof(temp);
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_NIGHT_SHOT) {
			if( config.isp.smart_ir_mode == RTS_ISP_SMART_IR_MODE_AUTO) temp = 0;
			else if( config.isp.smart_ir_mode == RTS_ISP_SMART_IR_MODE_DISABLE) temp = 1;
			else if( config.isp.smart_ir_mode == RTS_ISP_SMART_IR_MODE_LOW_LIGHT_PRIORITY) temp = 2;
			send_msg.arg = (void*)(&temp);
			send_msg.arg_size = sizeof(temp);
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_TIME_WATERMARK) {
			send_msg.arg = (void*)(&config.osd.enable);
			send_msg.arg_size = sizeof(config.osd.enable);
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_MOTION_SWITCH) {
			send_msg.arg = (void*)(&config.md.enable);
			send_msg.arg_size = sizeof(config.md.enable);
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_MOTION_ALARM_INTERVAL) {
			send_msg.arg = (void*)(&config.md.alarm_interval);
			send_msg.arg_size = sizeof(config.md.alarm_interval);
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_MOTION_SENSITIVITY) {
			send_msg.arg = (void*)(&config.md.sensitivity);
			send_msg.arg_size = sizeof(config.md.sensitivity);
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_MOTION_START) {
			send_msg.arg = (void*)(config.md.start);
			send_msg.arg_size = strlen(config.md.start) + 1;
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_MOTION_END) {
			send_msg.arg = (void*)(config.md.end);
			send_msg.arg_size = strlen(config.md.end) + 1;
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_CUSTOM_WARNING_PUSH) {
			send_msg.arg = (void*)(&config.md.cloud_report);
			send_msg.arg_size = sizeof(config.md.cloud_report);
		}
		else if( send_msg.arg_in.cat == VIDEO_PROPERTY_CUSTOM_DISTORTION) {
			send_msg.arg = (void*)(&config.isp.ldc);
			send_msg.arg_size = sizeof(config.isp.ldc);
		}
		ret = send_message( msg->receiver, &send_msg);
	}
	return ret;
}

static int video_set_property(message_t *msg)
{
	int ret=0, mode = -1;
	message_t send_msg;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_VIDEO;
	send_msg.arg_in.cat = msg->arg_in.cat;
	/****************************/
/*	if( msg->arg_in.cat == VIDEO_PROPERTY_IMAGE_ROLLOVER ) {
		int temp = *((int*)(msg->arg));
		if( temp == 0 && (config.isp.flip!=0 || config.isp.mirror!=0) ) {
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_FLIP, 0);
			ret |= video_isp_set_attr(RTS_VIDEO_CTRL_ID_MIRROR, 0);
			if(!ret) {
				config.isp.flip = 0;
				config.isp.mirror = 0;
				log_qcy(DEBUG_SERIOUS, "changed the isp flip = %d", config.isp.flip);
				log_qcy(DEBUG_SERIOUS, "changed the isp mirror = %d", config.isp.mirror);
				video_config_video_set(CONFIG_VIDEO_ISP, &config.isp);
			}
		}
		else if( temp == 180 && (config.isp.flip!=1 || config.isp.mirror!=1) ) {
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_FLIP, 1);
			ret |= video_isp_set_attr(RTS_VIDEO_CTRL_ID_MIRROR, 1);
			if(!ret) {
				config.isp.flip = 1;
				config.isp.mirror = 1;
				log_qcy(DEBUG_SERIOUS, "changed the isp flip = %d", config.isp.flip);
				log_qcy(DEBUG_SERIOUS, "changed the isp mirror = %d", config.isp.mirror);
				video_config_video_set(CONFIG_VIDEO_ISP, &config.isp);
			}
		}
	}else
*/
	if( msg->arg_in.cat == VIDEO_PROPERTY_NIGHT_SHOT ) {
		int temp = *((int*)(msg->arg));
		if( temp == 0) {	//automode
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_SMART_IR_MODE, RTS_ISP_SMART_IR_MODE_AUTO);
			if(!ret) {
				config.isp.smart_ir_mode = RTS_ISP_SMART_IR_MODE_AUTO;
			}
			mode = DAY_NIGHT_AUTO;
		}
		else if( temp == 1) {//close
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_SMART_IR_MODE, RTS_ISP_SMART_IR_MODE_DISABLE);
			if(!ret) {
				config.isp.smart_ir_mode = RTS_ISP_SMART_IR_MODE_DISABLE;
			}
			mode = DAY_NIGHT_OFF;
		}
		else if( temp == 2) {//open
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_SMART_IR_MODE, RTS_ISP_SMART_IR_MODE_LOW_LIGHT_PRIORITY);
			if(!ret) {
				config.isp.smart_ir_mode = RTS_ISP_SMART_IR_MODE_LOW_LIGHT_PRIORITY;
			}
			mode = DAY_NIGHT_ON;
		}
		if(!ret) {
			log_qcy(DEBUG_INFO, "changed the smart night mode = %d", config.isp.smart_ir_mode);
			video_config_video_set(CONFIG_VIDEO_ISP, &config.isp);
		    /********message body********/
			send_msg.arg_in.cat = DEVICE_CTRL_DAY_NIGHT_MODE;
			send_msg.arg = (void *)&mode;
			send_msg.arg_size = sizeof(mode);
			/***************************/
		}
	}
	else if( msg->arg_in.cat == VIDEO_PROPERTY_CUSTOM_DISTORTION ) {
		int temp = *((int*)(msg->arg));
		if( temp != config.isp.ldc ) {
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_LDC, temp );
			if(!ret) {
				config.isp.ldc = temp;
				log_qcy(DEBUG_INFO, "changed the lens distortion correction = %d", config.isp.ldc);
				video_config_video_set(CONFIG_VIDEO_ISP, &config.isp);
			}
		}
	}
	/***************************/
	send_msg.result = ret;
	ret = send_message(msg->receiver, &send_msg);
	/***************************/
	return ret;
}

static void video_mjpeg_func(void *priv, struct rts_av_profile *profile, struct rts_av_buffer *buffer)
{
    static unsigned long index;
    char filename[32];
    FILE *pfile = NULL;
    snprintf(filename, 32, "%s%s%d%s", config.jpg.image_path, "snap_", index++, ".jpg");
    pfile = fopen(filename, "wb");
    if (!pfile) {
		log_qcy(DEBUG_SERIOUS, "open %s fail\n", filename);
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
		log_qcy(DEBUG_SERIOUS, "set mjpeg callback fail, ret = %d\n", ret);
		return ret;
	}
	return ret;
}


static int *video_md_func(void *arg)
{
	video_md_config_t ctrl;
	int st;
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    misc_set_thread_name("server_video_md");
    pthread_detach(pthread_self());
    //init
    memcpy( &ctrl, (video_md_config_t*)arg, sizeof(video_md_config_t) );
    video_md_init( &ctrl, config.profile.profile[config.profile.quality].video.width,
    		config.profile.profile[config.profile.quality].video.height);
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_MD, 1 );
    while( 1 ) {
    	st = info.status;
    	if( info.exit ) break;
 /*   	if( st != STATUS_START && st != STATUS_RUN )
    		break;
*/
    	if( !md_run.started ) break;
    	else if( st == STATUS_START )
    		continue;
    	usleep(10);
   		video_md_proc();
    }
    //release
    log_qcy(DEBUG_INFO, "-----------thread exit: server_video_md-----------");
    video_md_release();
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_MD, 0 );
    pthread_exit(0);
}

static int *video_3acontrol_func(void *arg)
{
	video_3actrl_config_t ctrl;
	server_status_t st;
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    misc_set_thread_name("server_video_3a_control");
    pthread_detach(pthread_self());
    //init
    memcpy( &ctrl, (video_3actrl_config_t*)arg, sizeof(video_3actrl_config_t));
    video_white_balance_init( &ctrl.awb_para);
    video_exposure_init(&ctrl.ae_para);
    video_focus_init(&ctrl.af_para);
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_3ACTRL, 1 );
    while( 1 ) {
    	st = info.status;
    	if( info.exit ) break;
    	if( st != STATUS_START && st != STATUS_RUN )
    		break;
    	else if( st == STATUS_START )
    		continue;
    	video_white_balance_proc( &ctrl.awb_para,stream.frame);
    	video_exposure_proc(&ctrl.ae_para,stream.frame);
    	video_focus_proc(&ctrl.af_para,stream.frame);
    }
    //release
    log_qcy(DEBUG_INFO, "-----------thread exit: server_video_3a_control-----------");
    video_white_balance_release();
    video_exposure_release();
    video_focus_release();
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_3ACTRL, 0 );
    pthread_exit(0);
}

static int *video_osd_func(void *arg)
{
	int ret=0, st;
	video_osd_config_t ctrl;
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    misc_set_thread_name("server_video_osd");
    pthread_detach(pthread_self());
    //init
    memcpy( &ctrl,(video_osd_config_t*)arg, sizeof(video_osd_config_t));
    ret = video_osd_init(&ctrl, stream.osd, config.profile.profile[config.profile.quality].video.width,
    		config.profile.profile[config.profile.quality].video.height);
    if( ret != 0) {
    	log_qcy(DEBUG_SERIOUS, "osd init error!");
    	goto exit;
    }
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_OSD, 1 );
    while( 1 ) {
    	if( info.exit ) break;
    	st = info.status;
    	if( st != STATUS_START && st != STATUS_RUN )
    		break;
    	else if( st == STATUS_START )
    		continue;
    	video_osd_proc(&ctrl,stream.frame);
    }
    //release
exit:
    log_qcy(DEBUG_INFO, "-----------thread exit: server_video_osd-----------");
    video_osd_release();
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_OSD, 0 );
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
		log_qcy(DEBUG_SERIOUS, "set isp profile fail, ret = %d", ret);
		return -1;
	}
	if( stream.isp != -1 ) {
		ret = rts_av_enable_chn(stream.isp);
		if (ret) {
			log_qcy(DEBUG_SERIOUS, "enable isp fail, ret = %d", ret);
			return -1;
		}
	}
	else {
		return -1;
	}
	if( stream.h264 != -1 ) {
		ret = rts_av_enable_chn(stream.h264);
		if (ret) {
			log_qcy(DEBUG_SERIOUS, "enable h264 fail, ret = %d", ret);
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
				log_qcy(DEBUG_SERIOUS, "enable osd fail, ret = %d", ret);
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
				log_qcy(DEBUG_SERIOUS, "enable jpg fail, ret = %d", ret);
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
    	log_qcy(DEBUG_SERIOUS, "start recv h264 fail, ret = %d", ret);
    	return -1;
    }
/*    //start the 3a control thread
	ret = pthread_create(&isp_3a_id, NULL, video_3acontrol_func, (void*)&config.a3ctrl);
	if(ret != 0) {
		log_qcy(DEBUG_SERIOUS, "3a control thread create error! ret = %d",ret);
		return -1;
	 }
	else {
		log_qcy(DEBUG_SERIOUS, "3a control thread create successful!");
	}
*/
	if( config.osd.enable && stream.osd != -1 ) {
		//start the osd thread
		ret = pthread_create(&osd_id, NULL, video_osd_func, (void*)&config.osd);
		if(ret != 0) {
			log_qcy(DEBUG_SERIOUS, "osd thread create error! ret = %d",ret);
		 }
		else {
			log_qcy(DEBUG_INFO, "osd thread create successful!");
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

static int md_init_scheduler(void)
{
	int ret = 0;
	ret = video_md_get_scheduler_time(config.md.end, &md_run.scheduler, &md_run.mode);
	return ret;
}
static int md_check_scheduler(void)
{
	int ret;
	message_t msg;
	pthread_t md_id;
	if( config.md.enable ) {
		ret = video_md_check_scheduler_time(&md_run.scheduler, &md_run.mode);
		if( ret==1 ) {
			if( !md_run.started ) {
				//start the md thread
				ret = pthread_create(&md_id, NULL, video_md_func, (void*)&config.md);
				if(ret != 0) {
					log_qcy(DEBUG_SERIOUS, "md thread create error! ret = %d",ret);
					return -1;
				}
				else {
					log_qcy(DEBUG_INFO, "md thread create successful!");
					md_run.started = 1;
				    /********message body********/
					msg_init(&msg);
					msg.message = MSG_VIDEO_START;
					msg.sender = msg.receiver = SERVER_VIDEO;
				    server_video_message(&msg);
					/****************************/
				}
			}
		}
		else {
			if( md_run.started ) {
				goto stop_md;
			}
		}
	}
	else {
		if( md_run.started ) {
			goto stop_md;
		}
	}
	return ret;
stop_md:
	md_run.started = 0;
	/********message body********/
	msg_init(&msg);
	msg.message = MSG_VIDEO_STOP;
	msg.sender = msg.receiver = SERVER_VIDEO;
	server_video_message(&msg);
	/****************************/
	return ret;
}

static int video_init(void)
{
	int ret;
	stream_init();
	stream.isp = rts_av_create_isp_chn(&config.isp.isp_attr);
	if (stream.isp < 0) {
		log_qcy(DEBUG_SERIOUS, "fail to create isp chn, ret = %d", stream.isp);
		return -1;
	}
	log_qcy(DEBUG_INFO, "isp chnno:%d", stream.isp);
	stream.h264 = rts_av_create_h264_chn(&config.h264.h264_attr);
	if (stream.h264 < 0) {
		log_qcy(DEBUG_SERIOUS, "fail to create h264 chn, ret = %d", stream.h264);
		return -1;
	}
	log_qcy(DEBUG_INFO, "h264 chnno:%d", stream.h264);
	config.profile.profile[config.profile.quality].fmt = RTS_V_FMT_YUV420SEMIPLANAR;
	ret = rts_av_set_profile(stream.isp, &config.profile.profile[config.profile.quality]);
	if (ret) {
		log_qcy(DEBUG_SERIOUS, "set isp profile fail, ret = %d", ret);
		return -1;
	}
	if( config.osd.enable ) {
        stream.osd = rts_av_create_osd_chn();
        if (stream.osd < 0) {
        	log_qcy(DEBUG_SERIOUS, "fail to create osd chn, ret = %d\n", stream.osd);
        	return -1;
        }
        log_qcy(DEBUG_INFO, "osd chnno:%d", stream.osd);
        ret = rts_av_bind(stream.isp, stream.osd);
    	if (ret) {
    		log_qcy(DEBUG_SERIOUS, "fail to bind isp and osd, ret %d", ret);
    		return -1;
    	}
    	ret = rts_av_bind(stream.osd, stream.h264);
    	if (ret) {
    		log_qcy(DEBUG_SERIOUS, "fail to bind osd and h264, ret %d", ret);
    		return -1;
    	}
	}
	else {
    	ret = rts_av_bind(stream.isp, stream.h264);
    	if (ret) {
    		log_qcy(DEBUG_SERIOUS, "fail to bind isp and h264, ret %d", ret);
    		return -1;
    	}
	}
	if(config.jpg.enable) {
        stream.jpg = rts_av_create_mjpeg_chn(&config.jpg.jpg_ctrl);
        if (stream.jpg < 0) {
                log_qcy(DEBUG_SERIOUS, "fail to create jpg chn, ret = %d\n", stream.jpg);
                return -1;
        }
        log_qcy(DEBUG_INFO, "jpg chnno:%d", stream.jpg);
    	ret = rts_av_bind(stream.isp, stream.jpg);
    	if (ret) {
    		log_qcy(DEBUG_SERIOUS, "fail to bind isp and jpg, ret %d", ret);
    		return -1;
    	}
	}
	md_init_scheduler();
	video_isp_init(&config.isp);
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
			if( write_video_buffer(buffer, MSG_MISS_VIDEO_DATA, SERVER_MISS, 0) != 0 )
				log_qcy(DEBUG_WARNING, "Miss ring buffer push failed!");
		}
		if( misc_get_bit(info.status2, RUN_MODE_SAVE) ) {
			if( write_video_buffer(buffer, MSG_RECORDER_VIDEO_DATA, SERVER_RECORDER, RECORDER_TYPE_NORMAL) != 0 )
				log_qcy(DEBUG_WARNING, "Recorder ring buffer push failed!");
		}
		if( misc_get_bit(info.status2, RUN_MODE_MOTION_DETECT) ) {
			if( write_video_buffer(buffer, MSG_RECORDER_VIDEO_DATA, SERVER_RECORDER, RECORDER_TYPE_MOTION_DETECTION) != 0 )
				log_qcy(DEBUG_WARNING, "Recorder ring buffer push failed!");
		}
		if( misc_get_bit(info.status2, RUN_MODE_SEND_MICLOUD) ) {
/*	wait for other server
 * 			if( write_video_buffer(buffer, MSG_MICLOUD_VIDEO_DATA, SERVER_MICLOUD, 0) != 0 )
				log_qcy(DEBUG_SERIOUS, "Micloud ring buffer push failed!");
*/
		}
		stream.frame++;
		rts_av_put_buffer(buffer);
	}
    return ret;
}

static int write_video_buffer(struct rts_av_buffer *data, int id, int target, int type)
{
	int ret=0;
	message_t msg;
	av_data_info_t	info;
    /********message body********/
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.arg_in.cat = type;
	msg.message = id;
	msg.extra = data->vm_addr;
	msg.extra_size = data->bytesused;
	info.flag = data->flags;
	info.frame_index = data->frame_idx;
	info.timestamp = data->timestamp / 1000;
	info.fps = config.profile.profile[config.profile.quality].video.denominator;
	info.width = config.profile.profile[config.profile.quality].video.width;
	info.height = config.profile.profile[config.profile.quality].video.height;
   	info.flag |= FLAG_STREAM_TYPE_LIVE << 11;
   	if(config.osd.enable)
   		info.flag |= FLAG_WATERMARK_TIMESTAMP_EXIST << 13;
   	else
   		info.flag |= FLAG_WATERMARK_TIMESTAMP_NOT_EXIST << 13;
    if( misc_get_bit(data->flags, 0/*RTSTREAM_PKT_FLAG_KEY*/) )// I frame
    	info.flag |= FLAG_FRAME_TYPE_IFRAME << 0;
    else
    	info.flag |= FLAG_FRAME_TYPE_PBFRAME << 0;
    if( config.profile.quality==0 )
        info.flag |= FLAG_RESOLUTION_VIDEO_360P << 17;
    else if( config.profile.quality==1 )
        info.flag |= FLAG_RESOLUTION_VIDEO_480P << 17;
    else if( config.profile.quality==2 )
        info.flag |= FLAG_RESOLUTION_VIDEO_1080P << 17;
	msg.arg = &info;
	msg.arg_size = sizeof(av_data_info_t);
	if( target == SERVER_MISS )
		ret = server_miss_video_message(&msg);
//	else if( target == SERVER_MICLOUD )
//		ret = server_micloud_video_message(&msg);
	else if( target == SERVER_RECORDER )
		ret = server_recorder_video_message(&msg);
	/****************************/
	return ret;
}

static int server_set_status(int type, int st, int value)
{
	int ret=-1;
	ret = pthread_rwlock_wrlock(&info.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add lock fail, ret = %d", ret);
		return ret;
	}
	if(type == STATUS_TYPE_STATUS)
		info.status = st;
	else if(type==STATUS_TYPE_EXIT)
		info.exit = st;
	else if(type==STATUS_TYPE_CONFIG)
		config.status = st;
	else if(type==STATUS_TYPE_THREAD_START)
		misc_set_bit(&info.thread_start, st, value);
	ret = pthread_rwlock_unlock(&info.lock);
	if (ret)
		log_qcy(DEBUG_SERIOUS, "add unlock fail, ret = %d", ret);
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
	memset(&md_run,0,sizeof(video_md_run_t));
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0, ret1 = 0;
	message_t msg, send_msg;
	msg_init(&msg);
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	if( info.msg_lock ) {
		ret1 = pthread_rwlock_unlock(&message.lock);
		return 0;
	}
	ret = msg_buffer_pop(&message, &msg);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1) {
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	}
	if( ret == -1) {
		msg_free(&msg);
		return -1;
	}
	else if( ret == 1)
		return 0;

    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg.arg_pass),sizeof(message_arg_t));
	send_msg.message = msg.message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_VIDEO;
	send_msg.result = 0;
	/***************************/
	switch(msg.message) {
		case MSG_VIDEO_START:
			if( (msg.sender == SERVER_MISS)
					|| (msg.sender == SERVER_MIIO) )
				misc_set_bit(&info.status2, RUN_MODE_SEND_MISS, 1);
			if( msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_SEND_MICLOUD, 1);
			if( msg.sender == SERVER_RECORDER) {
				if( msg.arg_in.cat == RECORDER_TYPE_NORMAL)
					misc_set_bit(&info.status2, RUN_MODE_SAVE, 1);
				else if( msg.arg_in.cat == RECORDER_TYPE_MOTION_DETECTION )
					misc_set_bit(&info.status2, RUN_MODE_MOTION_DETECT, 1);
			}
			if( msg.sender == SERVER_VIDEO) misc_set_bit(&info.status2, RUN_MODE_MOTION, 1);
			if( info.status == STATUS_RUN ) {
				ret = send_message(msg.receiver, &send_msg);
				break;
			}
			info.task.func = task_start;
			info.task.start = info.status;
			memcpy(&info.task.msg, &msg,sizeof(message_t));
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_STOP:
			if( (msg.sender == SERVER_MISS)
					|| (msg.sender == SERVER_MIIO) )
				misc_set_bit(&info.status2, RUN_MODE_SEND_MISS, 0);
			if( msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_SEND_MICLOUD, 0);
			if( msg.sender == SERVER_RECORDER) {
				if( msg.arg_in.cat == RECORDER_TYPE_NORMAL)
					misc_set_bit(&info.status2, RUN_MODE_SAVE, 0);
				else if( msg.arg_in.cat == RECORDER_TYPE_MOTION_DETECTION )
					misc_set_bit(&info.status2, RUN_MODE_MOTION_DETECT, 0);
			}
			if( msg.sender == SERVER_VIDEO) misc_set_bit(&info.status2, RUN_MODE_MOTION, 0);
			if( info.status != STATUS_RUN ) {
				ret = send_message(msg.receiver, &send_msg);
				break;
			}
			if( info.status2 > 0 ) {
				ret = send_message(msg.receiver, &send_msg);
				break;
			}
			info.task.func = task_stop;
			info.task.start = info.status;
			msg_deep_copy(&info.task.msg, &msg);
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_PROPERTY_SET:
			if(msg.arg_in.cat == VIDEO_PROPERTY_QUALITY) {
				int temp = *((int*)(msg.arg));
				if( temp == config.profile.quality) {
					ret = send_message(msg.receiver, &send_msg);
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_PROPERTY_MOTION_SWITCH) {
				int temp = *((int*)(msg.arg));
				if( temp == config.md.enable ) {
					ret = send_message(msg.receiver, &send_msg);
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_PROPERTY_MOTION_ALARM_INTERVAL) {
				int temp = *((int*)(msg.arg));
				if( temp == config.md.alarm_interval ) {
					ret = send_message(msg.receiver, &send_msg);
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_PROPERTY_MOTION_SENSITIVITY) {
				int temp = *((int*)(msg.arg));
				if( temp == config.md.sensitivity ) {
					ret = send_message(msg.receiver, &send_msg);
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_PROPERTY_MOTION_START) {
				char *temp = (char*)(msg.arg);
				if( !strcmp( temp, config.md.start) ) {
					ret = send_message(msg.receiver, &send_msg);
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_PROPERTY_MOTION_END) {
				char *temp = (char*)(msg.arg);
				if( !strcmp( temp, config.md.end) ) {
					ret = send_message(msg.receiver, &send_msg);
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_PROPERTY_CUSTOM_WARNING_PUSH) {
				int temp = (char*)(msg.arg);
				if( temp == config.md.cloud_report ) {
					ret = send_message(msg.receiver, &send_msg);
					break;
				}
			}
			info.task.func = task_control;
			info.task.start = info.status;
			msg_deep_copy(&info.task.msg, &msg);
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_PROPERTY_SET_EXT:
			if( msg.arg_in.cat == VIDEO_PROPERTY_TIME_WATERMARK ) {
				int temp = *((int*)(msg.arg));
				if( temp  == config.osd.enable) {
					ret = send_message(msg.receiver, &send_msg);
					break;
				}
			}
			else if( msg.arg_in.cat == VIDEO_PROPERTY_IMAGE_ROLLOVER ) {
				int temp = *((int*)(msg.arg));
				if( ( temp == 0 && config.isp.flip==0 && config.isp.mirror==0 ) ||
					( temp == 180 && config.isp.flip==1 && config.isp.mirror==1)  ) {
					ret = send_message(msg.receiver, &send_msg);
					break;
				}
			}
			info.task.func = task_control_ext;
			info.task.start = info.status;
			msg_deep_copy(&info.task.msg, &msg);
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_PROPERTY_SET_DIRECT:
			video_set_property(&msg);
			break;
		case MSG_VIDEO_PROPERTY_GET:
			ret = video_get_property(&msg);
			break;
		case MSG_MANAGER_EXIT:
			info.exit = 1;
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_MIIO_PROPERTY_NOTIFY:
		case MSG_MIIO_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == MIIO_PROPERTY_TIME_SYNC ) {
				if( msg.arg_in.dog == 1 )
					misc_set_bit( &info.thread_exit, VIDEO_INIT_CONDITION_MIIO_TIME, 1);
			}
			break;
		case MSG_REALTEK_PROPERTY_NOTIFY:
		case MSG_REALTEK_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == REALTEK_PROPERTY_AV_STATUS ) {
				if( msg.arg_in.dog == 1 )
					misc_set_bit(&info.thread_exit, VIDEO_INIT_CONDITION_REALTEK, 1);
			}
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "not processed message = %x", msg.message);
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
	if( (tick - info.tick) > SERVER_HEARTBEAT_INTERVAL ) {
		info.tick = tick;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_HEARTBEAT;
		msg.sender = msg.receiver = SERVER_VIDEO;
		msg.arg_in.cat = info.status;
		msg.arg_in.dog = info.thread_start;
		msg.arg_in.duck = info.thread_exit;
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
			log_qcy(DEBUG_SERIOUS, "!!!!!!!!error in video, restart in 5 s!");
			info.tick3 = time_get_now_stamp();
			info.status = STATUS_NONE;
			break;
		case STATUS_NONE:
			tick = time_get_now_stamp();
			if( (tick - info.tick3) > SERVER_RESTART_PAUSE ) {
				info.exit = 1;
				info.tick3 = tick;
			}
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_error = %d", info.status);
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
	int ret = 0;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.result = 0;
	/***************************/
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
			if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_TIME_WATERMARK ) {
				config.osd.enable = *((int*)(info.task.msg.arg));
				log_qcy(DEBUG_INFO, "changed the osd switch = %d", config.osd.enable);
				video_config_video_set(CONFIG_VIDEO_OSD,  &config.osd);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_IMAGE_ROLLOVER ) {
				int temp = *((int*)(info.task.msg.arg));
				if( temp == 0 )  {
					config.isp.flip = 0;
					config.isp.mirror = 0;
				}
				else if( temp == 180 ) {
					config.isp.flip = 1;
					config.isp.mirror = 1;
				}
				log_qcy(DEBUG_INFO, "changed the image rollover, flip = %d, mirror = %d ", config.isp.flip, config.isp.mirror );
				video_config_video_set(CONFIG_VIDEO_ISP,  &config.isp);
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
			msg.result = -1;
			ret = send_message(info.task.msg.receiver, &msg);
			goto exit;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_control_exit = %d", info.status);
			break;
	}
	usleep(1000);
	return;
success_exit:
	ret = send_message(info.task.msg.receiver, &msg);
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
	int ret = 0;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.result = 0;
	/***************************/
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
			if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_QUALITY ) {
				config.profile.quality = *((int*)(info.task.msg.arg));
				log_qcy(DEBUG_INFO, "changed the quality = %d", config.profile.quality);
				video_config_video_set(CONFIG_VIDEO_PROFILE, &config.profile);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_MOTION_SWITCH ) {
				config.md.enable = *((int*)(info.task.msg.arg));
				log_qcy(DEBUG_INFO, "changed the motion switch = %d", config.md.enable);
				video_config_video_set(CONFIG_VIDEO_MD, &config.md);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_MOTION_ALARM_INTERVAL ) {
				config.md.alarm_interval = *((int*)(info.task.msg.arg));
				log_qcy(DEBUG_INFO, "changed the motion detection alarm interval = %d", config.md.alarm_interval);
				video_config_video_set(CONFIG_VIDEO_MD, &config.md);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_MOTION_SENSITIVITY ) {
				config.md.sensitivity = *((int*)(info.task.msg.arg));
				log_qcy(DEBUG_INFO, "changed the motion detection sensitivity = %d", config.md.sensitivity);
				video_config_video_set(CONFIG_VIDEO_MD, &config.md);
			}
			else if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_MOTION_START ) {
				strcpy( config.md.start, (char*)(info.task.msg.arg) );
				log_qcy(DEBUG_INFO, "changed the motion detection start = %s", config.md.start);
				video_config_video_set(CONFIG_VIDEO_MD, &config.md);
				md_init_scheduler();
			}
			else if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_MOTION_END ) {
				strcpy( config.md.end, (char*)(info.task.msg.arg) );
				log_qcy(DEBUG_INFO, "changed the motion detection end = %s", config.md.end);
				video_config_video_set(CONFIG_VIDEO_MD, &config.md);
				md_init_scheduler();
			}
			else if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_CUSTOM_WARNING_PUSH ) {
				config.md.cloud_report = *((int*)(info.task.msg.arg));
				log_qcy(DEBUG_INFO, "changed the motion detection cloud push = %d", config.md.cloud_report);
				video_config_video_set(CONFIG_VIDEO_MD, &config.md);
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
			msg.result = -1;
			ret = send_message( info.task.msg.receiver, &msg);
			if( !ret ) goto exit;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_control = %d", info.status);
			break;
	}
	usleep(1000);
	return;
success_exit:
	ret = send_message( info.task.msg.receiver, &msg);
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
	int ret = 0;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.result = 0;
	/***************************/
	switch(info.status){
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_RUN:
			ret = send_message(info.task.msg.receiver, &msg);
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
			msg.result = -1;
			ret = send_message(info.task.msg.receiver, &msg);
			goto exit;
			break;
		case STATUS_SETUP:
			if( video_init() == 0) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_start = %d", info.status);
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
	int ret = 0;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.result = 0;
	/***************************/
	switch(info.status){
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_IDLE:
			ret = send_message(info.task.msg.receiver, &msg);
			goto exit;
			break;
		case STATUS_RUN:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			msg.result = -1;
			ret = send_message(info.task.msg.receiver, &msg);
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_stop = %d", info.status);
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
	message_t msg;
	switch( info.status ){
		case STATUS_NONE:
			if( !misc_get_bit( info.thread_exit, VIDEO_INIT_CONDITION_CONFIG ) ) {
				ret = video_config_video_read(&config);
				if( !ret && misc_full_bit( config.status, CONFIG_VIDEO_MODULE_NUM) ) {
					misc_set_bit(&info.thread_exit, VIDEO_INIT_CONDITION_CONFIG, 1);
				}
				else {
					info.status = STATUS_ERROR;
					break;
				}
			}
			if( !misc_get_bit( info.thread_exit, VIDEO_INIT_CONDITION_REALTEK )
					&& ((time_get_now_stamp() - info.tick2 ) > MESSAGE_RESENT) ) {
					info.tick2 = time_get_now_stamp();
			    /********message body********/
				msg_init(&msg);
				msg.message = MSG_REALTEK_PROPERTY_GET;
				msg.sender = msg.receiver = SERVER_VIDEO;
				msg.arg_in.cat = REALTEK_PROPERTY_AV_STATUS;
				server_realtek_message(&msg);
				/****************************/
			}
			if( !misc_get_bit( info.thread_exit, VIDEO_INIT_CONDITION_MIIO_TIME )
					&& ((time_get_now_stamp() - info.tick2 ) > MESSAGE_RESENT) ) {
					info.tick2 = time_get_now_stamp();
			    /********message body********/
				msg_init(&msg);
				msg.message = MSG_MIIO_PROPERTY_GET;
				msg.sender = msg.receiver = SERVER_VIDEO;
				msg.arg_in.cat = MIIO_PROPERTY_TIME_SYNC;
				server_miio_message(&msg);
				/****************************/
			}
			if( misc_full_bit( info.thread_exit, VIDEO_INIT_CONDITION_NUM ) )
				info.status = STATUS_WAIT;
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			if( video_init() == 0) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_IDLE:
			md_check_scheduler();
			break;
		case STATUS_START:
			if( stream_start()==0 ) info.status = STATUS_RUN;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_RUN:
			if(video_main()!=0) info.status = STATUS_STOP;
			md_check_scheduler();
			break;
		case STATUS_STOP:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			info.task.func = task_error;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
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
	if( !message.init ) {
		msg_buffer_init(&message, MSG_BUFFER_OVERFLOW_NO);
	}
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
	log_qcy(DEBUG_INFO, "-----------thread exit: server_video-----------");
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
	ret = pthread_create(&info.id, NULL, server_func, NULL);
	if(ret != 0) {
		log_qcy(DEBUG_SERIOUS, "video server create error! ret = %d",ret);
		 return ret;
	 }
	else {
		log_qcy(DEBUG_INFO, "video server create successful!");
		return 0;
	}
}

int server_video_message(message_t *msg)
{
	int ret=0,ret1;
	if( !message.init ) {
		log_qcy(DEBUG_SERIOUS, "video server is not ready for message processing!");
		return -1;
	}
	ret = pthread_rwlock_wrlock(&message.lock);
	if(ret)	{
		log_qcy(DEBUG_SERIOUS, "add message lock fail, ret = %d\n", ret);
		return ret;
	}
	ret = msg_buffer_push(&message, msg);
	log_qcy(DEBUG_VERBOSE, "push into the video message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg->sender, msg->message, ret,
			message.head, message.tail);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in video error =%d", ret);
	ret1 = pthread_rwlock_unlock(&message.lock);
	if (ret1)
		log_qcy(DEBUG_SERIOUS, "add message unlock fail, ret = %d\n", ret1);
	return ret;
}
