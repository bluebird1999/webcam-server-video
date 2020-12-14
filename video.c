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
#include "config.h"

/*
 * static
 */
//variable
static 	message_buffer_t	message;
static 	server_info_t 		info;
static	video_stream_t		stream={-1,-1,-1,-1};
static	video_config_t		config;
static 	av_buffer_t			vbuffer;
static  pthread_rwlock_t	ilock = PTHREAD_RWLOCK_INITIALIZER;
static	pthread_rwlock_t	vlock = PTHREAD_RWLOCK_INITIALIZER;
static	pthread_mutex_t		mutex = PTHREAD_MUTEX_INITIALIZER;
static	pthread_cond_t		cond = PTHREAD_COND_INITIALIZER;
static 	miss_session_t		*session[MAX_SESSION_NUMBER];

//function
//common
static void *server_func(void);
static int server_message_proc(void);
static void server_release_1(void);
static void server_release_2(void);
static void server_release_3(void);
static int server_restart(void);
static void task_default(void);
static void task_start(void);
static void task_stop(void);
static void task_control(void);
static void task_control_ext(void);
static void task_exit(void);
static int server_set_status(int type, int st, int value);
static void server_thread_termination(int sign);
//specific
static int write_video_buffer(av_packet_t *data, int id, int target, int channel);
static void write_video_info(struct rts_av_buffer *data, av_data_info_t	*info);
static int *video_3acontrol_func(void *arg);
static int *video_osd_func(void *arg);
static int stream_init(void);
static int stream_destroy(void);
static int stream_start(void);
static int stream_stop(void);
static int video_init(void);
/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */

/*
 * helper
 */
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
		if( config.isp.ir_mode == VIDEO_ISP_IR_AUTO) temp = 0;
		else if( config.isp.ir_mode == RTS_ISP_IR_DAY) temp = 1;
		else if( config.isp.ir_mode == RTS_ISP_IR_NIGHT) temp = 2;
		send_msg.arg = (void*)(&temp);
		send_msg.arg_size = sizeof(temp);
	}
	else if( send_msg.arg_in.cat == VIDEO_PROPERTY_TIME_WATERMARK) {
		send_msg.arg = (void*)(&config.osd.enable);
		send_msg.arg_size = sizeof(config.osd.enable);
	}
	else if( send_msg.arg_in.cat == VIDEO_PROPERTY_CUSTOM_DISTORTION) {
		send_msg.arg = (void*)(&config.isp.ldc);
		send_msg.arg_size = sizeof(config.isp.ldc);
	}
	ret = manager_common_send_message( msg->receiver, &send_msg);
	return ret;
}

static int video_set_property(message_t *msg)
{
	int ret= 0, mode = -1;
	message_t send_msg;
	int temp;
    /********message body********/
	msg_init(&send_msg);
	memcpy(&(send_msg.arg_pass), &(msg->arg_pass),sizeof(message_arg_t));
	send_msg.message = msg->message | 0x1000;
	send_msg.sender = send_msg.receiver = SERVER_VIDEO;
	send_msg.arg_in.cat = msg->arg_in.cat;
	send_msg.arg_in.wolf = 0;
	/****************************/
	if( msg->arg_in.cat == VIDEO_PROPERTY_NIGHT_SHOT ) {
		int temp = *((int*)(msg->arg));
		if( temp == 0) {	//automode
			config.isp.ir_mode = VIDEO_ISP_IR_AUTO;
			mode = DAY_NIGHT_AUTO;
		}
		else if( temp == 1) {//close
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_IR_MODE, RTS_ISP_IR_DAY);
			if(!ret) {
				config.isp.ir_mode = RTS_ISP_IR_DAY;
			}
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_GRAY_MODE, RTS_ISP_IR_DAY);
			if(!ret) {
				config.isp.ir_mode = RTS_ISP_IR_DAY;
			}
			mode = DAY_NIGHT_OFF;
		}
		else if( temp == 2) {//open
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_IR_MODE, RTS_ISP_IR_NIGHT);
			if(!ret) {
				config.isp.ir_mode = RTS_ISP_IR_NIGHT;
			}
			ret = video_isp_set_attr(RTS_VIDEO_CTRL_ID_GRAY_MODE, RTS_ISP_IR_NIGHT);
			if(!ret) {
				config.isp.ir_mode = RTS_ISP_IR_NIGHT;
			}
			mode = DAY_NIGHT_ON;
		}
		if(!ret) {
			log_qcy(DEBUG_INFO, "changed the smart night mode = %d", config.isp.ir_mode);
			video_config_video_set(CONFIG_VIDEO_ISP, &config.isp);
			send_msg.arg_in.wolf = 1;
			send_msg.arg = &temp;
			send_msg.arg_size = sizeof(temp);
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
				send_msg.arg_in.wolf = 1;
				send_msg.arg = &temp;
				send_msg.arg_size = sizeof(temp);
			}
		}
	}
	else if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_TIME_WATERMARK ) {
		int temp = *((int*)(info.task.msg.arg));
		if( temp != config.osd.enable ) {
			config.osd.enable = temp;
			log_qcy(DEBUG_INFO, "changed the osd switch = %d", config.osd.enable);
			video_config_video_set(CONFIG_VIDEO_OSD,  &config.osd);
			send_msg.arg_in.wolf = 1;
			send_msg.arg = &temp;
			send_msg.arg_size = sizeof(temp);
		}
	}
	/***************************/
	send_msg.result = ret;
	ret = manager_common_send_message(msg->receiver, &send_msg);
	/***************************/
	return ret;
}

