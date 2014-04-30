/*
 * Implementation of mp3 media
 * Copyright (c) 2012-2013 Voicebase Inc.
 *
 * Authors: Alexander Ustinov, Pomazan Nikolay
 * email: alexander@voicebase.com, pomazannn@gmail.com
 *
 * This file is the part of the mod_hls apache module
 *
 * This program is free software, distributed under the terms of
 * the GNU Lesser General Public License version 3. See the LICENSE file
 * at the top of the source tree.
 *
 */

#include "hls_media.h"
#include "mod_conf.h"
#include "lame/lame.h"
#include "hls_mux.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


const int SamplingFrequencies[16]={96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
									  16000, 12000, 11025, 8000, 7350, -1, -1, -1};
const int ChannelConfigurations[16]={0, 1, 2, 3, 4, 5, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1};

int if_subbox(char* box_type) {
	int yeap=0;
	char types_with_subboxes[][4]={ {'m','o','o','v'},/* {'i','o','d','s'},*/ {'t','r','a','k'}, {'m','d','i','a'},
									{'m','i','n','f'}, {'d','i','n','f'}, /*{'d','r','e','f'},*/ {'s','t','b','l'},
									/*{'s','t','s','d'}, {'m','p','4','a'}, {'e','s','d','s'},*//* ES Descriptor and Decoder, */
									/*{'a','v','c','1'},*/ {'u','d','t','a'}, {0,0,0,0}};
	int i=0;
	int counter;
	while (types_with_subboxes[i][0]!=0 && types_with_subboxes[i][1]!=0 && types_with_subboxes[i][2]!=0 && types_with_subboxes[i][3]!=0 && yeap==0) {
		counter=0;
		for(int j=0; j<4; j++) {
			if (types_with_subboxes[i][j]==box_type[j])
				counter++;
			if (counter == 4)
				yeap=1;
		}
		i++;
	}
	return yeap>0 ? 1:0;
}
typedef struct {
	int first_chunk;
	int samples_per_chunk;
	int sample_description_index;
} sample_to_chunk;

typedef struct MP4_BOX_s{
	struct MP4_BOX_s *parent;
	int child_count;
	struct MP4_BOX_s *child_ptr[15];
	char box_type[4];
	int box_size;
	int box_first_byte;
} MP4_BOX;

struct MP4_BOX_s* add_item(MP4_BOX* self, int box_size, char* box_type, int first_byte) {
	MP4_BOX* new_item;
	new_item = (MP4_BOX*) malloc (sizeof(MP4_BOX));
	new_item->parent = self;
	new_item->box_size = box_size;
	memcpy(new_item->box_type, box_type, 4);
	new_item->box_first_byte = first_byte;
	new_item->child_count=0;
	return new_item;
}

//OLD VERSION
void scan_one_level(FILE* mp4, MP4_BOX* head) {
	int c;
	int child_count=0;
	int byte_counter = head->box_first_byte;
	int first_byte = 0;//head->box_first_byte;
	int box_size = 0;
	char box_type[4];
	if (head->box_first_byte != 0) {
		fseek(mp4, head->box_first_byte+8, SEEK_SET);
		first_byte = head->box_first_byte+8;
	}
	else
		fseek(mp4, head->box_first_byte, SEEK_SET);
	while(byte_counter < (head->box_size + head->box_first_byte-9)) {
		first_byte += box_size;
		if(box_size!=0)
			fseek(mp4, box_size-8, SEEK_CUR);
		fread(&box_size, 4, 1, mp4);
		box_size = ntohl(box_size);
		fread(box_type, 1, 4, mp4);

		head->child_ptr[child_count] = add_item(head, box_size, box_type, first_byte);
		child_count++;
		byte_counter += box_size;

	}
	head->child_count=child_count;
	printf("\ntype - '%.4s'; size - '%d'; child count - '%d'; first byte - '%d'",head->box_type, head->box_size, head->child_count, head->box_first_byte);
	if (child_count>0) {
		for (int count=0; count<head->child_count; count++) {
			if ((c=if_subbox(head->child_ptr[count]->box_type)) > 0)
				scan_one_level(mp4, head->child_ptr[count]);
			else
				printf("\ntype - '%.4s'; size - '%d'; child count - '%d'; first byte - '%d'",head->child_ptr[count]->box_type, head->child_ptr[count]->box_size, head->child_ptr[count]->child_count, head->child_ptr[count]->box_first_byte);
		}
	}
}

