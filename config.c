/*
 * config_video.c
 *
 *  Created on: Sep 1, 2020
 *      Author: ning
 */


/*
 * header
 */
//system header
#include <pthread.h>
#include <stdio.h>
#include <rtsvideo.h>
#include <malloc.h>
//program header
#include "../../tools/tools_interface.h"
#include "../../manager/manager_interface.h"
//server header
#include "config.h"

/*
 * static
 */
//variable
static int							dirty;
static video_config_t				video_config;

static config_map_t video_config_profile_map[] = {
	{"quality",				&(video_config.profile.quality),						cfg_u32, 2,0,0,32,},
    {"low_format", 			&(video_config.profile.profile[0].fmt),				cfg_u32, 3,0,0,32,},
    {"low_width",			&(video_config.profile.profile[0].video.width),		cfg_u32, 320,0,0,4800,},
	{"low_height",			&(video_config.profile.profile[0].video.height),	cfg_u32, 240,0,0,4800,},
	{"low_numerator",		&(video_config.profile.profile[0].video.numerator),	cfg_u32, 1,0,0,100,},
	{"low_denominator",		&(video_config.profile.profile[0].video.denominator),	cfg_u32, 15,0,0,100,},
    {"medium_format", 		&(video_config.profile.profile[1].fmt),				cfg_u32, 3,0,0,32,},
    {"medium_width",		&(video_config.profile.profile[1].video.width),		cfg_u32, 640,0,0,4800,},
	{"medium_height",		&(video_config.profile.profile[1].video.height),		cfg_u32, 480,0,0,4800,},
	{"medium_numerator",	&(video_config.profile.profile[1].video.numerator),	cfg_u32, 1,0,0,100,},
	{"medium_denominator",	&(video_config.profile.profile[1].video.denominator),	cfg_u32, 15,0,0,100,},
    {"high_format", 		&(video_config.profile.profile[2].fmt),				cfg_u32, 3,0,0,32,},
    {"high_width",			&(video_config.profile.profile[2].video.width),		cfg_u32, 640,0,0,4800,},
	{"high_height",			&(video_config.profile.profile[2].video.height),		cfg_u32, 480,0,0,4800,},
	{"high_numerator",		&(video_config.profile.profile[2].video.numerator),	cfg_u32, 1,0,0,100,},
	{"high_denominator",	&(video_config.profile.profile[2].video.denominator),	cfg_u32, 15,0,0,100,},
    {NULL,},
};

static config_map_t video_config_isp_map[] = {
	{"isp_buf_num",				&(video_config.isp.isp_attr.isp_buf_num),	cfg_s32, 2,0,0,10,},
	{"isp_id", 					&(video_config.isp.isp_attr.isp_id), 		cfg_s32, 0,0,0,1,},
    {"awb_ctrl", 				&(video_config.isp.awb_ctrl), 				cfg_u32, 1,0,0,2,},
    {"af",						&(video_config.isp.af),						cfg_u32, 1,0,0,1,},
	{"exposure_mode",			&(video_config.isp.exposure_mode),			cfg_u32, 1,0,0,1,},
    {"pan", 					&(video_config.isp.pan), 					cfg_s32, 0,0,-57600,57600,},
    {"tilt",					&(video_config.isp.tilt),					cfg_s32, 0,0,-43200,43200,},
	{"mirror",					&(video_config.isp.mirror),					cfg_u32, 0,0,0,1,},
    {"flip", 					&(video_config.isp.flip), 					cfg_u32, 0,0,0,1,},
    {"wdr_mode",				&(video_config.isp.wdr_mode),				cfg_u32, 0,0,0,2,},
	{"wdr_level",				&(video_config.isp.wdr_level),				cfg_u32, 0,0,0,100,},
    {"ir_mode", 				&(video_config.isp.ir_mode), 				cfg_u32, 0,0,0,2,},
    {"smart_ir_mode",			&(video_config.isp.smart_ir_mode),			cfg_u32, 0,0,0,4,},
	{"smart_ir_manual_level",	&(video_config.isp.smart_ir_manual_level),	cfg_u32, 50,0,0,100,},
    {"ldc", 					&(video_config.isp.ldc), 					cfg_u32, 0,0,0,1,},
    {"noise_reduction",			&(video_config.isp.noise_reduction),		cfg_u32, 4,0,1,7,},
	{"in_out_door_mode",		&(video_config.isp.in_out_door_mode),		cfg_u32, 0,0,0,2,},
    {"detail_enhancement", 		&(video_config.isp.detail_enhancement), 	cfg_u32, 4,0,1,7,},
    {NULL,},
};