static int video_qos_check(av_qos_t *qos)
{
	double ratio = 0.0;
	int ret = REALTEK_OQS_NORMAL;
	if( (qos->buffer_overrun + qos->buffer_success) < AV_BUFFER_MIN_SAMPLE)
		return ret;
	qos->buffer_total = qos->buffer_overrun + qos->buffer_success;
	qos->buffer_ratio = (double) ((double)qos->buffer_success / (double)qos->buffer_total );
	if( qos->buffer_ratio > AV_BUFFER_SUCCESS) {
		ret = REALTEK_QOS_UPGRADE;
	}
	else if( qos->buffer_ratio < REALTEK_QOS_DOWNGRADE ) {
		ret = REALTEK_QOS_DOWNGRADE;
	}
	if( qos->buffer_total > AV_BUFFER_MAX_SAMPLE) {
		qos->buffer_overrun = 0;
		qos->buffer_success = 0;
		qos->buffer_total = 0;
	}
	return ret;
}

static int video_quality_downgrade(av_qos_t *qos)
{
	message_t msg;
	int ret = 0;
	int vq = -1;
	if( config.profile.quality >= 1 )
		vq = config.profile.quality - 1;
	else
		return 0;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_VIDEO_PROPERTY_SET;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.arg_in.cat = VIDEO_PROPERTY_QUALITY;
	msg.arg = &vq;
	msg.arg_size = sizeof(vq);
	ret = server_video_message(&msg);
	/****************************/
	log_qcy(DEBUG_INFO, "-----------------sample number = %d, success ratio = %f, will downgrade!", qos->buffer_total, qos->buffer_ratio);
	log_qcy(DEBUG_INFO, "------------------downdgrade current video qulity to %d", vq);
	return ret;
}

static int video_quality_upgrade(av_qos_t *qos)
{
	message_t msg;
	int ret = 0;
	int vq = -1;
	if( config.profile.quality <= 1 )
		vq = config.profile.quality + 1;
	else
		return 0;
    /********message body********/
	msg_init(&msg);
	msg.message = MSG_VIDEO_PROPERTY_SET;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.arg_in.cat = VIDEO_PROPERTY_QUALITY;
	msg.arg = &vq;
	msg.arg_size = sizeof(vq);
	ret = server_video_message(&msg);
	/****************************/
	log_qcy(DEBUG_INFO, "-----------------sample number = %d, success ratio = %f, will upgrade!", qos->buffer_total, qos->buffer_ratio);
	log_qcy(DEBUG_INFO, "------------------upgrade current video qulity to %d", vq);
	return ret;
}

static int video_quit_send(int server, int channel)
{
	int ret = 0;
	message_t msg;
	msg_init(&msg);
	msg.sender = msg.receiver = server;
	msg.arg_in.wolf = channel;
	msg.message = MSG_VIDEO_STOP;
	manager_common_send_message(SERVER_VIDEO, &msg);
	return ret;
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
    manager_common_send_dummy(SERVER_VIDEO);
    while( 1 ) {
    	st = info.status;
    	if( info.exit ) break;
    	if( st != STATUS_START && st != STATUS_RUN )
    		break;
    	else if( st == STATUS_START )
    		continue;
    	if( misc_get_bit(info.thread_exit, THREAD_3ACTRL) ) break;
    	video_white_balance_proc( &ctrl.awb_para,stream.frame);
    	video_exposure_proc(&ctrl.ae_para,stream.frame);
    	video_focus_proc(&ctrl.af_para,stream.frame);
    	usleep(1000);
    }
    //release
    video_white_balance_release();
    video_exposure_release();
    video_focus_release();
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_3ACTRL, 0 );
    manager_common_send_dummy(SERVER_VIDEO);
    log_qcy(DEBUG_INFO, "-----------thread exit: server_video_3a_control-----------");
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
    manager_common_send_dummy(SERVER_VIDEO);
    while( 1 ) {
    	if( info.exit ) break;
    	if( misc_get_bit(info.thread_exit, THREAD_OSD) ) break;
    	st = info.status;
    	if( (st != STATUS_START) && (st != STATUS_RUN) )
    		break;
    	else if( st == STATUS_START )
    		continue;
   		ret = video_osd_proc(&ctrl);
    	usleep(1000*100);
    }
    //release
