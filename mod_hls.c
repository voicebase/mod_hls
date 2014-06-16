/*
 * main file for HLS apache module
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

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_script.h"
#include "http_connection.h"
#include "util_script.h"

#ifdef TWOLAME
#include "twolame.h"
#else
#include "lame.h"
#endif

#include "apr_strings.h"
#include "hls_file.h"
#include "hls_media.h"
#include "hls_mux.h"
#include "mod_conf.h"

#include <stdio.h>

double get_clock(){
	struct timeval ts;
	gettimeofday(&ts, NULL);
	return ts.tv_sec + (double)(ts.tv_usec) / 1000000.0;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/* Data declarations.                                                       */
/*                                                                          */
/* Here are the static cells and structure declarations private to our      */
/* module.                                                                  */
/*                                                                          */
/*--------------------------------------------------------------------------*/

/*
 * Declare ourselves so the configuration routines can find and know us.
 * We'll fill it in at the end of the module.
 */
module AP_MODULE_DECLARE_DATA hls_module;

/*--------------------------------------------------------------------------*/
/*                                                                          */
/* These routines are strictly internal to this module, and support its     */
/* operation.  They are not referenced by any external portion of the       */
/* server.                                                                  */
/*                                                                          */
/*--------------------------------------------------------------------------*/


static void remove_last_extension(char* filename){
    int len = strlen(filename);
    int pos = len - 1;
    while (pos >= 0 && filename[pos]!='.')
    	--pos;
    filename[pos] = 0;
}

int is_letter(char c){
	if (c >= '0' && c <= '9')
		return 1;
	if (c >= 'a' && c <= 'z')
		return 1;
	if (c >= 'A' && c <= 'Z')
		return 1;
	return 0;
}

char int_to_hex(int v){
	switch(v){
		case 0:
			return '0';
		case 1:
			return '1';
		case 2:
			return '2';
		case 3:
			return '3';
		case 4:
			return '4';
		case 5:
			return '5';
		case 6:
			return '6';
		case 7:
			return '7';
		case 8:
			return '8';
		case 9:
			return '9';
		case 10:
			return 'A';
		case 11:
			return 'B';
		case 12:
			return 'C';
		case 13:
			return 'D';
		case 14:
			return 'E';
		case 15:
			return 'F';

	}
	return '0';
}

void convert_to_hex(char* res, unsigned char c){
	int c1 = c >> 4;
	int c2 = (c & 0xF);

	res[0] = '%';
	res[1] = int_to_hex(c1);
	res[2] = int_to_hex(c2);

}

static char* get_pure_filename(request_rec *r, char* filename){
    int len = strlen(filename);
    int pos = len - 1;
    while (pos >= 0 && filename[pos]!='/')
    	--pos;
    return &filename[pos + 1];
}

static int get_real_filename(char* filename){ //return segment number
//the filename ends at _%d.ts
    int len = strlen(filename);
    int pos = len - 1;
    int segment = 0;
    while (pos >= 0 && filename[pos]!='_')
    	--pos;
    filename[pos] = 0;

    sscanf(&filename[pos+1], "%d.ts", &segment);

    return segment;
}

int check_exist(request_rec *r, const char* filename){
	int rc;
	apr_finfo_t finfo;
	int exists;
	rc = apr_stat(&finfo, filename, APR_FINFO_MIN, r->pool);
	if (rc == APR_SUCCESS) {
		exists = ( (finfo.filetype != APR_NOFILE) &&  !(finfo.filetype & APR_DIR) );
		if (exists)
			return 1;
	}
	return 0;
}

int hex_to_int(char c){
	switch (c){
		case '0': return 0;
		case '1': return 1;
		case '2': return 2;
		case '3': return 3;

		case '4': return 4;
		case '5': return 5;
		case '6': return 6;
		case '7': return 7;

		case '8': return 8;
		case '9': return 9;
		case 'a':
		case 'A': return 10;

		case 'b':
		case 'B': return 11;


		case 'c':
		case 'C': return 12;

		case 'd':
		case 'D': return 13;

		case 'e':
		case 'E': return 14;

		case 'f':
		case 'F': return 15;

	}
	return 0;
}

