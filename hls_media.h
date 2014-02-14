/*
 * Definition of media types and handlers
 * Copyright (c) 2012-2013 Voicebase Inc.
 *
 * Author: Alexander Ustinov
 * email: alexander@voicebase.com
 *
 * This file is the part of the mod_hls apache module
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser General Public License version 3. See the LICENSE file
 * at the top of the source tree.
 *
 */

#ifndef __HLS_MEDIA__
#define __HLS_MEDIA__

#include "hls_file.h"


//this is values for codec member of track_t
#define MPEG_AUDIO_L3  0x04
#define MPEG_AUDIO_L2  0x03
#define AAC_AUDIO		0x0F
#define H264_VIDEO		0x1B


#define KEY_FRAME_FLAG 1

typedef struct track_t{
	int 	codec;
	int 	n_frames;
	int 	bitrate;
	float*	pts;
	float*	dts;
	int 	repeat_for_every_segment;
	int*	flags;
	int 	sample_rate;
	int 	n_ch;
	int 	sample_size;
	int 	data_start_offset;
	int 	type;
} track_t;

typedef struct media_stats_t{
	int n_tracks;
	track_t* track[2];
} media_stats_t;


typedef struct track_data_t{
	int 	n_frames;
	int 	first_frame;
	char* 	buffer;
	int		buffer_size;
	int*	size;			//size and offset in buffer
	int* 	offset;
	int 	frames_written;
	int 	data_start_offset;
	int 	cc;				//mpeg2 ts continuity counter
} track_data_t;

typedef struct media_data_t{
	int n_tracks;
	track_data_t* track_data[2];
}media_data_t;

typedef struct media_handler_t{
	int (*get_media_stats)(file_handle_t* handle, file_source_t* file, media_stats_t* output_buffer, int output_buffer_size );
	int (*get_media_data)(file_handle_t* handle, file_source_t* file, media_stats_t* stats, int piece, media_data_t* output_buffer, int output_buffer_size );
} media_handler_t;

media_handler_t* get_media_handler(char* filename);

#endif