exit:
    video_osd_release();
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_OSD, 0 );
    manager_common_send_dummy(SERVER_VIDEO);
    log_qcy(DEBUG_INFO, "-----------thread exit: server_video_osd-----------");
    pthread_exit(0);
}

static int *video_main_func(void* arg)
{
	int ret=0, st, i;
	video_stream_t ctrl;
	av_packet_t	*packet = NULL;
	av_qos_t qos;
	struct rts_av_buffer *buffer = NULL;
    signal(SIGINT, server_thread_termination);
    signal(SIGTERM, server_thread_termination);
    misc_set_thread_name("server_video_main");
    pthread_detach(pthread_self());
    //init
    memset( &qos, 0, sizeof(qos));
    memcpy( &ctrl,(video_stream_t*)arg, sizeof(video_stream_t));
    av_buffer_init(&vbuffer, &vlock);
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_VIDEO, 1 );
    manager_common_send_dummy(SERVER_VIDEO);
    while( 1 ) {
    	if( info.exit ) break;
    	st = info.status;
    	if( st != STATUS_START && st != STATUS_RUN )
    		break;
    	else if( st == STATUS_START )
    		continue;
    	usleep(1000);
    	ret = rts_av_poll(ctrl.h264);
    	if ( ret )
    		continue;
    	ret = rts_av_recv(ctrl.h264, &buffer);
    	if ( ret )
    		continue;
    	if ( buffer ) {
        	packet = av_buffer_get_empty(&vbuffer, &qos.buffer_overrun, &qos.buffer_success);
    		packet->data = malloc( buffer->bytesused );
    		if( packet->data == NULL) {
    			log_qcy(DEBUG_WARNING, "allocate memory failed in video buffer, size=%d", buffer->bytesused);
    			rts_av_put_buffer(buffer);
    			continue;
    		}
    		memcpy(packet->data, buffer->vm_addr, buffer->bytesused);
    		if( (stream.realtek_stamp == 0) && (stream.unix_stamp == 0) ) {
    			stream.realtek_stamp = buffer->timestamp;
    			stream.unix_stamp = time_get_now_stamp();
    		}
    		write_video_info( buffer, &packet->info);
    		rts_av_put_buffer(buffer);
    		for(i=0;i<MAX_SESSION_NUMBER;i++) {
				if( misc_get_bit(info.status2, RUN_MODE_MISS+i) ) {
					ret = write_video_buffer(packet, MSG_MISS_VIDEO_DATA, SERVER_MISS, i);
					if( (ret == MISS_LOCAL_ERR_MISS_GONE) || (ret == MISS_LOCAL_ERR_SESSION_GONE) ) {
						log_qcy(DEBUG_WARNING, "Miss video ring buffer send failed due to non-existing miss server or session");
						video_quit_send(SERVER_MISS, i);
						log_qcy(DEBUG_WARNING, "----shut down video miss stream due to session lost!------");
					}
					else if( ret == MISS_LOCAL_ERR_AV_NOT_RUN) {
						qos.failed_send[RUN_MODE_MISS+i]++;
						if( qos.failed_send[RUN_MODE_MISS+i] > VIDEO_MAX_FAILED_SEND) {
							qos.failed_send[RUN_MODE_MISS+i] = 0;
							video_quit_send(SERVER_MISS, i);
							log_qcy(DEBUG_WARNING, "----shut down video miss stream due to long overrun!------");
						}
					}
					else if( ret == 0) {
						av_packet_add(packet);
						qos.failed_send[RUN_MODE_MISS+i] = 0;
					}
				}
    		}
/*
    		if( video_qos_check(&qos) == REALTEK_QOS_UPGRADE )
    			video_quality_upgrade(&qos);
    		else if( video_qos_check(&qos) == REALTEK_QOS_DOWNGRADE )
    			video_quality_downgrade(&qos);
*/
    	}
    }
    //release