//NEW Version
/*
void scan_one_level(FILE* mp4, MP4_BOX* head) {
	int c;
	int child_count=0;
	int first_byte = head->box_first_byte;
	int box_size = 0;
	char box_type[4];

	while(first_byte < (head->box_size + head->box_first_byte-9)) {

		fseek(mp4, first_byte, SEEK_SET);

		fread(&box_size, 4, 1, mp4);
		fread(box_type, 1, 4, mp4);

		box_size = ntohl (box_size);

		child_count++;
		head->child_ptr[child_count] = add_item(&head, box_size, box_type, first_byte+8);

		first_byte   += box_size;

	}
	head->child_count=child_count;

	if (child_count>0) {
		for (int count=0; count<head->child_count; count++) {
			printf("\ntype - '%.4s'; size - '%d'; child count - '%d'; first byte - '%d'",head->child_ptr[count+1]->box_type, head->child_ptr[count+1]->box_size, head->child_ptr[count+1]->child_count, head->child_ptr[count+1]->box_first_byte);

			if ((c=if_subbox(head->child_ptr[count+1]->box_type)) > 0)
				scan_one_level(mp4, head->child_ptr[count+1]);
		}
	}
}
*/

MP4_BOX* mp4_looking(FILE* mp4) {
	fseek (mp4, 0, SEEK_END);
	int fSize;
	fSize = ftell (mp4);
	MP4_BOX* head;
	head = (MP4_BOX*) malloc (sizeof(MP4_BOX));
	head->box_type[0] = 'f';
	head->box_type[1] = 'i';
	head->box_type[2] = 'l';
	head->box_type[3] = 'e';
	head->parent = NULL;
	head->box_first_byte = 0;
	head->child_count=0;
	head->box_size = fSize;
	scan_one_level(mp4, head);
	return head;
}

void i_want_to_break_free(MP4_BOX* root) {
	if(root->child_count>0) {
		for(int i=0; i<root->child_count; i++) {
			i_want_to_break_free(root->child_ptr[i]);
		}
	}
	memset(root, '\0', sizeof(MP4_BOX));
	free(root);
}

int compare_box_type(MP4_BOX* root, char boxtype[5]) {
	for (int i=0; i<4; i++) {
		if (root->box_type[i]!=boxtype[i])
			return 0;
	}
	return 1;
}

MP4_BOX* compare_one_level(MP4_BOX* root, char boxtype[5]) {
	int find_box=0;
	MP4_BOX* box;
	if (root->child_count>0) {
		for(int i=0; i<root->child_count; i++) {
					if (find_box==0) {
						box=root->child_ptr[i];
						find_box=compare_box_type(box, boxtype);
					}
					if (find_box>0)
						break;
				}
	}
 return find_box!=0 ? box:NULL;
}

MP4_BOX* find_box(MP4_BOX* root, char boxtype[5]) {
	MP4_BOX* box=NULL;
	box=compare_one_level(root, boxtype);
	if (box==NULL) {
		if (root->child_count>0) {
			for (int i=0; i<root->child_count; i++) {
				box=find_box(root->child_ptr[i], boxtype);
				if (box!=NULL)
					break;
			}
		}
	}
	return box;
}

int handlerType(FILE* mp4, MP4_BOX* root, char handler[5]) {
	char hdlr_type[4];
	fseek(mp4, root->box_first_byte+16, SEEK_SET); //+16 cause of 'hdlr' syntax.
	fread(hdlr_type, 1, 4, mp4);
	int counter=0;
	for (int i=0; i<4; i++) {
		if(hdlr_type[i]!=handler[i])
			return 0;
	}
	return 1;
}

MP4_BOX* find_necessary_trak(FILE* mp4, MP4_BOX* root, char trak_type[5]) {
	MP4_BOX* moov;
	MP4_BOX* necessary_trak=NULL;
	MP4_BOX* temp_ptr;
	moov=find_box(root, "moov");
	for(int i=0; i<moov->child_count; i++) {
		if (compare_box_type(moov->child_ptr[i],"trak")) {
			temp_ptr=find_box(moov->child_ptr[i], "hdlr");
			if(handlerType(mp4, temp_ptr, trak_type))
				necessary_trak=moov->child_ptr[i];
		}
	}
	return necessary_trak;
}