char convert_str_to_char(char c1, char c2){
	return (hex_to_int(c1) << 4) | hex_to_int(c2);
}

char* get_arg_value(request_rec * r, char* args, char* key){
	int i,j;
	int args_len = strlen(args);
	int key_len= strlen(key);
	char* result = apr_pcalloc(r->pool, args_len + 1);
	int level = 0;
	memset(result, 0, args_len + 1);

	for(i = 0; i < args_len; ++ i){
		int found = 0;
		if (i == 0){
			found = (strncmp(&args[ i ], key, key_len) == 0) ? 1 : 0;
		}else{
			if (args[i - 1] == '&'){
				found = (strncmp(&args[ i ], key, key_len) == 0) ? 1 : 0;
			}
		}
		if (found){
			j = 0;
			i+=key_len+1;
			if (i + j + 3 < args_len && args[i+j] == '%' && args[i+j+1] == '2' && args[i+j+2] == '2')
				i+=3;

			while ( i + j < args_len && args[i+j] != '&'){
				result[j] = args[i+j];
				++j;
			}

			if (j > 3 && result[j-3] == '%' && result[j-2] == '2' && result[j-1] == '2'){
				j-=3;
				result[j] =0 ;
			}
			break;
		}

	}

	return result;
}

int is_localhost(char* uri){

	int l = strlen(uri);

	if (l > 10){
		if (uri[0] == '/' &&
			(uri[1] == 'l' || uri[9] == 'L') &&
			(uri[2] == 'o' || uri[9] == 'O') &&
			(uri[3] == 'c' || uri[9] == 'C') &&
			(uri[4] == 'a' || uri[9] == 'A') &&
			(uri[5] == 'l' || uri[9] == 'L') &&
			(uri[6] == 'h' || uri[9] == 'H') &&
			(uri[7] == 'o' || uri[9] == 'O') &&
			(uri[8] == 's' || uri[9] == 'S') &&
			(uri[9] == 't' || uri[9] == 'T'))
			return 1;
	}

	if (l > 10){
		if (uri[0] == '/' &&
			(uri[1] == '1') &&
			(uri[2] == '2') &&
			(uri[3] == '7') &&
			(uri[4] == '.') &&
			(uri[5] == '0') &&
			(uri[6] == '.') &&
			(uri[7] == '0') &&
			(uri[8] == '.') &&
			(uri[9] == '1'))
			return 1;
	}

	return 0;
}