static config_map_t video_config_h264_map[] = {
    {"profile_level", 				&(video_config.h264.h264_attr.level), 						cfg_u32, 12,0,0,100,},
    {"profile_qp",					&(video_config.h264.h264_attr.qp),							cfg_s32, -1,0,-1,51,},
	{"profile_bps",					&(video_config.h264.h264_attr.bps),							cfg_u32, 2097152,0,10000,40000000,},
	{"profile_gop",					&(video_config.h264.h264_attr.gop),							cfg_u32, 30,0,0,32767,},
	{"profile_rotation",			&(video_config.h264.h264_attr.rotation),					cfg_u32, 0,0,0,3,},
	{"profile_videostab",			&(video_config.h264.h264_attr.videostab),					cfg_u32, 0,0,0,1,},
    {"supported_bitrate_mode", 		&(video_config.h264.h264_ctrl.supported_bitrate_mode), 		cfg_u32, 0,0,0,100,},
    {"supported_gop_mode",			&(video_config.h264.h264_ctrl.supported_gop_mode),			cfg_u32, 0,0,0,100,},
	{"bitrate_mode",				&(video_config.h264.h264_ctrl.bitrate_mode),				cfg_u32, 8,0,0,1000000,},
	{"gop_mode",					&(video_config.h264.h264_ctrl.gop_mode),					cfg_u32, 0,0,0,100,},
	{"bitrate",						&(video_config.h264.h264_ctrl.bitrate),						cfg_u32, 0,0,0,40000000,},
	{"max_bitrate",					&(video_config.h264.h264_ctrl.max_bitrate),					cfg_u32, 0,0,0,100,},
    {"min_bitrate", 				&(video_config.h264.h264_ctrl.min_bitrate), 				cfg_u32, 0,0,0,100,},
    {"qp",							&(video_config.h264.h264_ctrl.qp),							cfg_s32, 0,0,-1,100,},
	{"max_qp",						&(video_config.h264.h264_ctrl.max_qp),						cfg_u32, 0,0,0,51,},
	{"min_qp",						&(video_config.h264.h264_ctrl.min_qp),						cfg_u32, 0,0,0,51,},
	{"intra_qp_delta",				&(video_config.h264.h264_ctrl.gop),							cfg_u32, 0,0,0,32767,},
	{"gop",							&(video_config.h264.h264_ctrl.max_bitrate),					cfg_u32, 0,0,0,100,},
    {"slice_size", 					&(video_config.h264.h264_ctrl.slice_size), 					cfg_u32, 0,0,0,100,},
    {"sei_messages",				&(video_config.h264.h264_ctrl.sei_messages),				cfg_u32, 0,0,0,100,},
	{"video_full_range",			&(video_config.h264.h264_ctrl.video_full_range),			cfg_u32, 0,0,0,1000000,},
	{"constrained_intra_prediction",&(video_config.h264.h264_ctrl.constrained_intra_prediction),cfg_u32, 0,0,0,100,},
	{"disable_deblocking_filter",	&(video_config.h264.h264_ctrl.disable_deblocking_filter),	cfg_u32, 0,0,0,100,},
	{"enable_cabac",				&(video_config.h264.h264_ctrl.enable_cabac),				cfg_u32, 0,0,0,100,},
    {"cabac_init_idc", 				&(video_config.h264.h264_ctrl.cabac_init_idc), 				cfg_u32, 0,0,0,100,},
    {"transform8x8mode",			&(video_config.h264.h264_ctrl.transform8x8mode),			cfg_u32, 0,0,0,100,},
	{"gdr",							&(video_config.h264.h264_ctrl.gdr),							cfg_u32, 0,0,0,100,},
	{"hrd",							&(video_config.h264.h264_ctrl.hrd),							cfg_u32, 0,0,0,100,},
	{"hrd_cpb_size",				&(video_config.h264.h264_ctrl.hrd_cpb_size),				cfg_u32, 0,0,0,100,},
	{"longterm_pic_rate",			&(video_config.h264.h264_ctrl.longterm_pic_rate),			cfg_s32, 0,0,0,100,},
	{"br_level",					&(video_config.h264.h264_ctrl.br_level),					cfg_u32, 0,0,0,100,},
	{"super_p_period",				&(video_config.h264.h264_ctrl.super_p_period),				cfg_s32, 0,0,0,100,},
	{"mbrc_en",						&(video_config.h264.h264_ctrl.mbrc_en),						cfg_u32, 0,0,0,100,},
	{"mbrc_qp_gain",				&(video_config.h264.h264_ctrl.mbrc_qp_gain),				cfg_float, 0,0,0,100,},
    {"mbrc_qp_delta_range", 		&(video_config.h264.h264_ctrl.mbrc_qp_delta_range), 		cfg_s32, 0,0,0,12,},
    {NULL,},
};