sample_to_chunk** read_stsc(FILE* mp4, MP4_BOX* stsc, int* stsc_entry_count) {
	int entry_count=0;
	fseek(mp4, stsc->box_first_byte+12, SEEK_SET);
	fread(&entry_count, 4, 1, mp4);
	entry_count=ntohl(entry_count);
	*stsc_entry_count = entry_count;
	sample_to_chunk **stc;
	stc = malloc (sizeof(sample_to_chunk*)*entry_count);
	for (int j=0; j<entry_count; j++)
		stc[j] = (sample_to_chunk*) malloc (sizeof(sample_to_chunk));
	for(int i=0; i<entry_count; i++) {
		fread(&stc[i]->first_chunk,4,1,mp4);
		stc[i]->first_chunk=ntohl(stc[i]->first_chunk);
		fread(&stc[i]->samples_per_chunk,4,1,mp4);
		stc[i]->samples_per_chunk=ntohl(stc[i]->samples_per_chunk);
		fread(&stc[i]->sample_description_index,4,1,mp4);
		stc[i]->sample_description_index=ntohl(stc[i]->sample_description_index);
	}
	return stc;
}

int* read_stco(FILE* mp4, MP4_BOX* stco, int* stco_entry_count) {
	int entry_count=0;
	fseek(mp4, stco->box_first_byte+12, SEEK_SET);
	fread(&entry_count, 4,1,mp4);
	entry_count=ntohl(entry_count);
	*stco_entry_count=entry_count;
	int* stco_data;
	stco_data=(int*) malloc (sizeof(int)*entry_count);
	for (int i=0; i<entry_count; i++) {
		fread(&stco_data[i],4,1,mp4);
		stco_data[i]=ntohl(stco_data[i]);
	}
	return stco_data;
}

int* read_stsz(FILE* mp4, MP4_BOX* stsz, int* stsz_sample_count) {
	int sample_count=0;
	int sample_size=0;
	fseek(mp4, stsz->box_first_byte+12, SEEK_SET);
	fread(&sample_size,4,1,mp4);
	sample_size=ntohl(sample_size);
	fread(&sample_count, 4,1,mp4);
	sample_count=ntohl(sample_count);
	*stsz_sample_count=sample_count;
	int* stsz_data;
	stsz_data=(int*) malloc (sizeof(int)*sample_count);
	if (sample_size==0) {
		for(int i=0; i<sample_count; i++) {
			fread(&stsz_data[i],4,1,mp4);
			stsz_data[i]=ntohl(stsz_data[i]);
		}
	}
	else {
		for (int i=0; i<sample_count; i++)
			stsz_data[i]=sample_size;
	}
	return stsz_data;
}

int tag_size(FILE* mp4) {
	int tagsize=0;
	int tagsize_bytes=0;
	int temp;
	do {
		fread(&temp,1,1,mp4);
		tagsize = tagsize <<7;
		tagsize |= (temp & 127);
		tagsize_bytes += 1;
	} while ((temp&128)>0);
	return tagsize_bytes;
}

int read_esds_flag(FILE* mp4) { //return count of bytes depends of flag
	int flags;
	int count=1;
	fread(&flags,1,1,mp4);
	//streamDependenceflag
	if ((flags & 128) > 0)
		count+=2;
	//URL_Flag
	if ((flags & 64) > 0)
		count+=2;
	//OCR_Streamflag
	if ((flags & 32) > 0)
		count+=2;
	return count;
}