int process_m3u8(request_rec *r, int lookup_uri, int http){
    char *filename;
    int uri_len;
    char* pure_filename;
    int i,k;

    media_handler_t* 	media;
    file_source_t*   	source;
    file_handle_t* 		handle;
    media_stats_t* 		stats;
    int 				piece;
    double start;
    double stop;
    double get_data_start;
    double get_data_stop;

	int 			stats_size;
	char* 			stats_buffer;
	int 			source_size;
	char* data_source =  NULL;
	const char *url = NULL;
	int prefix_len = 0;


//		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "m3u8 url=%s\n", filename);

	if (r->header_only) {
		return OK;
	}

	uri_len = strlen(r->unparsed_uri);

	prefix_len = 0;

	if (http){
		prefix_len = strlen("http:/");
	}else{
		if (get_data_path() == NULL){
			prefix_len = strlen( r->server->path);
		}else{
			prefix_len = strlen( get_data_path() );
		}
	}


	filename = apr_pcalloc(r->pool, uri_len + 1 + prefix_len);

	if (http){
		strcpy(filename, "http:/");
		strcat(filename, r->unparsed_uri);
	}else{
		char* r_unparsed_uri = r->unparsed_uri;
		if (is_localhost(r->unparsed_uri))
			r_unparsed_uri += 10;

		if (get_data_path() == NULL){
			strcpy(filename, r->server->path);
			strcat(filename, r_unparsed_uri);
		}else{
			strcpy(filename, get_data_path());
			strcat(filename, r_unparsed_uri);

		}
	}

	uri_len = strlen(filename);


	start = get_clock();

	remove_last_extension(filename);//for example we have request for 'test.wav.m3u8' this function cut the last extension ('test.wav') and the file really have to exist

	data_source = filename;

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: playlist preparation started");

	media  = get_media_handler(data_source);
	if ( !media ){
		return DECLINED;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: get media handler");

	source_size = get_file_source(r, data_source, NULL, 0);

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: get file source size (%d bytes)", (int) source_size);

	source 	= (file_source_t*)apr_pcalloc(r->pool, source_size);
	if ( !source )
		return HTTP_INSUFFICIENT_STORAGE;

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: allocated file source buffer");

	source_size  = get_file_source(r, data_source, source, source_size);

	if ( source_size <= 0 )
		return HTTP_NOT_ACCEPTABLE;

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: filled file source buffer");


	handle 	= (char*)apr_pcalloc(r->pool, source->handler_size);
	if ( !handle )
		return HTTP_INSUFFICIENT_STORAGE;

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: allocated file handler buffer");


	if ( !source->open(source, handle, data_source, FIRST_ACCESS) ){
		ap_log_error(APLOG_MARK, APLOG_ERR, get_log_level(), r->server, "HLS: opening data source %s failed", data_source);
		return DECLINED;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: opened data source %s", data_source);

	get_data_start = get_clock();
						//ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "11111", stats_size);
	stats_size 			= media->get_media_stats(r, handle, source, NULL, 0);
						ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "22222", stats_size);
	stats_buffer		= (char*)apr_pcalloc(r->pool, stats_size);
						//ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "33333", stats_size);
	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: got media stats size %d", stats_size);
						//ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "44444", stats_size);

	if ( !stats_buffer ){
		source->close(handle, 0);
		ap_log_error(APLOG_MARK, APLOG_ERR, get_log_level(), r->server, "HLS: failed to allocate media stats buffer");

		return HTTP_INSUFFICIENT_STORAGE;
	}

	stats_size 				= media->get_media_stats(r->pool, handle, source, stats_buffer, stats_size);

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: filled media stats buffer");

	get_data_stop = get_clock();

	pure_filename = get_pure_filename(r,data_source); //get only filename without any directory info

	if (pure_filename){
		ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: got pure filename %s", pure_filename);

		int numofchunks=0;
		int playlist_size 		= generate_playlist(stats_buffer, pure_filename, NULL, 0, url, &numofchunks);
		char* playlist_buffer 	= (char*)apr_pcalloc(r->pool, playlist_size);

		if ( !playlist_buffer ){
			source->close(handle, 0);

			ap_log_error(APLOG_MARK, APLOG_ERR, get_log_level(), r->server, "HLS: failed to allocate playlist buffer (requested size = %d)", (int)playlist_size);
			return HTTP_INSUFFICIENT_STORAGE;
		}

		ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: playlist buffer allocated");


		playlist_size 			= generate_playlist(stats_buffer, pure_filename, playlist_buffer, playlist_size, url, &numofchunks);
		if (playlist_size <= 0){
			ap_log_error(APLOG_MARK, APLOG_ERR, get_log_level(), r->server, "HLS: failed to fill playlist buffer");

			source->close(handle, 0);
			return HTTP_FORBIDDEN;
		}

		ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: playlist generated successfully");


		ap_set_content_type(r, "audio/x-mpegurl");

		ap_set_content_length(r, playlist_size);
		ap_rwrite( playlist_buffer, playlist_size, r);

		ap_finalize_request_protocol(r);

		ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: sent content to client");

	}

	source->close(handle, 0);

	stop = get_clock();

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: data source closed");

	return DONE;
}

int process_ts(request_rec *r, int lookup_uri, int http){
    char *filename;
    int uri_len;
    char* pure_filename;
    int i,k;

    media_handler_t* 	media;
    file_source_t*   	source;
    file_handle_t* 		handle;
    media_stats_t* 		stats;
    int 				piece;
    double start;
    double stop;
    double get_data_start;
    double get_data_stop;
	int 			stats_size;
	media_stats_t* 	stats_buffer;
	int 			data_size;
	media_data_t* 	data_buffer;
	int 			muxed_size;
	char* 			muxed_buffer;
	int 			source_size;
	char* 			url;
	char* data_source;
	int prefix_len;

	if (r->header_only) {
		return OK;
	}

	uri_len= strlen(r->unparsed_uri);
	prefix_len = 0;

	if (http){
		prefix_len = strlen("http:/");
	}else{
		if (get_data_path() == NULL){
			prefix_len = strlen( r->server->path);
		}else{
			prefix_len = strlen( get_data_path() );
		}
	}


	filename = apr_pcalloc(r->pool, uri_len + 1 + prefix_len);

	if (http){
		strcpy(filename, "http:/");
		strcat(filename, r->unparsed_uri);
	}else{
		char* r_unparsed_uri = r->unparsed_uri;
		if (is_localhost(r->unparsed_uri))
			r_unparsed_uri += 10;

		if (get_data_path() == NULL){
			strcpy(filename, r->server->path);
			strcat(filename, r_unparsed_uri);
		}else{
			strcpy(filename, get_data_path());
			strcat(filename, r_unparsed_uri);

		}
	}

	uri_len = strlen(filename);

	start = get_clock();

	piece = get_real_filename(filename);

	data_source = filename;

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: segment preparation started");

	media  = get_media_handler(data_source);
	if ( !media )
		return HTTP_NOT_ACCEPTABLE;

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: got media handler");

	source_size = get_file_source(r, data_source, NULL, 0);

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: data source requires %d bytes", (int)source_size);

	source 	= (file_source_t*)apr_pcalloc(r->pool, source_size);
	if ( !source )
		return HTTP_INSUFFICIENT_STORAGE;

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: data source allocated");

	source_size  = get_file_source(r, data_source, source, source_size);

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: data source filled");

	if ( source_size <= 0 )
		return HTTP_NOT_ACCEPTABLE;

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: file handler requires %d bytes", (int)source->handler_size);

	handle 	= (char*)apr_pcalloc(r->pool, source->handler_size);
	if ( !handle )
		return HTTP_INSUFFICIENT_STORAGE;

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: file handler allocated");

	if ( !source->open(source, handle, data_source, 0) ){
		ap_log_error(APLOG_MARK, APLOG_ERR, get_log_level(), r->server, "HLS: failed to open file %s", data_source);
		return HTTP_NOT_FOUND;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: file opened");


	stats_size 			= media->get_media_stats(r->pool, handle, source, NULL, 0);
	if ( stats_size <= 0){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "HLS: failed to get media stats size");
		source->close(handle, 0);
		return HTTP_MOVED_PERMANENTLY;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: got media stats size");

	stats_buffer		= (char*)apr_pcalloc(r->pool, stats_size);
	if ( !stats_buffer ){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "HLS: failed to alloc media stats buffer (%d bytes requested)", (int)stats_size);
		source->close(handle, 0);
		return HTTP_INSUFFICIENT_STORAGE;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: allocated space for media stats");


	stats_size 				= media->get_media_stats(r->pool, handle, source, stats_buffer, stats_size);
	if ( stats_size <= 0){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "HLS: failed to get media stats data");
		source->close(handle, 0);
		return HTTP_MOVED_PERMANENTLY;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: got media stats data");

	data_size 			= media->get_media_data(r->pool, handle, source, stats_buffer, piece, NULL, 0);
	if (data_size <= 0){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "HLS: failed to get media data size");
		source->close(handle, 0);
		return HTTP_MOVED_PERMANENTLY;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: got media data size");


	data_buffer 		= (media_data_t*)apr_pcalloc(r->pool, data_size);
	if ( !data_buffer ){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "HLS: failed to alloc media data (requested size %d bytes)", (int) data_size);
		source->close(handle, 0);
		return HTTP_INSUFFICIENT_STORAGE;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: allocated media data buffer");

	get_data_start = get_clock();
	data_size 			= media->get_media_data(r->pool, handle, source, stats_buffer, piece, data_buffer, data_size);
	if (data_size <= 0){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "HLS: failed to get media data");
		source->close(handle, 0);
		return HTTP_MOVED_PERMANENTLY;
	}
	get_data_stop = get_clock();

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: fill media data buffer");


	muxed_size = mux_to_ts(stats_buffer, data_buffer, NULL, 0);
	if ( muxed_size <= 0 ){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "HLS: failed to get multiplex size");
		source->close(handle, 0);
		return HTTP_EXPECTATION_FAILED;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: got multiplex size");

	muxed_buffer = (char*)apr_pcalloc(r->pool, muxed_size);
	if ( !muxed_buffer ){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "HLS: failed to alloc multiplex buffer (requested size %d bytes)", (int)muxed_size);
		source->close(handle, 0);
		return HTTP_INSUFFICIENT_STORAGE;
	}

	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: allocate multiplex buffer");

	muxed_size = mux_to_ts(stats_buffer, data_buffer, muxed_buffer, muxed_size);
	if ( muxed_size <= 0 ){
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "HLS: failed to fill multiplex buffer");
		source->close(handle, 0);
		return HTTP_EXPECTATION_FAILED;
	}
	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: filled multiplex buffer");

	ap_set_content_type(r, "video/mp2t");
	ap_set_content_length(r, muxed_size);
	ap_rwrite( muxed_buffer, muxed_size, r);
	ap_finalize_request_protocol(r);

	source->close(handle, 0);
	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: data source closed");

	stop = get_clock();

