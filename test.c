#include <stdio.h>
#include <stdlib.h>

#include "curl/curl.h"

#define SERVER_TEST

#ifdef SERVER_TEST

#include "hls_file.h"
#include "hls_media.h"
#include "hls_mux.h"
#include "mod_conf.h"

#include "lame.h"

static char* get_pure_filename(char* filename){
    int len = strlen(filename);
    int pos = len - 1;
    while (pos >= 0 && filename[pos]!='/')
    	--pos;
    return &filename[pos + 1];
}

void generate_playlist_test(char* filename, char* playlist){
	media_handler_t* 	media;
	file_source_t*   	source;
	file_handle_t* 		handle;
	media_stats_t* 		stats;
	int 				piece;
	int 				stats_size;
	char* 				stats_buffer;
	int 				source_size;
	char*				pure_filename;
	FILE* f;

	media  = get_media_handler(filename);
	if ( !media )
		return;

	source_size = get_file_source(NULL, filename, NULL, 0);

	source 	= (file_source_t*)malloc(source_size);
	if ( !source )
		return ;

	source_size  = get_file_source(NULL, filename, source, source_size);
	if ( source_size <= 0 )
		return ;

	handle 	= (char*)malloc(source->handler_size);
	if ( !handle )
		return ;

	if ( !source->open(source, handle, filename) )
		return ;

	stats_size 			= streammedia->get_media_stats(handle, source, NULL, 0);
	stats_buffer		= (char*)malloc(stats_size);

	if ( !stats_buffer ){
		source->close(handle);
		return ;
	}

	stats_size 				= media->get_media_stats(handle, source, stats_buffer, stats_size);

	pure_filename = get_pure_filename(filename); //get only filename without any directory info

	if (pure_filename){
		int playlist_size 		= generate_playlist(stats_buffer, pure_filename, NULL, 0, NULL);
		char* playlist_buffer 	= (char*)malloc( playlist_size);
		if ( !playlist_buffer ){
			source->close(handle);
			return;
		}

		playlist_size 			= generate_playlist(stats_buffer, pure_filename, playlist_buffer, playlist_size, NULL);
		if (playlist_size <= 0){
			source->close(handle);
			return ;
		}

		f = fopen(playlist, "wb");
		if (f){
			fwrite(playlist_buffer, 1, playlist_size, f);
			fclose(f);
		}

		if (playlist_buffer)
			free(playlist_buffer);

	}

	source->close(handle);

	if (stats_buffer)
		free(stats_buffer);

	if (handle)
		free(handle);

	if (source)
		free(source);
}

void generate_piece(char* filename, char* out_filename, int piece){
	media_handler_t* 	media;
	file_source_t*   	source;
	file_handle_t* 		handle;
	media_stats_t* 		stats;
	int 				stats_size;
	char* 				stats_buffer;
	int 				source_size;
	char*				pure_filename;
	int 				data_size;
	media_data_t* 		data_buffer;
	int 				muxed_size;
	char* 				muxed_buffer;
	FILE* f;

	media  = get_media_handler(filename);
	if ( !media )
		return ;

	source_size = get_file_source(NULL, filename, NULL, 0);

	source 	= (file_source_t*)malloc(source_size);
	if ( !source )
		return ;

	source_size  = get_file_source(NULL, filename, source, source_size);
	if ( source_size <= 0 )
		return ;

	handle 	= (char*)malloc(source->handler_size);
	if ( !handle )
		return ;

	if ( !source->open(source, handle, filename) )
		return ;

	stats_size 			= media->get_media_stats(handle, source, NULL, 0);
	if ( stats_size <= 0){
		source->close(handle);
		return ;
	}

	stats_buffer		= (char*)malloc( stats_size);
	if ( !stats_buffer ){
		source->close(handle);
		return ;
	}

	stats_size 				= media->get_media_stats(handle, source, stats_buffer, stats_size);
	if ( stats_size <= 0){
		source->close(handle);
		return ;
	}

	data_size 			= media->get_media_data(handle, source, stats_buffer, piece, NULL, 0);
	if (data_size <= 0){
		source->close(handle);
		return ;
	}

	data_buffer 		= (media_data_t*)malloc(data_size);
	if ( !data_buffer ){
		source->close(handle);
		return ;
	}

	data_size 			= media->get_media_data(handle, source, stats_buffer, piece, data_buffer, data_size);
	if (data_size <= 0){
		source->close(handle);
		return ;
	}

	muxed_size = mux_to_ts(stats_buffer, data_buffer, NULL, 0);
	if ( muxed_size <= 0 ){
		source->close(handle);
		return ;
	}

	muxed_buffer = (char*)malloc(muxed_size);
	if ( !muxed_buffer ){
		source->close(handle);
		return ;
	}

	muxed_size = mux_to_ts(stats_buffer, data_buffer, muxed_buffer, muxed_size);
	if ( muxed_size <= 0 ){
		source->close(handle);
		return ;
	}

	f = fopen(out_filename, "wb");
	if (f){
		fwrite(muxed_buffer, 1, muxed_size, f);
		fclose(f);
	}

	source->close(handle);

	if (source)
		free(source);

	if (handle)
		free(handle);

	if (stats_buffer)
		free(stats_buffer);

	if (data_buffer)
		free(data_buffer);

	if (muxed_buffer)
		free(muxed_buffer);
}