int get_DecoderSpecificInfo(FILE* mp4, MP4_BOX* stsd) {
	/*
	 * stsd:
	 * 		Fullbox - 12 byte
	 * 		entry_count - 4 byte
	 * AudioSampleEntry
	 * 		Box - 8 byte
	 * 		reserverd - 6 byte
	 * 		data_references_index - 2 byte
	 * 		reserved - 8 byte
	 * 		channel_count - 2 byte
	 * 		samplesize - 2 byte
	 * 		pre_defined - 2 byte
	 * 		rezerved - 2 byte
	 * 		samplerate - 4 byte
	 * 	esds
	 * 		Fullbox - 12 byte
	 * 		tag - 1 byte
	 * 		size - (#1)1*N byte (if size <128  N==1), else
													int sizeOfInstance = 0;
													bit(1) nextByte;
													bit(7) sizeOfInstance;
													while(nextByte) {
													bit(1) nextByte;
													bit(7) sizeByte;
													sizeOfInstance = sizeOfInstance<<7 | sizeByte;
													}
	 * 		ES_ID - 2 byte
	 * 		streamDependenceFlag - 1 bit
	 * 		URL_Flag - 1 bit
	 * 		OCR_Streamflag - 1 bit
	 * 		streamPriority - 5 bit
	 * 		if (streamDependenceFlag)
	 * 			dependsOn_ES_ID - 2 byte
	 * 		if (URL_Flag) {
	 *			URLlength - 1 byte
	 * 			URLstring[URLlength] - 1 byte
	 *			}
	 *		if (OCRstreamFlag)
	 *			OCR_ES_Id - 2 byte
	 *	DecoderConfigDescriptor
	 * 		tag - 1 byte
	 * 		size - 1*N byte (#1)
	 * 		ObjectTypeIndication - 1 byte
	 * 		streamtype - 6 bit
	 * 		upstream - 1 bit
	 * 		reserved - 1 bit
	 * 		buffersizeDB - 3 byte
	 * 		maxBitrate - 4 byte
	 * 		avgBitrate - 4 byte
	 * 	DecoderSpecificInfo
	 * 		tag - 1 byte
	 * 		size - 1*N byte (#1)
	 * 		Data[0] - 1 byte
	 * 		Data[1] - 1 byte
	 *
	 *
	 * 	Data[0] & Data [1] - is necessary;
	 */
	int skip_bytes=0; // byte counter before Data[0];
	skip_bytes = 65; //before tag_size;
	fseek(mp4, stsd->box_first_byte+skip_bytes, SEEK_SET);
	skip_bytes += (tag_size(mp4) + 2); //2 - ES_ID
	fseek(mp4,2,SEEK_CUR); // 2 - ES_ID
	skip_bytes += (read_esds_flag(mp4) +1); // 1 - DecoderConfigDescriptor tag
	fseek(mp4,stsd->box_first_byte+skip_bytes,SEEK_SET);
	skip_bytes += (tag_size(mp4) + 13 + 1); // 13 - Sum(ObjectTypeIndication .. avgBitrate); 1 - DecoderSpecificInfo tag
	fseek(mp4,stsd->box_first_byte+skip_bytes,SEEK_SET);
	skip_bytes += tag_size(mp4);
	int data0=0;
	int data1=0;
	fread(&data0,1,1,mp4);
	fread(&data1,1,1,mp4);
	data0 = (data0<<8) | data1;
	// Temporary ignored 5 bytes info;
	return data0;
}