static config_map_t video_config_osd_map[] = {
	{"enable", 				&(video_config.osd.enable), 			cfg_u32, 1,0,0,1,},
	{"time_mode", 			&(video_config.osd.time_mode), 			cfg_u32, 0,0,0,1,},
	{"time_rotate", 		&(video_config.osd.time_rotate), 		cfg_u32, 0,0,0,1,},
	{"time_font_face",		&(video_config.osd.time_font_face),		cfg_string, ' ',0,0,128,},
	{"time_pixel_size", 	&(video_config.osd.time_pixel_size),	cfg_u32, 0,0,0,256,},
	{"time_alpha", 			&(video_config.osd.time_alpha), 		cfg_u32, 0,0,0,256,},
	{"time_color", 			&(video_config.osd.time_color), 		cfg_u32, 0,0,0,255,},
	{"time_pos_mode", 		&(video_config.osd.time_pos_mode), 		cfg_u32, 0,0,0,100,},
	{"time_offset", 		&(video_config.osd.time_offset), 		cfg_u32, 0,0,0,255,},
	{"time_flick", 			&(video_config.osd.time_flick), 		cfg_s32, 0,0,0,360,},
	{"time_flick_on", 		&(video_config.osd.time_flick_on), 		cfg_s32, 0,0,0,10000,},
	{"time_flick_off", 		&(video_config.osd.time_flick_off), 	cfg_s32, 0,0,0,10000,},
	{"warning_mode", 		&(video_config.osd.warning_mode), 		cfg_u32, 0,0,0,1,},
	{"warning_rotate", 		&(video_config.osd.warning_rotate), 	cfg_u32, 0,0,0,1,},
	{"warning_font_face",	&(video_config.osd.warning_font_face),	cfg_string, ' ',0,0,128,},
	{"warning_pixel_size", 	&(video_config.osd.warning_pixel_size),	cfg_u32, 0,0,0,256,},
	{"warning_alpha", 		&(video_config.osd.warning_alpha), 		cfg_u32, 0,0,0,256,},
	{"warning_color", 		&(video_config.osd.warning_color), 		cfg_u32, 0,0,0,255,},
	{"warning_pos_mode", 	&(video_config.osd.warning_pos_mode), 	cfg_u32, 0,0,0,100,},
	{"warning_offset", 		&(video_config.osd.warning_offset), 	cfg_u32, 0,0,0,255,},
	{"warning_flick", 		&(video_config.osd.warning_flick), 		cfg_s32, 0,0,0,360,},
	{"warning_flick_on", 	&(video_config.osd.warning_flick_on), 	cfg_s32, 0,0,0,10000,},
	{"warning_flick_off", 	&(video_config.osd.warning_flick_off), 	cfg_s32, 0,0,0,10000,},
	{"label_mode", 			&(video_config.osd.label_mode), 		cfg_u32, 0,0,0,1,},
	{"label_rotate", 		&(video_config.osd.label_rotate), 		cfg_u32, 0,0,0,1,},
	{"label_font_face",		&(video_config.osd.label_font_face),	cfg_string, ' ',0,0,128,},
	{"label_pixel_size", 	&(video_config.osd.label_pixel_size),	cfg_u32, 0,0,0,256,},
	{"label_alpha", 		&(video_config.osd.label_alpha), 		cfg_u32, 0,0,0,256,},
	{"label_color", 		&(video_config.osd.label_color), 		cfg_u32, 0,0,0,255,},
	{"label_pos_mode", 		&(video_config.osd.label_pos_mode), 	cfg_u32, 0,0,0,100,},
	{"label_offset", 		&(video_config.osd.label_offset), 		cfg_u32, 0,0,0,255,},
	{"label_flick", 		&(video_config.osd.label_flick), 		cfg_s32, 0,0,0,360,},
	{"label_flick_on", 		&(video_config.osd.label_flick_on), 	cfg_s32, 0,0,0,10000,},
	{"label_flick_off", 	&(video_config.osd.label_flick_off), 	cfg_s32, 0,0,0,10000,},
    {NULL,},
};

