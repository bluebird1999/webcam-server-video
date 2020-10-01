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
//program header
#include "../../manager/manager_interface.h"
#include "../../server/realtek/realtek_interface.h"
#include "../../tools/tools_interface.h"
#include "../../server/config/config_video_interface.h"
#include "../../server/miss/miss_interface.h"
#include "../../server/config/config_interface.h"
//server header
#include "video.h"
#include "video_interface.h"
#include "video.h"
#include "white_balance.h"
#include "focus.h"
#include "exposure.h"

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
//specific
static int write_miss_avbuffer(struct rts_av_buffer *data);
static void video_mjpeg_func(void *priv, struct rts_av_profile *profile, struct rts_av_buffer *buffer);
static int video_snapshot(void);
static int *video_3acontrol_func(void *arg);
static int *video_osd_func(void *arg);
static int stream_init(void);
static int stream_destroy(void);
static int stream_start(void);
static int stream_stop(void);
static int video_init(void);
static int video_main(void);
static int send_miio_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg,int size);
static int send_config_save(message_t *msg, int module, void *arg, int size);
static int video_get_miio_config(video_miio_config_t *tmp);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
static int send_config_save(message_t *msg, int module, void *arg, int size)
{
	int ret=0;
	/********message body********/
	msg_init(msg);
	msg->message = MSG_CONFIG_WRITE;
	msg->sender = msg->receiver = SERVER_VIDEO;
	msg->arg_in.cat = module;
	msg->arg = arg;
	msg->arg_size = size;
	ret = server_config_message(msg);
	/********message body********/
	return ret;
}

static int send_miio_ack(message_t *org_msg, message_t *msg, int id, int receiver, int result, void *arg, int size)
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
	case SERVER_CONFIG:
		st = server_config_message(msg);
		break;
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
}

static int video_get_miio_config(video_miio_config_t *tmp)
{
	int ret = 0;
	memset(tmp,0,sizeof(video_miio_config_t));
	if( info.status <= STATUS_WAIT ) return -1;
	tmp->on = (info.status == STATUS_RUN) ? 1:0;
	tmp->image_roll = config.isp.flip;
	tmp->night = config.isp.smart_ir_mode;
	tmp->watermark = config.osd.enable;
	tmp->wdr = config.isp.wdr_mode;
	tmp->glimmer = 0;
	tmp->recording = misc_get_bit(config.profile.run_mode,RUN_MODE_SAVE);
	tmp->motion = 0;
	tmp->motion_switch = 0;
	tmp->motion_alarm = 0;
	tmp->motion_sensitivity = 0;
	tmp->motion_start = 0;
	tmp->motion_end = 0;
	tmp->custom_distortion = config.isp.ldc;
	return ret;
}