void get_sounddata_to_file(FILE* mp4, MP4_BOX* root) {
	FILE* mp4_sound;
	mp4_sound=fopen("mp4_sound.aac","wb");
	if(mp4_sound==NULL) {
		printf("\nCould'n create mp4_sound.mp3 file\n");
		exit(1);
	}
	MP4_BOX* find_trak;
	MP4_BOX* find_stsc;
	MP4_BOX* find_stco;
	MP4_BOX* find_stsz;
	MP4_BOX* find_stsd;

	find_trak=find_necessary_trak(mp4, root, "soun");
	find_stsc=(find_box(find_trak, "stsc"));
	find_stco=(find_box(find_trak, "stco"));
	find_stsz=(find_box(find_trak, "stsz"));
	find_stsd=(find_box(find_trak, "stsd"));

	int stsc_entry_count=0;
	int stco_entry_count=0;
	int stsz_entry_count=0;

	sample_to_chunk** stsc_data;
	stsc_data = read_stsc(mp4, find_stsc, &stsc_entry_count);

	int* stco_data;
	stco_data = read_stco(mp4, find_stco, &stco_entry_count);

	int* stsz_data;
	stsz_data = read_stsz(mp4, find_stsz, &stsz_entry_count);

	//Test getDecoderSpecificInfo
	int DecSpecInfo=0;
	long long BaseMask=0xFFF10200001FFC;
	DecSpecInfo = get_DecoderSpecificInfo(mp4, find_stsd);
	long long objecttype=0;
	long long frequency_index=0;
	long long channel_count=0;
	long long TempMask=0;
	objecttype = (DecSpecInfo >> 11) - 1;
	frequency_index = (DecSpecInfo >> 7) & 15;
	channel_count = (DecSpecInfo >> 3) & 15;

	objecttype = objecttype << 38;
	frequency_index = frequency_index << 34;
	channel_count = channel_count << 30;
	BaseMask = BaseMask | objecttype | frequency_index | channel_count;

	//read and write chunks
	char* buffer;
	int samples_in_chunk;
	int last_used_stsz_sample_size=0;
	for (int i=0; i<stco_entry_count; i++) {
		fseek(mp4,stco_data[i],SEEK_SET);
		//get sample count from sample_to_chunk
		for(int j=0; j<stsc_entry_count; j++) {
			if (j<stsc_entry_count-1) {
				if ((i+1 >= stsc_data[j]->first_chunk) && (i+1 < stsc_data[j+1]->first_chunk)) {
					samples_in_chunk = stsc_data[j]->samples_per_chunk;
					break;
				}
			}
			else {
				samples_in_chunk = stsc_data[j]->samples_per_chunk;
			}
		}
		//read data from mp4 and write to mp4_sound
		for (int k=last_used_stsz_sample_size; k<last_used_stsz_sample_size+samples_in_chunk; k++) {
			buffer = malloc (stsz_data[k]);
			if(buffer==NULL) {
				printf("\nCouldn't allocate memory for buffer[%d]",k);
				exit(1);
			}
			fread(buffer,stsz_data[k],1,mp4);
			TempMask = BaseMask | ((stsz_data[k] + 7) << 13);
			char for_write=0;
			if(k==0)
				printf("\nTempMask[%d] = %lld\n",k,TempMask);
			for (int g=6; g>=0; g--) {
				for_write=TempMask >> g*8;

				fwrite(&for_write,1,1,mp4_sound);
			}
			fwrite(buffer,stsz_data[k],1,mp4_sound);
			free(buffer);
			if(k==last_used_stsz_sample_size+samples_in_chunk-1) {
				last_used_stsz_sample_size+=samples_in_chunk;
				break;
			}

		}
	}
	//free used memory
	free(stco_data);
	free(stsz_data);
	for (int z=0; z<stsc_entry_count; z++)
		free(stsc_data[z]);
	free(stsc_data);
	fclose(mp4_sound);
}


int get_count_of_traks(MP4_BOX* root, MP4_BOX*** moov_traks) {
	MP4_BOX* moov;
	moov_traks = (MP4_BOX**) malloc (sizeof(MP4_BOX*)*2);
	if(moov_traks==NULL) {
		printf("Couldn't allocate memory for moov_traks");
		exit(1);
	}
	moov = find_box(root, "moov");
	int trak_counter=0;
	for(int i=0; i<moov->child_count; i++) {
		if (compare_box_type(moov->child_ptr[i],"trak")==1) {
			moov_traks[trak_counter]=moov->child_ptr[i];
			trak_counter++;
		}
	}
	return trak_counter;
}

int get_count_of_audio_traks(FILE* mp4, MP4_BOX* root, MP4_BOX*** moov_traks) {
	MP4_BOX* moov;
	moov_traks = (MP4_BOX**) malloc (sizeof(MP4_BOX*)*2);
	if(moov_traks==NULL) {
		printf("Couldn't allocate memory for moov_traks");
		exit(1);
	}
	moov = find_box(root, "moov");
	int trak_counter=0;
	for(int i=0; i<moov->child_count; i++) {
		if (compare_box_type(moov->child_ptr[i],"trak") && handlerType(mp4,find_box(moov->child_ptr[i],"hdlr"),"soun")) {
			moov_traks[trak_counter]=moov->child_ptr[i];
			trak_counter++;
		}
	}
	return trak_counter;
}

int get_audio_codec(FILE* mp4, MP4_BOX* audio_trak) {
	MP4_BOX* stsd;
	stsd = find_box(audio_trak, "stsd");
	char box_type[4];
	fseek(mp4, stsd->box_first_byte+20, SEEK_SET);
	fread(box_type, 1, 4, mp4);
	char codec[4]="mp4a";
	for(int i=0; i<4; i++) {
		if (box_type[i]!=codec[i])
			return 0;
	}
	return 0x0F;
}