exit:
	av_buffer_release(&vbuffer);
    server_set_status(STATUS_TYPE_THREAD_START, THREAD_VIDEO, 0 );
    manager_common_send_dummy(SERVER_VIDEO);
    log_qcy(DEBUG_INFO, "-----------thread exit: server_video_main-----------");
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
	pthread_t isp_3a_id, osd_id, md_id, main_id;
	config.profile.profile[config.profile.quality].fmt = RTS_V_FMT_YUV420SEMIPLANAR;
	ret = rts_av_set_profile(stream.isp, &config.profile.profile[config.profile.quality]);
	info.tick = 0;
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
		misc_set_bit(&info.tick, THREAD_3ACTRL, 1);
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
			misc_set_bit(&info.tick, THREAD_OSD, 1);
			log_qcy(DEBUG_INFO, "osd thread create successful!");
		}
	}
	ret = pthread_create(&main_id, NULL, video_main_func, (void*)&stream);
	if(ret != 0) {
		log_qcy(DEBUG_INFO, "video main thread create error! ret = %d",ret);
		return -1;
	 }
	else {
		misc_set_bit(&info.tick, THREAD_VIDEO, 1);
		log_qcy(DEBUG_SERIOUS, "video main thread create successful!");
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
	if( stream.osd != -1 )
		ret = rts_av_disable_chn(stream.osd);
	if(stream.h264!=-1)
		ret = rts_av_disable_chn(stream.h264);
	if(stream.isp!=-1)
		ret = rts_av_disable_chn(stream.isp);
	stream.frame = 0;
	stream.realtek_stamp = 0;
	stream.unix_stamp = 0;
	return ret;
}

static int video_init(void)
{
	int ret;
	struct rts_video_mjpeg_ctrl *mjpeg_ctrl = NULL;
	pthread_t osd_id;
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
	video_isp_init(&config.isp);
	return 0;
}

static void write_video_info(struct rts_av_buffer *data, av_data_info_t	*info)
{
	info->flag = data->flags;
	info->frame_index = data->frame_idx;
//	info->timestamp = data->timestamp / 1000;
	info->timestamp = ( ( data->timestamp - stream.realtek_stamp ) / 1000) + stream.unix_stamp * 1000;
	info->fps = config.profile.profile[config.profile.quality].video.denominator;
	info->width = config.profile.profile[config.profile.quality].video.width;
	info->height = config.profile.profile[config.profile.quality].video.height;
   	info->flag |= FLAG_STREAM_TYPE_LIVE << 11;
   	if(config.osd.enable)
   		info->flag |= FLAG_WATERMARK_TIMESTAMP_EXIST << 13;
   	else
   		info->flag |= FLAG_WATERMARK_TIMESTAMP_NOT_EXIST << 13;
    if( misc_get_bit(data->flags, 0/*RTSTREAM_PKT_FLAG_KEY*/) )// I frame
    	info->flag |= FLAG_FRAME_TYPE_IFRAME << 0;
    else
    	info->flag |= FLAG_FRAME_TYPE_PBFRAME << 0;
    if( config.profile.quality==0 )
        info->flag |= FLAG_RESOLUTION_VIDEO_360P << 17;
    else if( config.profile.quality==1 )
        info->flag |= FLAG_RESOLUTION_VIDEO_480P << 17;
    else if( config.profile.quality==2 )
        info->flag |= FLAG_RESOLUTION_VIDEO_1080P << 17;
    info->size = data->bytesused;
}

static int write_video_buffer(av_packet_t *data, int id, int target, int channel)
{
	int ret=0;
	message_t msg;
    /********message body********/
	msg_init(&msg);
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.arg_in.wolf = channel;
	msg.arg_in.handler = session[channel];
	msg.message = id;
	msg.arg = data;
	msg.arg_size = 0;
	if( target == SERVER_MISS )
		ret = server_miss_video_message(&msg);
//	else if( target == SERVER_MICLOUD )
//		ret = server_micloud_video_message(&msg);
	else if( target == SERVER_RECORDER )
		ret = server_recorder_video_message(&msg);
	/****************************/
	return ret;
}

static int video_add_session(miss_session_t *ses, int sid)
{
	session[sid] = ses;
	return 0;
}

static int video_remove_session(miss_session_t *ses, int sid)
{
	session[sid] = NULL;
	return 0;
}