static config_map_t video_config_3actrl_map[] = {
    {"awb_mode", 		&(video_config.a3ctrl.awb_para.awb_mode), 		cfg_u32, 1,0,0,10,},
	{"awb_temperature",	&(video_config.a3ctrl.awb_para.awb_temperature),cfg_s32, 0,0,0,10000,},
    {"awb_rgain",		&(video_config.a3ctrl.awb_para.awb_rgain),		cfg_s32, 0,0,0,256,},
	{"awb_bgain",		&(video_config.a3ctrl.awb_para.awb_bgain),		cfg_s32, 0,0,0,256,},
	{"awb_r",			&(video_config.a3ctrl.awb_para.awb_r),			cfg_s32, 0,0,0,256,},
	{"awb_g",			&(video_config.a3ctrl.awb_para.awb_g),			cfg_s32, 0,0,0,256,},
	{"awb_b",			&(video_config.a3ctrl.awb_para.awb_b),			cfg_s32, 0,0,0,256,},
	{"af_window_size",	&(video_config.a3ctrl.af_para.af_window_size),	cfg_s32, 0,0,0,10000,},
	{"af_window_num",	&(video_config.a3ctrl.af_para.af_window_num),	cfg_s32, 0,0,0,100,},
    {"ae_mode", 		&(video_config.a3ctrl.ae_para.ae_mode), 		cfg_u32, 0,0,0,10,},
	{"ae_target_delta",	&(video_config.a3ctrl.ae_para.ae_target_delta),	cfg_s32, 0,0,0,10000,},
    {"ae_gain_max",		&(video_config.a3ctrl.ae_para.ae_gain_max),		cfg_s32, 0,0,0,10000,},
	{"ae_min_fps",		&(video_config.a3ctrl.ae_para.ae_min_fps),		cfg_s32, 0,0,0,10000,},
	{"ae_weight",		&(video_config.a3ctrl.ae_para.ae_weight),		cfg_s32, 0,0,0,10000,},
	{"ae_total_gain",	&(video_config.a3ctrl.ae_para.ae_total_gain),	cfg_s32, 0,0,0,10000,},
	{"ae_analog",		&(video_config.a3ctrl.ae_para.ae_analog),		cfg_s32, 0,0,0,10000,},
	{"ae_digital",		&(video_config.a3ctrl.ae_para.ae_digital),		cfg_s32, 0,0,0,10000,},
	{"ae_isp_digital",	&(video_config.a3ctrl.ae_para.ae_isp_digital),	cfg_s32, 0,0,0,10000,},
    {"ae_exposure_time",&(video_config.a3ctrl.ae_para.ae_exposure_time),cfg_s32, 0,0,0,10000,},
    {NULL,},
};

//function
static int video_config_save(void);


/*
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 * %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
 */


/*
 * interface
 */