int get_nframes(FILE* mp4, MP4_BOX* trak) {
	MP4_BOX* stsz;
	stsz = find_box(trak, "stsz");
	int* data;
	int nframes=0;
	data = read_stsz(mp4, stsz, &nframes);
	free(data);
	return nframes;
}

//Bitrate = 0

void get_pts(FILE* mp4, MP4_BOX* audio_trak, int nframes, int DecoderSpecificInfo,float* pts) {

	int* delta;
	delta = (int*) malloc (sizeof(int)*nframes);
	if (delta==NULL) {
			printf("\nCouldn't allocate memory for pts");
			exit(1);
		}
	MP4_BOX* stts;
	stts = find_box(audio_trak, "stts");
	fseek(mp4, stts->box_first_byte+12, SEEK_SET);
	int stts_entry_count=0;
	fread(&stts_entry_count, 4, 1, mp4);
	stts_entry_count=ntohl(stts_entry_count);

	//TEST stts_entry_count
	printf("\nstts_entry_count = %d\n",stts_entry_count);
	fflush(stdout);
	//
	int sample_count=0;
	int sample_size=0;
	int delta_counter=0;
	for(int i=0; i<stts_entry_count; i++) {
		fread(&sample_count,4,1,mp4);
		fread(&sample_size,4,1,mp4);
		sample_count=ntohl(sample_count);
		sample_size=ntohl(sample_size);
		for(int k=0; k<sample_count; k++) {
			delta[delta_counter]=sample_size;
			delta_counter++;
		}
	}
	pts[0]=0;
	DecoderSpecificInfo = (DecoderSpecificInfo>>7) & 15;
	for(int i=1; i<nframes; i++) {
		pts[i]=pts[i-1]+((float)delta[i-1]/SamplingFrequencies[DecoderSpecificInfo]);
	}
	free(delta);

}

// dts = pts;

//repeat_for_every_segment = 0;

//flags[] = {1,....,1};

void get_samplerate_and_nch(int DecoderSpecificInfo, int* sample_rate, int* n_ch) {
	int sample_rate_index = (DecoderSpecificInfo>>7) & 15;
	*sample_rate = SamplingFrequencies[sample_rate_index];
	int n_ch_index = (DecoderSpecificInfo>>3) & 15;
	*n_ch = ChannelConfigurations[n_ch_index];
}

//sample_size = 16 bit;

//data_start_offset = 0 //isn't necessary

//type = 0;

int get_MediaStatsT(FILE* mp4, MP4_BOX* root, media_stats_t* m_stat_ptr) {
	int MediaStatsT=0;
	MP4_BOX** moov_traks=NULL;
	int n_tracks=get_count_of_audio_traks(mp4, root, &moov_traks);
	track_t* track;

	if (m_stat_ptr==NULL) {
		MediaStatsT=sizeof(media_stats_t)+sizeof(track_t)*n_tracks;
		for (int i=0; i<n_tracks; i++) {
			MediaStatsT+=((sizeof(float)+sizeof(int))*get_nframes(mp4,moov_traks[i]));
		}
		return MediaStatsT;
	}

	m_stat_ptr->n_tracks=n_tracks;
	for (int i=0; i<n_tracks && i<2; i++) {
		if (i==0)
			m_stat_ptr->track[i]=(char*)m_stat_ptr+sizeof(media_stats_t);
		else
			m_stat_ptr->track[i]=(char*)m_stat_ptr->track[i-1]+sizeof(track_t)+((sizeof(float)+sizeof(int))*track[i-1].n_frames);

		track=m_stat_ptr->track[i];
		if(handlerType(mp4,find_box(moov_traks[i],"hdlr"),"soun"))
			track->codec=get_audio_codec(mp4,moov_traks[i]);
		else track->codec=0;
		/*//NEED TO MAKE THIS FUNCTION
		else if(handlerType(mp4,find_box(moov_traks[i],"vide")))
			track[i].codec=get_video_codec(mp4,moov_traks[i]);
		*/
		track->n_frames=get_nframes(mp4,moov_traks[i]);
		track->bitrate=0;
		track->pts=(char*)track+sizeof(track_t);
		get_pts(mp4,moov_traks[i],track->n_frames,get_DecoderSpecificInfo(mp4,find_box(moov_traks[i],"stsd")),track->pts);
		track->dts=track->pts;
		track->repeat_for_every_segment=0;
		track->flags=(char*)track->pts+sizeof(float)*track->n_frames;
		for(int z=0; z<track->n_frames; z++)
			track->flags[z]=1;
		get_samplerate_and_nch(get_DecoderSpecificInfo(mp4,find_box(moov_traks[i],"stsd")), &track->sample_rate, &track->n_ch);
		track->sample_size=16;
		track->data_start_offset=0;
		track->type=0;

	}

	return MediaStatsT;
}