#endif

typedef struct stream_t{
	char* buf;
	int pos;
	int size;
	int alloc_buffer;
} stream_t;

static size_t fileWriteCallback(void *buffer, size_t size, size_t nmemb, void *stream)
{
	int bs = size * nmemb;
	stream_t* s = (stream_t*)stream;
	if (s->alloc_buffer){
		if (!s->buf || s->size == 0) {
			int cs = bs + 100;
			if (cs < 10000)
				cs = 10000;

			s->buf 	= (char*)malloc(cs);
			s->size = cs;
			s->pos 	= 0;
		}else{
			if (s->pos + bs >= s->size){
				s->buf = (char*)realloc(s->buf, s->size * 2);
				s->size *= 2;
			}
		}
	}

	if (s->buf && s->size > s->pos + bs){
		memcpy(s->buf + s->pos, buffer, bs);
		s->pos += bs;
	}

	return bs;

}

/// \return number of bytes processed
static size_t headerfunc_short ( char * ptr, size_t size, size_t nmemb, int* error_code )
{
	if (!strncmp ( ptr, "HTTP/1.1", 8 )) {
		*error_code   = atoi ( ptr + 9 );
	}
	return nmemb * size;
}



int download_file_to_mem(char* http_url, char** playlist){
	CURL *curl;
	CURLcode res = CURLE_CHUNK_FAILED;

	char Buf[1024];
	char range[1024];
	int offset = 30000;
	int size = 4096;

	int http_error_code = 0;
	stream_t stream = {0};

	if (playlist)
		stream.alloc_buffer = 1;
	else
		stream.alloc_buffer = 0;

	curl = curl_easy_init();
	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, http_url);

		curl_easy_setopt ( curl, CURLOPT_WRITEFUNCTION, fileWriteCallback );
		curl_easy_setopt ( curl, CURLOPT_WRITEDATA, &stream );
		curl_easy_setopt ( curl, CURLOPT_HEADERFUNCTION, headerfunc_short );
		curl_easy_setopt ( curl, CURLOPT_HEADERDATA, &http_error_code );
		curl_easy_setopt ( curl, CURLOPT_VERBOSE, 1 );

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);
		/* Check for errors */
		if(res != CURLE_OK){
			fprintf(stderr, "curl_easy_perform() failed: %s\n", 	curl_easy_strerror(res));
		}

		curl_easy_cleanup(curl);
	}

	if (playlist){
		*playlist = stream.buf;
	}
	return res;
}

int get_segments_count(char* playlist){
	int c = 0;
	int i = 0;
	int len = strlen(playlist);
	for(i = 0; i < len - 1; ++i){
		if (playlist[i] == 0x0A && playlist[i+1] != '#'){
			++c;
		}
	}
	return c;
}

void get_segment_name(char* segment_name, int segment_name_size, char* playlist, int segment_num){
	int c = 0;
	int i = 0;
	int k;
	int len = strlen(playlist);
	for(i = 0; i < len - 1; ++i){
		if (playlist[i] == 0x0A && playlist[i+1] != '#'){
			if (c == segment_num){
				k = 0;
				while( i + k + 1 < len && playlist[i+k+1] != 0x0A && k < segment_name_size ){
					segment_name[k] = playlist[i+k+1];
					++k;
				}
				segment_name[k] = 0;

				break;
			}
			++c;
		}
	}
}