static int video_config_init_auto_profile(void)
{
	int id;
	// ~1
	id = 1;
//	video_config.profile.profile[id].fmt = 3;
//	video_config.profile.profile[id].video.width = 640;
//	video_config.profile.profile[id].video.height = 480;
//	video_config.profile.profile[id].video.numerator = 1;
//	video_config.profile.profile[id].video.denominator = 15;
	video_config.h264.h264_gop[id] = 20;
	video_config.h264.h264_bitrate[id] = 250*1024;
	video_config.h264.h264_level[id] = 12;
	video_config.h264.h264_qp[id] = -1;
	// ~2
	id = 2;
//	video_config.profile.profile[id].fmt = 3;
//	video_config.profile.profile[id].video.width = 1920;
//	video_config.profile.profile[id].video.height = 1080;
//	video_config.profile.profile[id].video.numerator = 1;
//	video_config.profile.profile[id].video.denominator = 15;
	video_config.h264.h264_gop[id] = 30;
	video_config.h264.h264_bitrate[id] = 1250*1024;
	video_config.h264.h264_level[id] = 12;
	video_config.h264.h264_qp[id] = -1;
	//*************************************************************
	//start of auto mode configuration
	// ~ 3
	id = 3;
	video_config.profile.profile[id].fmt = 3;
	video_config.profile.profile[id].video.width = 640;
	video_config.profile.profile[id].video.height = 360;
	video_config.profile.profile[id].video.numerator = 1;
	video_config.profile.profile[id].video.denominator = 10;
	video_config.h264.h264_gop[id] = 20;
	video_config.h264.h264_bitrate[id] = 250*1024;
	video_config.h264.h264_level[id] = 12;
	video_config.h264.h264_qp[id] = -1;
	// ~4
	id = 4;
	video_config.profile.profile[id].fmt = 3;
	video_config.profile.profile[id].video.width = 640;
	video_config.profile.profile[id].video.height = 360;
	video_config.profile.profile[id].video.numerator = 1;
	video_config.profile.profile[id].video.denominator = 15;
	video_config.h264.h264_gop[id] = 30;
	video_config.h264.h264_bitrate[id] = 500*1024;
	video_config.h264.h264_level[id] = 12;
	video_config.h264.h264_qp[id] = -1;
	// ~5
	id = 5;
	video_config.profile.profile[id].fmt = 3;
	video_config.profile.profile[id].video.width = 1280;
	video_config.profile.profile[id].video.height = 720;
	video_config.profile.profile[id].video.numerator = 1;
	video_config.profile.profile[id].video.denominator = 10;
	video_config.h264.h264_gop[id] = 20;
	video_config.h264.h264_bitrate[id] = 750*1024;
	video_config.h264.h264_level[id] = 12;
	video_config.h264.h264_qp[id] = -1;
	// ~6
	id = 6;
	video_config.profile.profile[id].fmt = 3;
	video_config.profile.profile[id].video.width = 1280;
	video_config.profile.profile[id].video.height = 720;
	video_config.profile.profile[id].video.numerator = 1;
	video_config.profile.profile[id].video.denominator = 15;
	video_config.h264.h264_gop[id] = 30;
	video_config.h264.h264_bitrate[id] = 1000*1024;
	video_config.h264.h264_level[id] = 12;
	video_config.h264.h264_qp[id] = -1;
	// ~7
	id = 7;
	video_config.profile.profile[id].fmt = 3;
	video_config.profile.profile[id].video.width = 1920;
	video_config.profile.profile[id].video.height = 1080;
	video_config.profile.profile[id].video.numerator = 1;
	video_config.profile.profile[id].video.denominator = 10;
	video_config.h264.h264_gop[id] = 20;
	video_config.h264.h264_bitrate[id] = 1250*1024;
	video_config.h264.h264_level[id] = 12;
	video_config.h264.h264_qp[id] = -1;
	// ~8
	id = 8;
	video_config.profile.profile[id].fmt = 3;
	video_config.profile.profile[id].video.width = 1920;
	video_config.profile.profile[id].video.height = 1080;
	video_config.profile.profile[id].video.numerator = 1;
	video_config.profile.profile[id].video.denominator = 15;
	video_config.h264.h264_gop[id] = 30;
	video_config.h264.h264_bitrate[id] = 1250*1024;
	video_config.h264.h264_level[id] = 12;
	video_config.h264.h264_qp[id] = -1;
}

static int video_config_save(void)
{
	int ret = 0;
	message_t msg;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	if( misc_get_bit(dirty, CONFIG_VIDEO_PROFILE) ) {
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_PROFILE_PATH);
		ret = write_config_file(&video_config_profile_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_VIDEO_PROFILE, 0);
	}
	else if( misc_get_bit(dirty, CONFIG_VIDEO_ISP) ) {
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_ISP_PATH);
		ret = write_config_file(&video_config_isp_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_VIDEO_ISP, 0);
	}
	else if( misc_get_bit(dirty, CONFIG_VIDEO_H264) ) {
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_H264_PATH);
		ret = write_config_file(&video_config_h264_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_VIDEO_H264, 0);
	}
	else if( misc_get_bit(dirty, CONFIG_VIDEO_OSD) ) {
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_OSD_PATH);
		ret = write_config_file(&video_config_osd_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_VIDEO_OSD, 0);
	}
	else if( misc_get_bit(dirty, CONFIG_VIDEO_3ACTRL) ) {
		memset(fname,0,sizeof(fname));
		sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_3ACTRL_PATH);
		ret = write_config_file(&video_config_3actrl_map, fname);
		if(!ret)
			misc_set_bit(&dirty, CONFIG_VIDEO_3ACTRL, 0);
	}
	if( !dirty ) {
		/********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_REMOVE;
		msg.arg_in.handler = video_config_save;
		/****************************/
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	return ret;
}