int get_MediaDataT(FILE* mp4, MP4_BOX* root, int piece, media_stats_t* stats, media_data_t* output_buffer ) {
	int MediaDataT=0;
	MP4_BOX** moov_traks=NULL;
	int n_tracks=get_count_of_audio_traks(mp4, root, moov_traks);
	//int n_tracks=stats->n_tracks;
	int lenght=5; // recommended_lenght for test

	if(output_buffer==NULL) {
		MediaDataT=sizeof(media_data_t)+sizeof(track_data_t)*n_tracks;
		int tmp_sf=0, tmp_ef=0, sample_count=0;
		for (int i=0; i<n_tracks; i++) {
			int* stsz_data=read_stsz(mp4, find_box(moov_traks[i],"stsz"), &sample_count);
			int tmp_buffer_size=0;
			int temp_nframes=get_frames_in_piece(stats, piece, i, &tmp_sf, &tmp_ef, lenght);
			for(int j=tmp_sf; j<tmp_ef; j++)
				tmp_buffer_size+=stsz_data[j];
			MediaDataT+=(sizeof(int/*int* size*/)+sizeof(int/*int* offset*/))*temp_nframes+sizeof(char/*char* buffer*/)*tmp_buffer_size;
			free(stsz_data);
		}
		return MediaDataT;
	}

	media_data_t* data;
	track_data_t* track_data;
	output_buffer->n_tracks=n_tracks;
	for(int i=0; i<n_tracks; i++) {

	/*	if (i==0)
			m_stat_ptr->track[i]=(char*)m_stat_ptr+sizeof(media_stats_t);
		else
			m_stat_ptr->track[i]=(char*)m_stat_ptr->track[i-1]+sizeof(track_t)+((sizeof(float)+sizeof(int))*track[i-1].n_frames);*/

		if (i==0)
			output_buffer->track_data[i]=(char*)output_buffer+sizeof(media_data_t);
		else
			output_buffer->track_data[i]=(char*)output_buffer->track_data[i-1]+sizeof(track_data_t)+sizeof(char)*output_buffer->track_data[i-1]->buffer_size+(sizeof(int)*output_buffer->track_data[i-1]->n_frames)*2;

		track_data=output_buffer->track_data[i];
		int tmp_ef=0, tmp_sample_count=0;
		track_data->n_frames=get_frames_in_piece(stats, piece, i, &track_data->first_frame, &tmp_ef, lenght);
		int* stsz_data=read_stsz(mp4, find_box(moov_traks[i],"stsz"), &tmp_sample_count);
		track_data->buffer=(char*)track_data+sizeof(track_data_t);
		track_data->buffer_size=0;
		int k=0;
		track_data->size=(char*)track_data->buffer+track_data->buffer_size;
		track_data->offset=(char*)track_data->size+sizeof(int)*track_data->n_frames;
		for(int j=track_data->first_frame; j<tmp_ef; j++) {
			track_data->size[k]=stsz_data[j];
			track_data->offset[k]=track_data->buffer_size;
			track_data->buffer_size+=stsz_data[j];
			++k;
		}
		track_data->frames_written=0;
		track_data->data_start_offset=0;
		track_data->cc=0;
	}

	return MediaDataT;
}

media_handler_t mp4_file_handler = {
										//.get_media_stats = mp4_media_get_stats,
										//.get_media_data  = mp4_media_get_data
									};
/*
int main(void) {

	FILE* mp4;
	mp4 = fopen("/home/bocharick/Work/testfiles/testfile2.mp4", "rb");
	if (mp4==NULL) {
		printf("\nCan't open file\n");
		exit(1);
	}
	MP4_BOX* root;
	root = mp4_looking(mp4);
	get_sounddata_to_file(mp4,root);
	printf("\n");
	i_want_to_break_free(root);
	fclose(mp4);
	return 0;
}
*/