//		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "time required to send .ts responce = %lf, get data takes %lf", (stop - start), (get_data_stop - get_data_start));

	return DONE;
}
/*--------------------------------------------------------------------------*/
/*                                                                          */
/* Now we declare our content handlers, which are invoked when the server   */
/* encounters a document which our module is supposed to have a chance to   */
/* see.  (See mod_mime's SetHandler and AddHandler directives, and the      */
/* mod_info and mod_status examples, for more details.)                     */
/*                                                                          */
/* Since content handlers are dumping data directly into the connection     */
/* (using the r*() routines, such as rputs() and rprintf()) without         */
/* intervention by other parts of the server, they need to make             */
/* sure any accumulated HTTP headers are sent first.  This is done by       */
/* calling send_http_header().  Otherwise, no header will be sent at all,   */
/* and the output sent to the client will actually be HTTP-uncompliant.     */
/*--------------------------------------------------------------------------*/
/*
 * Sample content handler.  All this does is display the call list that has
 * been built up so far.
 *
 * The return value instructs the caller concerning what happened and what to
 * do next:
 *  OK ("we did our thing")
 *  DECLINED ("this isn't something with which we want to get involved")
 *  HTTP_mumble ("an error status should be reported")
 */
static int hls_quick_handler(request_rec *r, int lookup_uri)
{
    char *filename;
    int uri_len;
    char* pure_filename;
    int i,k;

    media_handler_t* 	media;
    file_source_t*   	source;
    file_handle_t* 		handle;
    media_stats_t* 		stats;
    int 				piece;
    double start;
    double stop;
    double get_data_start;
    double get_data_stop;


    int use_http = get_allow_http() && (!is_localhost(r->unparsed_uri));
    
    uri_len= strlen(r->unparsed_uri);

    if ( !(use_http) && is_localhost(r->unparsed_uri) ) {
	int prefix_len = 0;
	char* r_unparsed_uri = r->unparsed_uri;
	r_unparsed_uri += 10;

	if (get_data_path() == NULL){
		prefix_len = strlen( r->server->path);
	} else{
		prefix_len = strlen( get_data_path() );
	}

	filename = apr_pcalloc(r->pool, uri_len + 1 + prefix_len);
	
	if (get_data_path() == NULL){
		strcpy(filename, r->server->path);
		strcat(filename, r_unparsed_uri);
	}else{
		strcpy(filename, get_data_path());
		strcat(filename, r_unparsed_uri);
	}

    	int path_len;
	path_len = strlen(filename);

        if (path_len > 4 && filename[path_len-3] == '.' && filename[path_len-2] == 't' && filename[path_len-1] == 's') {
		prefix_len = get_real_filename(filename);
	} else {
		remove_last_extension(filename);
   	}
	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: check local file : %s", filename);
       FILE *file;
       if (!(file = fopen(filename, "r"))) {
         use_http = 1;
       } else {
         fclose(file);
       }
   }

   	ap_log_error(APLOG_MARK, APLOG_WARNING, get_log_level(), r->server, "HLS: unparsed uri %s, use http=%d", r->unparsed_uri, (int)use_http);


   filename = r->unparsed_uri;

    if (uri_len > 4 && filename[uri_len-3] == '.' && filename[uri_len-2] == 't' && filename[uri_len-1] == 's') {
    	return process_ts(r, lookup_uri, use_http);
    }

	if (uri_len > 5 && filename[uri_len-5] == '.' && filename[uri_len-4] == 'm' && filename[uri_len-3] == '3'
												  && filename[uri_len-2] == 'u' && filename[uri_len-1] == '8') {
		return process_m3u8(r, lookup_uri, use_http);
	}

    return DECLINED;
}
//void set_server_pool(apr_pool_t* p);

