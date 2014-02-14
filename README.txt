This apache module was created for Voicebase Inc. by Alexander Ustinov (alexander@voicebase.com).
If you have any questions you can ask me via email.
All Rights Reserved by Voicebase Inc. (c) 2013.

Module mod_hls add ability to stream HLS for apache.
For WAV files it encode content to HLS and send it to the client on fly.
In current version used mp3lame encoder to encode audio.

To install library place module to 
'/usr/lib/apache2/modules' or other folder where stored apache modules for your operating system and apache installation.

Then add hls.conf to the folder with configurations of apache modules

The content of hls.conf
======================cut here===================
<IfModule mod_hls.c>
AddHandler voicebase-m3u8-handler .m3u8
AddHandler voicebase-ts-handler .ts
</IfModule>
======================cut here===================

Also you have to add hls.load file to the same folder as hls.conf

The content of hls.load
======================cut here===================
LoadModule hls_module /usr/lib/apache2/modules/mod_hls.so
======================cut here===================

Change the path to shared library to place where it is.

Also you have to add several configuration options to apache configuration file:

this is example how it is looks like in apache config file:
======================sample start=======================
# hls module configuration start

AllowWAV yes
AllowMP3 yes
AllowHTTP yes
AudioEncodingBitrate 64000
AudioEncodingCodec mp3
LogoFilename "/var/www/html/media/logo.h264"
SegmentLength 5
AllowRedirect no
HLSLogLevel 0
HLSDataPath "/var/www/html/media/"


#hls module configuration stop
======================sample stop========================

AllowWAV option allows to convert wav files to HLS. If option set to 1 this is enabled.

AudioEncodingBitrate option allow to set output bitrate of mp3 data. Note: this is audio encoding data and it is less then real bitrate because the data wrapped into MPEG2-TransportStream and 
the JWPlayer has requirement that every HLS segment contains at least one frame of video.

AudioEncodingCodec option allow to set encoding format. Currently supported value only 'mp3'.

LogoFilename option allows to set video data required by JWplayer for playback. Video data MUST BE in format described in h264 Annex. B and MUST contain at least one frame.
This option MUST NOT be null and ALWAYS has to point to existed file.

The sample ffmpeg command line to get LogoFilename data in required format:
ffmpeg -loop 1 -i logo.png -vcodec libx264 -r 20 -t 0.5 -bf 0 -vbsf h264_mp4toannexb logo.h264


After installing plugin you can use it this way:
To access HLS stream use:
http://yourservername/wav_folder/wav_name.wav.m3u8

To get one Transport Stream segment you can use this way:
http://yourservername/wav_folder/wav_name.wav_NNN.ts
where NNN is the segment number NOT the letters.