static int video_process_direct_ctrl(message_t *msg)
{
	int ret=0;
	message_t send_msg;
	if( (msg->arg_in.cat == VIDEO_CTRL_WDR_MODE) &&  (msg->arg_in.dog != config.isp.wdr_mode) ) {
		ret = isp_set_attr(RTS_VIDEO_CTRL_ID_WDR_MODE, msg->arg_in.dog);
		if(!ret) {
			config.isp.wdr_mode = msg->arg_in.cat;
			log_info("changed the wdr = %d", config.isp.wdr_mode);
			send_config_save(&send_msg, CONFIG_VIDEO_ISP, &config.isp, sizeof(video_isp_config_t));
		}
	}
	else if( (msg->arg_in.cat == VIDEO_CTRL_IMAGE_ROLLOVER) ) {
		if( (msg->arg_in.dog == 0) && config.isp.flip != 0 ) {
			ret = isp_set_attr(RTS_VIDEO_CTRL_ID_FLIP, 0);
			if(!ret) {
				config.isp.flip = 0;
				log_info("changed the image rollover = 0");
				send_config_save(&send_msg, CONFIG_VIDEO_ISP, &config.isp, sizeof(video_isp_config_t));
			}
		}
		else if( msg->arg_in.dog == 180 && config.isp.flip != 1 ) {
			ret = isp_set_attr(RTS_VIDEO_CTRL_ID_FLIP, 1);
			if(!ret) {
				config.isp.flip = 1;
				log_info("changed the image rollover = 1");
				send_config_save(&send_msg, CONFIG_VIDEO_ISP, &config.isp, sizeof(video_isp_config_t));
			}
		}
	}
	else if( (msg->arg_in.cat == VIDEO_CTRL_NIGHT_SHOT) && (msg->arg_in.dog != config.isp.ir_mode) ) {
		ret = isp_set_attr(RTS_VIDEO_CTRL_ID_IR_MODE, msg->arg_in.dog);
		if(!ret) {
			config.isp.ir_mode = msg->arg_in.cat;
			log_info("changed the night mode = %d", config.isp.ir_mode);
			send_config_save(&send_msg, CONFIG_VIDEO_ISP, &config.isp, sizeof(video_isp_config_t));
		}
	}
	ret = send_miio_ack(msg, &send_msg, MSG_VIDEO_CTRL_DIRECT, msg->receiver, ret, 0, 0);
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

static int *video_3acontrol_func(void *arg)
{
	video_3actrl_config_t *ctrl=(video_3actrl_config_t*)arg;
    misc_set_thread_name("server_video_3a_control");
    pthread_detach(pthread_self());
    //init
    white_balance_init(&ctrl->awb_para);
    exposure_init(&ctrl->ae_para);
    focus_init(&ctrl->af_para);
    while( !info.exit ) {
    	if( info.status != STATUS_START && info.status != STATUS_RUN )
    		break;
    	else if( info.status == STATUS_START )
    		continue;

    	white_balance_proc(&ctrl->awb_para,stream.frame);
    	exposure_proc(&ctrl->ae_para,stream.frame);
    	focus_proc(&ctrl->af_para,stream.frame);
    }
    //release
    log_info("-----------thread exit: server_video_3a_control-----------");
    white_balance_release();
    exposure_release();
    focus_release();
    misc_set_bit(&info.thread_exit, THREAD_3ACTRL, 1);
    pthread_exit(0);
}

static int *video_osd_func(void *arg)
{
	int ret=0;
	video_osd_config_t *ctrl=(video_osd_config_t*)arg;
    server_status_t st;
    misc_set_thread_name("server_video_osd");
    pthread_detach(pthread_self());
    //init
    ret = osd_init(ctrl, stream.osd);
    if( ret != 0) {
    	log_err("osd init error!");
    	goto exit;
    }
    while( !info.exit ) {
    	st = info.status;
    	if( st != STATUS_START && st != STATUS_RUN )
    		break;
    	else if( st == STATUS_START )
    		continue;
    	osd_proc(ctrl,stream.frame);
    }
    //release
exit:
    log_info("-----------thread exit: server_video_osd-----------");
    osd_release();
    misc_set_bit(&info.thread_exit, THREAD_OSD, 1);
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
	pthread_t isp_3a_id, osd_id;
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
		misc_set_bit(&info.thread_start,THREAD_3ACTRL,1);
	}
	if( config.osd.enable && stream.osd != -1 ) {
		//start the osd thread
		ret = pthread_create(&osd_id, NULL, video_osd_func, (void*)&config.osd);
		if(ret != 0) {
			log_err("osd thread create error! ret = %d",ret);
		 }
		else {
			log_info("osd thread create successful!");
			misc_set_bit(&info.thread_start,THREAD_OSD,1);
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
		if( misc_get_bit(config.profile.run_mode, RUN_MODE_SEND_MISS) ) {
			if( write_miss_avbuffer(buffer)!=0 )
				log_err("Miss ring buffer push failed!");
		}
		if( misc_get_bit(config.profile.run_mode, RUN_MODE_SAVE) ) {
//			if( write_avbuffer(&recorder_buffer, buffer)!=0 )
//				log_err("Recorder ring buffer push failed!");
		}
		stream.frame++;
		rts_av_put_buffer(buffer);
	}
    return ret;
}

static int write_miss_avbuffer(struct rts_av_buffer *data)
{
	int ret=0;
	message_t msg;
	av_data_info_t	info;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_VIDEO_BUFFER_MISS;
	msg.extra = data->vm_addr;
	msg.extra_size = data->bytesused;
	info.flag = data->flags;
	info.frame_index = data->frame_idx;
	info.index = data->index;
	info.timestamp = data->timestamp;
	msg.arg = &info;
	msg.arg_size = sizeof(av_data_info_t);
	server_miss_video_message(&msg);
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
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0, ret1 = 0;
	message_t msg,send_msg;
	video_miio_config_t tmp;
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
			if( info.status != STATUS_IDLE) {
				ret = send_miio_ack(&msg, &send_msg, MSG_VIDEO_START, msg.receiver, 0, 0, 0);
				break;
			}
			info.task.func = task_start;
			info.task.start = info.status;
			memcpy(&info.task.msg, &msg,sizeof(message_t));
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_STOP:
			if( info.status != STATUS_RUN ) {
				ret = send_miio_ack(&msg, &send_msg, MSG_VIDEO_STOP, msg.receiver, 0, 0, 0 );
				break;
			}
			info.task.func = task_stop;
			info.task.start = info.status;
			memcpy(&info.task.msg, &msg,sizeof(message_t));
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_CTRL:
			if(msg.arg_in.cat == VIDEO_CTRL_QUALITY) {
				if( msg.arg_in.dog == config.profile.quality) {
					ret = send_miio_ack(&msg, &send_msg, MSG_VIDEO_CTRL, msg.receiver, 0, 0, 0 );
				}
				else {
					info.task.func = task_control;
					info.task.start = info.status;
					memcpy(&info.task.msg, &msg,sizeof(message_t));
					info.msg_lock = 1;
				}
			}
			break;
		case MSG_VIDEO_CTRL_EXT:
			if( msg.arg_in.cat == VIDEO_CTRL_TIME_WATERMARK ) {
				if( msg.arg_in.dog == config.osd.enable) {
					ret = send_miio_ack(&msg, &send_msg, MSG_VIDEO_CTRL_EXT, msg.receiver, 0, 0, 0 );
				}
				else {
					info.task.func = task_control_ext;
					info.task.start = info.status;
					memcpy(&info.task.msg, &msg,sizeof(message_t));
					info.msg_lock = 1;
				}
			}
			break;
		case MSG_VIDEO_CTRL_DIRECT:
			video_process_direct_ctrl(&msg);
			break;
		case MSG_VIDEO_GET_PARA:
			ret = video_get_miio_config(&tmp);
			send_miio_ack(&msg, &send_msg, MSG_VIDEO_GET_PARA, msg.receiver, ret,
					&tmp, sizeof(video_miio_config_t));
			break;
		case MSG_MANAGER_EXIT:
			info.exit = 1;
			break;
		case MSG_CONFIG_READ_ACK:
			if( msg.result==0 )
				memcpy( (video_config_t*)(&config), (video_config_t*)msg.arg, msg.arg_size);
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
	}
	msg_free(&msg);
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
			log_err("!!!!!!!!error in video, restart in 10 s!");
			info.tick = get_nowtime_ms();
			info.status = STATUS_NONE;
			break;
		case STATUS_NONE:
			tick = get_nowtime_ms();
			if( (tick - info.tick) > 5000 ) {
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
			else {
				if( info.task.msg.arg_in.cat == VIDEO_CTRL_TIME_WATERMARK )
					goto success_exit;
			}
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
				config.osd.enable = info.task.msg.arg_in.dog;
				log_info("changed the osd switch = %d", config.osd.enable);
				send_config_save(&msg, CONFIG_VIDEO_OSD,  &config.osd, sizeof(video_osd_config_t));
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
			ret = send_miio_ack(&info.task.msg, &msg, MSG_VIDEO_CTRL_EXT, info.task.msg.receiver, -1, 0, 0);
			goto exit;
			break;
	}
	usleep(1000);
	return;
success_exit:
	ret = send_miio_ack(&info.task.msg, &msg, MSG_VIDEO_CTRL_EXT, info.task.msg.receiver, 0,
			&(config.osd.enable),sizeof(int));
exit:
	para_set = 0;
	info.task.func = &task_default;
	info.msg_lock = 0;
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
			else {
				if( info.task.msg.arg_in.cat == VIDEO_CTRL_QUALITY) {
					ret = send_miio_ack(&info.task.msg, &msg, MSG_VIDEO_CTRL, info.task.msg.receiver, 0
							,&(config.profile.quality),sizeof(int));
					goto exit;
				}
			}
			break;
		case STATUS_STOP:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_IDLE:
			if( info.task.msg.arg_in.cat == VIDEO_CTRL_QUALITY) {
				config.profile.quality = info.task.msg.arg_in.dog;
				log_info("changed the quality = %d", config.profile.quality);
				send_config_save(&msg, CONFIG_VIDEO_PROFILE, &config.profile, sizeof(video_profile_config_t));
			}
			para_set = 1;
			info.status = STATUS_START;
			break;
		case STATUS_START:
			if( stream_start()==0 ) info.status = STATUS_RUN;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			ret = send_miio_ack(&info.task.msg, &msg, MSG_VIDEO_CTRL, info.task.msg.receiver, -1, 0, 0);
			if( !ret ) goto exit;
	}
	usleep(1000);
	return;