static int server_set_status(int type, int st, int value)
{
	int ret=-1;
	pthread_rwlock_wrlock(&ilock);
	if(type == STATUS_TYPE_STATUS)
		info.status = st;
	else if(type==STATUS_TYPE_EXIT)
		info.exit = st;
	else if(type==STATUS_TYPE_CONFIG)
		config.status = st;
	else if(type==STATUS_TYPE_THREAD_START)
		misc_set_bit(&info.thread_start, st, value);
	pthread_rwlock_unlock(&ilock);
	return ret;
}

static void server_thread_termination(int sign)
{
    /********message body********/
	message_t msg;
	msg_init(&msg);
	msg.message = MSG_VIDEO_SIGINT;
	msg.sender = msg.receiver = SERVER_VIDEO;
	manager_common_send_message(SERVER_MANAGER, &msg);
	/****************************/
}

static void video_broadcast_thread_exit(void)
{
}

static void server_release_1(void)
{
	message_t msg;
	stream_stop();
	stream_destroy();
	usleep(1000*10);
}

static void server_release_2(void)
{
	msg_buffer_release2(&message, &mutex);
	memset(&config,0,sizeof(video_config_t));
	memset(&stream,0,sizeof(video_stream_t));
	video_osd_font_release();
}

static void server_release_3(void)
{
	memset(&info, 0, sizeof(server_info_t));
}

/*
 *
 */
static int video_message_filter(message_t  *msg)
{
	int ret = 0;
	if( info.task.func == task_exit) { //only system message
		if( !msg_is_system(msg->message) && !msg_is_response(msg->message) )
			return 1;
	}
	return ret;
}

static int server_message_proc(void)
{
	int ret = 0, ret1 = 0;
	message_t msg;
//condition
	pthread_mutex_lock(&mutex);
	if( message.head == message.tail ) {
		if( info.status == info.old_status	)
			pthread_cond_wait(&cond,&mutex);
	}
	if( info.msg_lock ) {
		pthread_mutex_unlock(&mutex);
		return 0;
	}
	msg_init(&msg);
	ret = msg_buffer_pop(&message, &msg);
	pthread_mutex_unlock(&mutex);
	if( ret == 1)
		return 0;
	if( video_message_filter(&msg) ) {
		msg_free(&msg);
		log_qcy(DEBUG_VERBOSE, "VIDEO message--- sender=%d, message=%x, ret=%d, head=%d, tail=%d was screened, the current task is %p", msg.sender, msg.message,
				ret, message.head, message.tail, info.task.func);
		return -1;
	}
	log_qcy(DEBUG_VERBOSE, "-----pop out from the VIDEO message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg.sender, msg.message,
			ret, message.head, message.tail);
	msg_init(&info.task.msg);
	msg_deep_copy(&info.task.msg, &msg);
	switch(msg.message) {
		case MSG_VIDEO_START:
			info.task.func = task_start;
			info.task.start = info.status;
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_STOP:
			info.task.msg.arg_in.cat = info.status2;
			if( msg.sender == SERVER_MISS) misc_set_bit(&info.task.msg.arg_in.cat, (RUN_MODE_MISS + msg.arg_in.wolf), 0);
			if( msg.sender == SERVER_MICLOUD) misc_set_bit(&info.task.msg.arg_in.cat, RUN_MODE_MICLOUD, 0);
			if( msg.sender == SERVER_RECORDER) misc_set_bit(&info.task.msg.arg_in.cat, (RUN_MODE_SAVE + msg.arg_in.wolf), 0);
			if( msg.sender == SERVER_VIDEO) misc_set_bit(&info.task.msg.arg_in.cat, RUN_MODE_MOTION, 0);
			info.task.func = task_stop;
			info.task.start = info.status;
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_PROPERTY_SET:
			info.task.func = task_control;
			info.task.start = info.status;
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_PROPERTY_SET_EXT:
			info.task.func = task_control_ext;
			info.task.start = info.status;
			info.msg_lock = 1;
			break;
		case MSG_VIDEO_PROPERTY_SET_DIRECT:
			video_set_property(&msg);
			break;
		case MSG_VIDEO_PROPERTY_GET:
			ret = video_get_property(&msg);
			break;
		case MSG_MANAGER_EXIT:
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		case MSG_MANAGER_TIMER_ACK:
			((HANDLER)msg.arg_in.handler)();
			break;
		case MSG_MIIO_PROPERTY_NOTIFY:
		case MSG_MIIO_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == MIIO_PROPERTY_TIME_SYNC ) {
				if( msg.arg_in.dog == 1 )
					misc_set_bit( &info.init_status, VIDEO_INIT_CONDITION_MIIO_TIME, 1);
			}
			break;
		case MSG_REALTEK_PROPERTY_NOTIFY:
		case MSG_REALTEK_PROPERTY_GET_ACK:
			if( msg.arg_in.cat == REALTEK_PROPERTY_AV_STATUS ) {
				if( msg.arg_in.dog == 1 )
					misc_set_bit(&info.init_status, VIDEO_INIT_CONDITION_REALTEK, 1);
			}
			break;
		case MSG_MANAGER_EXIT_ACK:
			misc_set_bit(&info.error, msg.sender, 0);
			break;
		case MSG_MANAGER_DUMMY:
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "not processed message = %x", msg.message);
			break;
	}
	msg_free(&msg);
	return ret;
}