/*--------------------------------------------------------------------------*/
/*                                                                          */
/* Which functions are responsible for which hooks in the server.           */
/*                                                                          */
/*--------------------------------------------------------------------------*/
/*
 * Each function our module provides to handle a particular hook is
 * specified here.  The functions are registered using
 * ap_hook_foo(name, predecessors, successors, position)
 * where foo is the name of the hook.
 *
 * The args are as follows:
 * name         -> the name of the function to call.
 * predecessors -> a list of modules whose calls to this hook must be
 *                 invoked before this module.
 * successors   -> a list of modules whose calls to this hook must be
 *                 invoked after this module.
 * position     -> The relative position of this module.  One of
 *                 APR_HOOK_FIRST, APR_HOOK_MIDDLE, or APR_HOOK_LAST.
 *                 Most modules will use APR_HOOK_MIDDLE.  If multiple
 *                 modules use the same relative position, Apache will
 *                 determine which to call first.
 *                 If your module relies on another module to run first,
 *                 or another module running after yours, use the
 *                 predecessors and/or successors.
 *
 * The number in brackets indicates the order in which the routine is called
 * during request processing.  Note that not all routines are necessarily
 * called (such as if a resource doesn't have access restrictions).
 * The actual delivery of content to the browser [9] is not handled by
 * a hook; see the handler declarations below.
 */