exit:
	para_set = 0;
	info.task.func = &task_default;
	info.msg_lock = 0;
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
			ret = send_miio_ack(&info.task.msg, &msg, MSG_VIDEO_START, info.task.msg.receiver, 0, 0, 0);
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
			ret = send_miio_ack(&info.task.msg, &msg, MSG_VIDEO_START, info.task.msg.receiver, -1 ,0 ,0);
			goto exit;
			break;
	}
	usleep(1000);
	return;
exit:
	info.task.func = &task_default;
	info.msg_lock = 0;
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
			ret = send_miio_ack(&info.task.msg, &msg, MSG_VIDEO_STOP, info.task.msg.receiver, 0, 0, 0);
			goto exit;
			break;
		case STATUS_RUN:
			if( stream_stop()==0 ) info.status = STATUS_IDLE;
			else info.status = STATUS_ERROR;
			break;
		case STATUS_ERROR:
			ret = send_miio_ack(&info.task.msg, &msg, MSG_VIDEO_STOP, info.task.msg.receiver, -1,0 ,0);
			break;
	}
	usleep(1000);
	return;
exit:
	info.task.func = &task_default;
	info.msg_lock = 0;
	return;
}
/*
 * default task: none->run
 */
static void task_default(void)
{
	message_t msg;
	int ret = 0;
	switch( info.status){
		case STATUS_NONE:
		    /********message body********/
			msg_init(&msg);
			msg.message = MSG_CONFIG_READ;
			msg.sender = msg.receiver = SERVER_VIDEO;
			ret = server_config_message(&msg);
			/***************************/
			if( !ret ) info.status = STATUS_WAIT;
			else sleep(1);
			break;
		case STATUS_WAIT:
			if( config.status == ( (1<<CONFIG_VIDEO_MODULE_NUM) -1 ) )
				info.status = STATUS_SETUP;
			else usleep(1000);
			break;
		case STATUS_SETUP:
			if( video_init() == 0) info.status = STATUS_IDLE;
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
	}
	server_release();
	if( info.exit ) {
		while( info.thread_exit != info.thread_start ) {
		}
	    /********message body********/
		message_t msg;
		msg_init(&msg);
		msg.message = MSG_MANAGER_EXIT_ACK;
		msg.sender = SERVER_VIDEO;
		manager_message(&msg);
		/***************************/
	}
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