/*
 *
 */
static int server_none(void)
{
	int ret = 0;
	message_t msg;
	if( !misc_get_bit( info.init_status, VIDEO_INIT_CONDITION_CONFIG ) ) {
		ret = video_config_video_read(&config);
		if( !ret && misc_full_bit( config.status, CONFIG_VIDEO_MODULE_NUM) ) {
			misc_set_bit(&info.init_status, VIDEO_INIT_CONDITION_CONFIG, 1);
		}
		else {
			info.status = STATUS_ERROR;
			return -1;
		}
	}
	if( !misc_get_bit( info.init_status, VIDEO_INIT_CONDITION_REALTEK ) ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_REALTEK_PROPERTY_GET;
		msg.sender = msg.receiver = SERVER_VIDEO;
		msg.arg_in.cat = REALTEK_PROPERTY_AV_STATUS;
		manager_common_send_message(SERVER_REALTEK,    &msg);
		/****************************/
		usleep(MESSAGE_RESENT_SLEEP);
	}
	if( !misc_get_bit( info.init_status, VIDEO_INIT_CONDITION_MIIO_TIME)) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MIIO_PROPERTY_GET;
		msg.sender = msg.receiver = SERVER_VIDEO;
		msg.arg_in.cat = MIIO_PROPERTY_TIME_SYNC;
		ret = manager_common_send_message(SERVER_MIIO, &msg);
		/***************************/
		usleep(MESSAGE_RESENT_SLEEP);
	}
	if( misc_full_bit( info.init_status, VIDEO_INIT_CONDITION_NUM ) ) {
		info.status = STATUS_WAIT;
		video_osd_font_init(&config.osd);
	}
	return ret;
}

static int server_setup(void)
{
	int ret = 0;
	message_t msg;
	if( video_init() == 0) {
		info.status = STATUS_IDLE;
	}
	else
		info.status = STATUS_ERROR;
	return ret;
}

static int server_start(void)
{
	int ret = 0;
	if( stream_start()==0 )
		info.status = STATUS_RUN;
	else
		info.status = STATUS_ERROR;
	return ret;
}

static int server_stop(void)
{
	int ret = 0;
	if( stream_stop()==0 )
		info.status = STATUS_IDLE;
	else
		info.status = STATUS_ERROR;
	return ret;
}

static int server_restart(void)
{
	int ret = 0;
	stream_stop();
	stream_destroy();
	return ret;
}
/*
 * task
 */
/*
 * task control: restart->wait->change->setup->start->run
 */
static void task_control_ext(void)
{
	static int para_set = 0;
	int temp = 0;
	message_t msg;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.result = 0;
	msg.arg_in.wolf = 0;
	/***************************/
	switch(info.status){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			if( info.thread_start == 0 ) {
				if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_TIME_WATERMARK ) {
					int temp = *((int*)(info.task.msg.arg));
					if( temp != config.osd.enable) {
						config.osd.enable = temp;
						log_qcy(DEBUG_INFO, "changed the osd switch = %d", config.osd.enable);
						video_config_video_set(CONFIG_VIDEO_OSD,  &config.osd);
						msg.arg_in.wolf = 1;
						msg.arg = &temp;
						msg.arg_size = sizeof(temp);
					}
				}
				else if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_IMAGE_ROLLOVER ) {
					int temp = *((int*)(info.task.msg.arg));
					if( ( (temp==0) && ( (config.isp.flip!=0) || (config.isp.mirror!=0) ) ) ||
							( (temp==180) && ( (config.isp.flip!=1) || (config.isp.mirror!=1) ) ) ) {
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
						msg.arg_in.wolf = 1;
						msg.arg = &temp;
						msg.arg_size = sizeof(temp);
					}
				}
				para_set = 1;
				if( info.task.start == STATUS_WAIT )
					goto exit;
				else
					info.status = STATUS_SETUP;
			}
			break;
		case STATUS_SETUP:
			server_setup();
			break;
		case STATUS_IDLE:
			if( !para_set )
				info.status = STATUS_RESTART;
			else {
				if( info.task.start == STATUS_IDLE )
					goto exit;
				else
					info.status = STATUS_START;
			}
			break;
		case STATUS_START:
			server_start();
		case STATUS_RUN:
			if( !para_set )
				info.status = STATUS_RESTART;
			else {
				if( misc_get_bit( info.thread_start, THREAD_VIDEO ) )
					goto exit;
			}
			break;
		case STATUS_RESTART:
			server_restart();
			info.status = STATUS_WAIT;
			break;
		case STATUS_ERROR:
			msg.result = -1;
			goto exit;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_control_exit = %d", info.status);
			break;
	}
	return;