void get_file_url(char* file_url, int file_url_size, char* http_url, char* segment_name){
	int pos = strlen(http_url);
	int i;
	while(pos >= 0 && http_url[pos] != '/'){
		--pos;
	}
	for(i = 0; i < pos; ++i){
		file_url[i] = http_url[i];
	}
	file_url[pos] = 0;
	strcat(file_url, "/");
	strcat(file_url, segment_name);
}

void process_hls_stream(char* http_url){
	char* playlist = NULL;
	int i;
	while (download_file_to_mem(http_url, &playlist) != CURLE_OK);

	if (playlist){
		int segments_count = get_segments_count(playlist);

		for(i=0; i < segments_count; ++i){
			char file_url[2048];
			char segment_name[2048];

			get_segment_name(segment_name, sizeof(segment_name), playlist, i);

			get_file_url(file_url, sizeof(file_url), http_url, segment_name);
			while( download_file_to_mem(file_url, NULL) != CURLE_OK);
		//	usleep(1000000);
		}

		free(playlist);
	}

}

//typedef struct size_error_t{
//	size_t size;
//	int error;
//}size_error_t;
//
///// \return number of bytes processed
//static size_t headerfunc_content_length ( char * ptr, size_t size, size_t nmemb, size_error_t* se )
//{
//	if (!strncmp ( ptr, "HTTP/1.1", 8 )) {
//		 se->error  = atoi ( ptr + 9 );
//	}
//	if (!strncmp ( ptr, "Content-Length:", 15 )) {
//		 se->size  = atoi ( ptr + 16 );
//	}
//
//	return nmemb * size;
//}
//
//CURLcode curl_get_resource_size(char* url, size_t* size, int* error_code){
//	 CURLcode res = CURLE_CHUNK_FAILED;
//	 CURL* ctx = curl_easy_init();
//	 if (ctx){
//		 size_error_t se;
//		 struct curl_slist *headers = NULL;
//
//		 headers = curl_slist_append(headers,"Accept: */*");
//		 if (headers){
//			 curl_easy_setopt(ctx,CURLOPT_HTTPHEADER, 	headers );
//			 curl_easy_setopt(ctx,CURLOPT_NOBODY,		1 );
//			 curl_easy_setopt(ctx,CURLOPT_URL,			url );
//			 curl_easy_setopt(ctx,CURLOPT_NOPROGRESS,	1 );
//			 curl_easy_setopt(ctx, CURLOPT_HEADERFUNCTION, headerfunc_content_length );
//			 curl_easy_setopt(ctx, CURLOPT_HEADERDATA,	&se );
//			 curl_easy_setopt(ctx, CURLOPT_VERBOSE, 0 );
//
//			 res = curl_easy_perform(ctx);
//			 curl_easy_cleanup(ctx);
//
//			 if (res == CURLE_OK){
//				 if (size){
//					 *size = se.size;
//				 }
//				 if (error_code){
//					 *error_code = se.error;
//				 }
//			 }
//		 }
//	 }
//	 return res;
//}
//
//typedef struct range_request_t{
//	char* buf;
//	int pos;
//	int size;
//} range_request_t;
//
//static size_t range_request_func(void *buffer, size_t size, size_t nmemb, void *stream)
//{
//	int bs = size * nmemb;
//	range_request_t* rr = (stream_t*)stream;
//
//	if (rr->buf && rr->size >= rr->pos + bs){
//		memcpy(rr->buf + rr->po s, buffer, bs);
//		rr->pos += bs;
//	}
//
//	return bs;
//
//}
//
//CURLcode curl_get_data(char* http_url, char* buffer, long long offset, long long requested_size, long long * received_size){
//	CURL *curl;
//	CURLcode res = CURLE_CHUNK_FAILED;
//	char range_str[128];
//	range_request_t rr;
//
//	rr.buf = buffer;
//	rr.pos = 0;
//	rr.size = requested_size;
//
//	curl = curl_easy_init();
//	if(curl) {
//		curl_easy_setopt(curl, CURLOPT_URL, http_url);
//
//		snprintf(range_str, sizeof(range_str), "%lld-%lld", (long long)offset, (long long)(offset + requested_size - 1));
//
//
//		curl_easy_setopt ( curl, CURLOPT_WRITEFUNCTION, range_request_func );
//		curl_easy_setopt ( curl, CURLOPT_WRITEDATA, &rr );
//		curl_easy_setopt ( curl, CURLOPT_VERBOSE, 0 );
//		curl_easy_setopt ( curl, CURLOPT_RANGE, range_str);
//
//		/* Perform the request, res will get the return code */
//		res = curl_easy_perform(curl);
//		/* Check for errors */
//		if(res != CURLE_OK){
//			fprintf(stderr, "curl_easy_perform() failed: %s\n", 	curl_easy_strerror(res));
//		}else{
//			if (received_size)
//				*received_size = rr.pos;
//		}
//
//		curl_easy_cleanup(curl);
//	}
//
//	return res;
//}

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
	}

	char convert_str_to_char(char c1, char c2){
		return (hex_to_int(c1) << 4) | hex_to_int(c2);
	}

	char* get_arg_value(/*request_rec * r,*/ char* args, char* key){
		int i,j;
		int args_len = strlen(args);
		int key_len= strlen(key);
		//char* result = apr_pcalloc(r->pool, args_len + 1);
		char* result = (char*)malloc(args_len + 1);
		int level = 0;
		memset(result, 0, args_len + 1);

		for(i = 0; i < args_len; ++ i){
			int found = 0;
			if (i == 0){
				found = (strncmp(&args[ i + j ], key, key_len) == 0) ? 1 : 0;
			}else{
				if (args[i - 1] == '&'){
					found = (strncmp(&args[ i + j ], key, key_len) == 0) ? 1 : 0;
				}
			}
			if (found){
				j = 0;
				i+=key_len+1;
				if (i + j + 3< args_len && args[i+j] == '%' && args[i+j+1] == '2' && args[i+j+2] == '2')
					i+=3;

				while ( i + j < args_len && args[j] != '&'){
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

int main (int argc, char* argv[]){

/*	char buffer[1024];
	size_t 	size 	= 0;
	long long bytes_read = 0;
	int 	error 	= 0;
	char* url = "http://rs1614.freeconferencecall.com/fcc/cgi-bin/play.mp3/7605696000-927848-105.mp3/voicebase.mp3";

	CURLcode res = curl_get_resource_size(url, &size, &error);
	if (res == CURLE_OK && size != 36429294){
		printf("Error while getting size\n");
	}

	res = curl_get_data(url, buffer, 0, 256, &bytes_read);

	if (buffer[0] == 'I' && buffer[1] == 'D' && buffer[2] == '3'){
		printf("Found id3 tag in very begining\n");
	}
*/
	//test curl range downloading procedure

	//test curl content size

#ifndef SERVER_TEST

//	if (argc != 2){
//		printf("Usage: test hls_url\n");
//		exit(0);
//	}

//	int i;
//	double amount = 0;
//
//	for(i = 0; i < 40; ++i){
//		amount = amount * 1.1 + 36000;
//	}
//
//	printf("%lf\n", amount / (12.0 * 19.0));

	process_hls_stream(argv[1]);

#else

//	char* res = get_arg_value("source=%22http://192.168.0.105/test_playlist.m3u8%22","source");
//	printf("source=%s\n", res);
//	free(res);

	char filename[2][255] = { //"/home/alex/work/download/contractor/INSTANT-CONTRACTOR.wav",
							  //"/home/alex/work/download/problem/249697_2012082202_59_Roxana_QRJvtcDNInmjQVT.wav",
							  "/home/alex/work/tmp/7605696000-927848-105.mp3",
			  	  	  	  	  //"/home/alex/work/tmp/32000.mp3",
							 NULL	};
	int i = 0;

	set_encode_audio_bitrate(64000);
	set_allow_wav(1);
	set_allow_mp3(1);
	set_encode_audio_codec(1);
	set_segment_length(5);
	//set_logo_filename("/home/alex/work/tmp/logo.h264");
	set_logo_filename(NULL);


	generate_playlist_test(filename[0], "/home/alex/work/tmp/test_playlist.m3u8");

	for(i = 0; i < 450; ++i)

	{
		char tmp[1024];
		sprintf(tmp, "%s_%d.ts",filename[0], i);
		generate_piece(filename[0], tmp, i);
	}
#endif
	return 0;
}