int video_config_video_read(video_config_t *vconf)
{
	int ret,ret1=0;
	char fname[MAX_SYSTEM_STRING_SIZE*2];
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_PROFILE_PATH);
	ret = read_config_file(&video_config_profile_map, fname);
	if(!ret)
		misc_set_bit(&video_config.status, CONFIG_VIDEO_PROFILE,1);
	else
		misc_set_bit(&video_config.status, CONFIG_VIDEO_PROFILE,0);
	ret1 |= ret;
	video_config_init_auto_profile();
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_ISP_PATH);
	ret = read_config_file(&video_config_isp_map,fname );
	if(!ret)
		misc_set_bit(&video_config.status, CONFIG_VIDEO_ISP,1);
	else
		misc_set_bit(&video_config.status, CONFIG_VIDEO_ISP,0);
	ret1 |= ret;
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_H264_PATH);
	ret = read_config_file(&video_config_h264_map,fname );
	if(!ret)
		misc_set_bit(&video_config.status, CONFIG_VIDEO_H264,1);
	else
		misc_set_bit(&video_config.status, CONFIG_VIDEO_H264,0);
	ret1 |= ret;
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_OSD_PATH);
	ret = read_config_file(&video_config_osd_map, fname);
	if(!ret)
		misc_set_bit(&video_config.status, CONFIG_VIDEO_OSD,1);
	else
		misc_set_bit(&video_config.status, CONFIG_VIDEO_OSD,0);
	ret1 |= ret;
	memset(fname,0,sizeof(fname));
	sprintf(fname,"%s%s",_config_.qcy_path, CONFIG_VIDEO_3ACTRL_PATH);
	ret = read_config_file(&video_config_3actrl_map, fname);
	if(!ret)
		misc_set_bit(&video_config.status, CONFIG_VIDEO_3ACTRL,1);
	else
		misc_set_bit(&video_config.status, CONFIG_VIDEO_3ACTRL,0);
	ret1 |= ret;
	memcpy(vconf,&video_config,sizeof(video_config_t));
	return ret1;
}

int video_config_video_set(int module, void* arg)
{
	int ret = 0;
	if(dirty==0) {
		message_t msg;
	    /********message body********/
		msg_init(&msg);
		msg.message = MSG_MANAGER_TIMER_ADD;
		msg.sender = SERVER_VIDEO;
		msg.arg_in.cat = 30000;	//1min
		msg.arg_in.dog = 0;
		msg.arg_in.duck = 0;
		msg.arg_in.handler = &video_config_save;
		/****************************/
		manager_common_send_message(SERVER_MANAGER, &msg);
	}
	misc_set_bit(&dirty, module, 1);
	if( module == CONFIG_VIDEO_PROFILE) {
		memcpy( (video_profile_config_t*)(&video_config.profile), arg, sizeof(video_profile_config_t));
	}
	else if ( module == CONFIG_VIDEO_ISP ) {
		memcpy( (video_isp_config_t*)(&video_config.isp), arg, sizeof(video_isp_config_t));
	}
	else if ( module == CONFIG_VIDEO_H264 ) {
		memcpy( (video_h264_config_t*)(&video_config.h264), arg, sizeof(video_h264_config_t));
	}
	else if ( module == CONFIG_VIDEO_OSD ) {
		memcpy( (video_osd_config_t*)(&video_config.osd), arg, sizeof(video_osd_config_t));
	}
	else if ( module == CONFIG_VIDEO_3ACTRL ) {
		memcpy( (video_3actrl_config_t*)(&video_config.a3ctrl), arg, sizeof(video_3actrl_config_t));
	}
	return ret;
}