exit:
	manager_common_send_message(info.task.msg.receiver, &msg);
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
	int temp = 0;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.result = 0;
	msg.arg_in.cat = 0;
	/***************************/
	switch(info.status){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			info.status = STATUS_WAIT;
			break;
		case STATUS_SETUP:
			server_setup();
			break;
		case STATUS_IDLE:
			if( info.thread_start == 0) {
				if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_QUALITY ) {
					temp = *((int*)(info.task.msg.arg));
					if( temp != config.profile.quality ) {
						config.profile.quality = temp;
						log_qcy(DEBUG_INFO, "changed the quality = %d", config.profile.quality);
						video_config_video_set(CONFIG_VIDEO_PROFILE, &config.profile);
						msg.arg_in.wolf = 1;
						msg.arg = &temp;
						msg.arg_size = sizeof(temp);
					}
				}
				para_set = 1;
				if( info.task.start == STATUS_IDLE )
					goto exit;
				else
					info.status = STATUS_START;
			}
			break;
		case STATUS_START:
			server_start();
			break;
		case STATUS_RUN:
			if( !para_set ) {
				if( info.task.msg.arg_in.cat == VIDEO_PROPERTY_QUALITY ) {
					temp = *((int*)(info.task.msg.arg));
					if( temp == config.profile.quality ) {
						msg.arg_in.wolf = 0;
						msg.arg = &temp;
						msg.arg_size = sizeof(temp);
						goto exit;
					}
				}
				info.status = STATUS_STOP;
			}
			else {
				if( misc_get_bit( info.thread_start, THREAD_VIDEO ) )
					goto exit;
			}
			break;
		case STATUS_STOP:
			server_stop();
			break;
		case STATUS_ERROR:
			msg.result = -1;
			goto exit;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_control = %d", info.status);
			break;
	}
	return;
exit:
	manager_common_send_message( info.task.msg.receiver, &msg);
	msg_free(&info.task.msg);
	para_set = 0;
	info.task.func = task_default;
	info.msg_lock = 0;
	return;
}
/*
 * task start: idle->start
 */
static void task_start(void)
{
	message_t msg;
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.result = 0;
	/***************************/
	switch(info.status){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			server_setup();
			break;
		case STATUS_IDLE:
			info.status = STATUS_START;
			break;
		case STATUS_START:
			server_start();
			break;
		case STATUS_RUN:
			if( misc_get_bit( info.thread_start, THREAD_VIDEO ) )
				goto exit;
			break;
		case STATUS_ERROR:
			msg.result = -1;
			goto exit;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_start = %d", info.status);
			break;
	}
	return;
exit:
	if( msg.result == 0 ) {
		if( info.task.msg.sender == SERVER_MISS ) {
			video_add_session(info.task.msg.arg_in.handler, info.task.msg.arg_in.wolf);
		}
		if( info.task.msg.sender == SERVER_MISS ) misc_set_bit(&info.status2, (RUN_MODE_MISS + info.task.msg.arg_in.wolf), 1);
		if( info.task.msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_MICLOUD, 1);
		if( info.task.msg.sender == SERVER_RECORDER) misc_set_bit(&info.status2, (RUN_MODE_SAVE + info.task.msg.arg_in.wolf), 1);
	}
	manager_common_send_message(info.task.msg.receiver, &msg);
	msg_free(&info.task.msg);
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
	/**************************/
	msg_init(&msg);
	memcpy(&(msg.arg_pass), &(info.task.msg.arg_pass),sizeof(message_arg_t));
	msg.message = info.task.msg.message | 0x1000;
	msg.sender = msg.receiver = SERVER_VIDEO;
	msg.result = 0;
	/***************************/
	switch(info.status){
		case STATUS_NONE:
		case STATUS_WAIT:
		case STATUS_SETUP:
			goto exit;
			break;
		case STATUS_IDLE:
			if( info.thread_start == 0 )
				goto exit;
			break;
		case STATUS_START:
		case STATUS_RUN:
			if( info.task.msg.arg_in.cat > 0 ) {
				goto exit;
				break;
			}
			else
				server_stop();
			break;
		case STATUS_ERROR:
			msg.result = -1;
			goto exit;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_stop = %d", info.status);
			break;
	}
	return;
exit:
	if( msg.result==0 ) {
		if( info.task.msg.sender == SERVER_MISS ) {
			video_remove_session(info.task.msg.arg_in.handler, info.task.msg.arg_in.wolf);
		}
		if( info.task.msg.sender == SERVER_MISS) misc_set_bit(&info.status2, (RUN_MODE_MISS + info.task.msg.arg_in.wolf), 0);
		if( info.task.msg.sender == SERVER_MICLOUD) misc_set_bit(&info.status2, RUN_MODE_MICLOUD, 0);
		if( info.task.msg.sender == SERVER_RECORDER) misc_set_bit(&info.status2, (RUN_MODE_SAVE + info.task.msg.arg_in.wolf), 0);
		log_qcy(DEBUG_INFO, "=========status2 = %x", info.status2);
	}
	manager_common_send_message(info.task.msg.receiver, &msg);
	msg_free(&info.task.msg);
	info.task.func = &task_default;
	info.msg_lock = 0;
	return;
}
/*
 * task
 */
