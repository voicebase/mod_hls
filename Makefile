all: debug

debug:
	gcc hls_file.c hls_media_mp3.c hls_media_wav.c hls_mux.c test.c mod_conf.c -o mod-hls -O0 -g3 -Wall -lcurl

release:
	gcc hls_file.c hls_media_mp3.c hls_media_wav.c hls_mux.c test.c mod_conf.c -o mod-hls -O3 -Wall -lcurl

clean:
	rm -f mod-hls