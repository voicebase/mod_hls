LAME_FOLDER=/home/alex/work/SGPUA/mod_example/lame-3.99.5
CURL_FOLDER=/home/alex/work/curl-7.29.0
TOOL=apxs2
$TOOL -c -i -I $LAME_FOLDER/include/ -I $CURL_FOLDER/include -L $CURL_FOLDER/lib/.libs/ -L $LAME_FOLDER/libmp3lame/.libs/ -l mp3lame -l curl -l rt -l idn mod_hls.c hls_media_wav.c hls_media_mp3.c hls_mux.c hls_file.c mod_conf.c