/*
 * default exit: *->exit
 */
static void task_exit(void)
{
	switch( info.status ){
		case EXIT_INIT:
			info.error = VIDEO_EXIT_CONDITION;
			if( info.task.msg.sender == SERVER_MANAGER) {
				info.error &= (info.task.msg.arg_in.cat);
			}
			info.status = EXIT_SERVER;
			break;
		case EXIT_SERVER:
			if( !info.error )
				info.status = EXIT_STAGE1;
			break;
		case EXIT_STAGE1:
			server_release_1();
			info.status = EXIT_THREAD;
			break;
		case EXIT_THREAD:
			info.thread_exit = info.thread_start;
			video_broadcast_thread_exit();
			if( !info.thread_start )
				info.status = EXIT_STAGE2;
			break;
			break;
		case EXIT_STAGE2:
			server_release_2();
			info.status = EXIT_FINISH;
			break;
		case EXIT_FINISH:
			info.exit = 1;
		    /********message body********/
			message_t msg;
			msg_init(&msg);
			msg.message = MSG_MANAGER_EXIT_ACK;
			msg.sender = SERVER_VIDEO;
			manager_common_send_message(SERVER_MANAGER, &msg);
			/***************************/
			info.status = STATUS_NONE;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_exit = %d", info.status);
			break;
		}
	return;
}

/*
 * default task: none->run
 */
static void task_default(void)
{
	switch( info.status ){
		case STATUS_NONE:
			server_none();
			break;
		case STATUS_WAIT:
			info.status = STATUS_SETUP;
			break;
		case STATUS_SETUP:
			server_setup();
			break;
		case STATUS_IDLE:
			break;
		case STATUS_START:
			break;
		case STATUS_RUN:
			break;
		case STATUS_ERROR:
			info.task.func = task_exit;
			info.status = EXIT_INIT;
			info.msg_lock = 0;
			break;
		default:
			log_qcy(DEBUG_SERIOUS, "!!!!!!!unprocessed server status in task_default = %d", info.status);
			break;
		}
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
	msg_buffer_init2(&message, MSG_BUFFER_OVERFLOW_NO, &mutex);
	info.init = 1;
	//default task
	info.task.func = task_default;
	while( !info.exit ) {
		info.old_status = info.status;
		info.task.func();
		server_message_proc();
	}
	server_release_3();
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
	int ret=0;
	pthread_mutex_lock(&mutex);
	if( !message.init ) {
		log_qcy(DEBUG_SERIOUS, "video server is not ready for message processing!");
		pthread_mutex_unlock(&mutex);
		return -1;
	}
	ret = msg_buffer_push(&message, msg);
	log_qcy(DEBUG_VERBOSE, "push into the video message queue: sender=%d, message=%x, ret=%d, head=%d, tail=%d", msg->sender, msg->message, ret,
			message.head, message.tail);
	if( ret!=0 )
		log_qcy(DEBUG_WARNING, "message push in video error =%d", ret);
	else {
		pthread_cond_signal(&cond);
	}
	pthread_mutex_unlock(&mutex);
	return ret;
}