static void hls_register_hooks(apr_pool_t *p)
{
	set_allow_wav(1);
	set_allow_mp3(1);
	set_allow_http(1);
	set_encode_audio_bitrate(128000);
	set_encode_audio_codec(1);
	set_logo_filename(NULL);
	set_segment_length(10);
	set_allow_redirect(0);

	//set_server_pool(p);
	set_log_level(9);

	set_data_path(NULL);

    ap_hook_quick_handler(hls_quick_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

const char* hls_option_audio_encoding_bitrate(cmd_parms *cmd, void *cfg, const char *arg){
	set_encode_audio_bitrate(atoi(arg));

	return NULL;
}

const char* hls_option_audio_encoding_codec(cmd_parms *cmd, void *cfg, const char *arg){
	if(!strcasecmp(arg, "mp3")){
		set_encode_audio_codec(1);
	}
	set_encode_audio_codec(1);

	return NULL;
}

const char* hls_option_logo_filename(cmd_parms *cmd, void *cfg, const char *arg){
	set_logo_filename(arg);
	return NULL;
}

const char* hls_option_allow_http(cmd_parms *cmd, void *cfg, const char *arg){
	if(!strcasecmp(arg, "yes")) set_allow_http(1);
	    else set_allow_http(0);
	return NULL;
}

const char* hls_option_allow_redirect(cmd_parms *cmd, void *cfg, const char *arg){
	if(!strcasecmp(arg, "yes")) set_allow_redirect(1);
	    else set_allow_redirect(0);
	return NULL;
}


const char* hls_option_allow_wav(cmd_parms *cmd, void *cfg, const char *arg){
	if(!strcasecmp(arg, "yes")) set_allow_wav(1);
	    else set_allow_wav(0);
	return NULL;
}

const char* hls_option_allow_mp3(cmd_parms *cmd, void *cfg, const char *arg){
	if(!strcasecmp(arg, "yes")) set_allow_mp3(1);
	    else set_allow_mp3(0);
	return NULL;
}

const char* hls_option_data_path(cmd_parms *cmd, void *cfg, const char *arg){
	set_data_path(arg);
	return NULL;
}

const char* hls_option_segment_length(cmd_parms *cmd, void *cfg, const char *arg){
	set_segment_length(atoi(arg));
	return NULL;
}

const char* hls_option_log_level(cmd_parms *cmd, void *cfg, const char *arg){
	set_log_level(atoi(arg));
	return NULL;
}

/*--------------------------------------------------------------------------*/
/*                                                                          */
/* All of the routines have been declared now.  Here's the list of          */
/* directives specific to our module, and information about where they      */
/* may appear and how the command parser should pass them to us for         */
/* processing.  Note that care must be taken to ensure that there are NO    */
/* collisions of directive names between modules.                           */
/*                                                                          */
/*--------------------------------------------------------------------------*/
/*
 * List of directives specific to our module.
 */
static const command_rec hls_cmds[] =
{
	AP_INIT_TAKE1(  "AudioEncodingBitrate", hls_option_audio_encoding_bitrate, NULL, OR_OPTIONS, "Audio bitrate for internal audio encoder (in kbps)" ),
	AP_INIT_TAKE1(  "AudioEncodingCodec",   hls_option_audio_encoding_codec,   NULL, OR_OPTIONS, "Audio codec used for internal encoding. Currently support only one codec 1 - mp3" ),
	AP_INIT_TAKE1(  "LogoFilename",         hls_option_logo_filename,  		   NULL, OR_OPTIONS, "H264 Video file in AnnexB form" ),
	AP_INIT_TAKE1(  "AllowWAV", 			hls_option_allow_wav, 		  	   NULL, OR_OPTIONS, "Allow WAV files to process" ),
	AP_INIT_TAKE1(  "AllowMP3", 			hls_option_allow_mp3, 		  	   NULL, OR_OPTIONS, "Allow MP3 files to process" ),
	AP_INIT_TAKE1(  "AllowHTTP", 			hls_option_allow_http, 		  	   NULL, OR_OPTIONS, "Allow HTTP routing" ),
	AP_INIT_TAKE1(  "SegmentLength", 		hls_option_segment_length, 	 	   NULL, OR_OPTIONS, "Segment length in seconds" ),
	AP_INIT_TAKE1(  "AllowRedirect", 		hls_option_allow_redirect, 	 	   NULL, OR_OPTIONS, "Allow redirect for HTTP request to remote content" ),
	AP_INIT_TAKE1(  "HLSLogLevel", 			hls_option_log_level, 		 	   NULL, OR_OPTIONS, "Setup log level for HLS plugin" ),
	AP_INIT_TAKE1(  "HLSDataPath", 			hls_option_data_path, 		 	   NULL, OR_OPTIONS, "Data path for HLS plugin" ),


    {NULL}
};
/*--------------------------------------------------------------------------*/
/*                                                                          */
/* Finally, the list of callback routines and data structures that provide  */
/* the static hooks into our module from the other parts of the server.     */
/*                                                                          */
/*--------------------------------------------------------------------------*/
/*
 * Module definition for configuration.  If a particular callback is not
 * needed, replace its routine name below with the word NULL.
 */
module AP_MODULE_DECLARE_DATA hls_module =
{
    STANDARD20_MODULE_STUFF,
    NULL,    /* per-directory config creator */
    NULL,     /* dir config merger */
    NULL, /* server config creator */
    NULL,  /* server config merger */
    hls_cmds,                 /* command table */
    hls_register_hooks,       /* set up other request processing hooks */
};
