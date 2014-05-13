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
/*
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
*/

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

//NEW NEW VERSION (works)

void scan_one_level(file_handle_t* mp4, file_source_t* source, MP4_BOX* head) {
	int child_count=0;
	int byte_counter = head->box_first_byte;
	int first_byte = head->box_first_byte; //0
	int box_size = 0;
	char box_type[4];
	if (head->box_first_byte != 0)
		first_byte = head->box_first_byte+8;

	while(byte_counter < (head->box_size + head->box_first_byte-9)) {
		int how_much=0;
		first_byte += box_size;
//	fseek(mp4, first_byte, SEEK_SET);
//	fread(&box_size, 4, 1, mp4);
		how_much = source->read(mp4, &box_size, 4, first_byte, 0);
//	fread(box_type, 1, 4, mp4);
		source->read(mp4, box_type, 4, first_byte+how_much, 0);
		box_size = ntohl(box_size);
		head->child_ptr[child_count] = add_item(head, box_size, box_type, first_byte);
		child_count++;
		byte_counter += box_size;

	}
	head->child_count=child_count;
//	printf("\ntype - '%.4s'; size - '%d'; child count - '%d'; first byte - '%d'",head->box_type, head->box_size, head->child_count, head->box_first_byte);
	if (child_count>0) {
		for (int count=0; count<head->child_count; count++) {
			if ((if_subbox(head->child_ptr[count]->box_type)) > 0)
				scan_one_level(mp4, source, head->child_ptr[count]);
			//else
			//	printf("\ntype - '%.4s'; size - '%d'; child count - '%d'; first byte - '%d'",head->child_ptr[count]->box_type, head->child_ptr[count]->box_size, head->child_ptr[count]->child_count, head->child_ptr[count]->box_first_byte);
		}
	}
}


MP4_BOX* mp4_looking(file_handle_t* mp4, file_source_t* source) {
//	fseek (mp4, 0, SEEK_END);
	int fSize;
//	fSize = ftell (mp4);
	fSize = source->get_file_size(mp4, 0);
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
	scan_one_level(mp4, source, head);
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

int handlerType(file_handle_t* mp4, file_source_t* source, MP4_BOX* root, char handler[5]) {
	char hdlr_type[4];
//	fseek(mp4, root->box_first_byte+16, SEEK_SET); //+16 cause of 'hdlr' syntax.
//	fread(hdlr_type, 1, 4, mp4);
	source->read(mp4, hdlr_type, 4, root->box_first_byte+16, 0);

	for (int i=0; i<4; i++) {
		if(hdlr_type[i]!=handler[i])
			return 0;
	}
	return 1;
}

MP4_BOX* find_necessary_trak(file_handle_t* mp4, file_source_t* source, MP4_BOX* root, char trak_type[5]) {
	MP4_BOX* moov;
	MP4_BOX* necessary_trak=NULL;
	MP4_BOX* temp_ptr;
	moov=find_box(root, "moov");
	for(int i=0; i<moov->child_count; i++) {
		if (compare_box_type(moov->child_ptr[i],"trak")) {
			temp_ptr=find_box(moov->child_ptr[i], "hdlr");
			if(handlerType(mp4, source, temp_ptr, trak_type))
				necessary_trak=moov->child_ptr[i];
		}
	}
	return necessary_trak;
}

sample_to_chunk** read_stsc(file_handle_t* mp4, file_source_t* source, MP4_BOX* stsc, int* stsc_entry_count) {
	int entry_count=0;
	int how_much=0;
//  fseek(mp4, stsc->box_first_byte+12, SEEK_SET);
//	fread(&entry_count, 4, 1, mp4);
	source->read(mp4, &entry_count, 4, stsc->box_first_byte+12, 0);
	entry_count=ntohl(entry_count);
	*stsc_entry_count = entry_count;
	sample_to_chunk **stc;
	stc = malloc (sizeof(sample_to_chunk*)*entry_count);
	for (int j=0; j<entry_count; j++)
		stc[j] = (sample_to_chunk*) malloc (sizeof(sample_to_chunk));
	how_much = stsc->box_first_byte+12+4;
	for(int i=0; i<entry_count; i++) {
	//fread(&stc[i]->first_chunk,4,1,mp4);
		how_much += source->read(mp4, &stc[i]->first_chunk, 4, how_much, 0);
		stc[i]->first_chunk=ntohl(stc[i]->first_chunk);
	//fread(&stc[i]->samples_per_chunk,4,1,mp4);
		how_much += source->read(mp4, &stc[i]->samples_per_chunk, 4, how_much, 0);
		stc[i]->samples_per_chunk=ntohl(stc[i]->samples_per_chunk);
	//fread(&stc[i]->sample_description_index,4,1,mp4);
		how_much += source->read(mp4, &stc[i]->sample_description_index, 4, how_much, 0);
		stc[i]->sample_description_index=ntohl(stc[i]->sample_description_index);
	}
	return stc;
}

int* read_stco(file_handle_t* mp4, file_source_t* source, MP4_BOX* stco, int* stco_entry_count) {
	int entry_count=0;
	int how_much=0;
//	fseek(mp4, stco->box_first_byte+12, SEEK_SET);
//	fread(&entry_count, 4,1,mp4);
	source->read(mp4, &entry_count, 4, stco->box_first_byte+12, 0);
	entry_count=ntohl(entry_count);
	*stco_entry_count=entry_count;
	int* stco_data;
	stco_data=(int*) malloc (sizeof(int)*entry_count);
	how_much = stco->box_first_byte+12+4;
	for (int i=0; i<entry_count; i++) {
		//fread(&stco_data[i],4,1,mp4);
		how_much += source->read(mp4, &stco_data[i], 4, how_much, 0);
		stco_data[i]=ntohl(stco_data[i]);
	}
	return stco_data;
}

int* read_stsz(file_handle_t* mp4, file_source_t* source, MP4_BOX* stsz, int* stsz_sample_count) {
	int sample_count=0;
	int sample_size=0;
	int how_much=0;
//	fseek(mp4, stsz->box_first_byte+12, SEEK_SET);
//	fread(&sample_size,4,1,mp4);
	source->read(mp4, &sample_size, 4, stsz->box_first_byte+12, 0);
	sample_size=ntohl(sample_size);
//	fread(&sample_count, 4,1,mp4);
	source->read(mp4, &sample_count, 4, stsz->box_first_byte+12+4, 0);
	sample_count=ntohl(sample_count);
	*stsz_sample_count=sample_count;
	int* stsz_data;
	stsz_data=(int*) malloc (sizeof(int)*sample_count);
	how_much = stsz->box_first_byte+12+4+4;
	if (sample_size==0) {
		for(int i=0; i<sample_count; i++) {
			//fread(&stsz_data[i],4,1,mp4);
			how_much += source->read(mp4, &stsz_data[i], 4,  how_much, 0);
			stsz_data[i]=ntohl(stsz_data[i]);
		}
	}
	else {
		for (int i=0; i<sample_count; i++)
			stsz_data[i]=sample_size;
	}
	return stsz_data;
}

int tag_size(file_handle_t* mp4, file_source_t* source, int offset_from_file_start) {
	int tagsize=0;
	int tagsize_bytes=0;
	int temp;
	int how_much=offset_from_file_start;
	do {
		//fread(&temp,1,1,mp4);
		how_much += source->read(mp4, &temp, 1, how_much, 0);
		tagsize = tagsize <<7;
		tagsize |= (temp & 127);
		tagsize_bytes += 1;
	} while ((temp&128)>0);
	return tagsize_bytes;
}

int read_esds_flag(file_handle_t* mp4, file_source_t* source, int offset_from_file_start) { //return count of bytes depends of flag
	int flags;
	int count=1;
	//fread(&flags,1,1,mp4);
	source->read(mp4, &flags, 1, offset_from_file_start, 0);
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

int get_DecoderSpecificInfo(file_handle_t* mp4, file_source_t* source, MP4_BOX* stsd) {
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
	 * 		tag  - 1 byte
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
	//fseek(mp4, stsd->box_first_byte+skip_bytes, SEEK_SET);
	skip_bytes += (tag_size(mp4, source, stsd->box_first_byte+skip_bytes) + 2); //2 - ES_ID
	//fseek(mp4,2,SEEK_CUR); // 2 - ES_ID
	skip_bytes += (read_esds_flag(mp4, source, stsd->box_first_byte+skip_bytes) +1); // 1 - DecoderConfigDescriptor tag
	//fseek(mp4,stsd->box_first_byte+skip_bytes,SEEK_SET);
	skip_bytes += (tag_size(mp4, source, stsd->box_first_byte+skip_bytes) + 13 + 1); // 13 - Sum(ObjectTypeIndication .. avgBitrate); 1 - DecoderSpecificInfo tag
	//fseek(mp4,stsd->box_first_byte+skip_bytes,SEEK_SET);
	skip_bytes += tag_size(mp4, source, stsd->box_first_byte+skip_bytes);
	int data0=0;
	int data1=0;
	//fread(&data0,1,1,mp4);
	//fread(&data1,1,1,mp4);
	source->read(mp4, &data0, 1, stsd->box_first_byte+skip_bytes, 0);
	source->read(mp4, &data1, 1, stsd->box_first_byte+skip_bytes+1, 0);
	data0 = (data0<<8) | data1;
	// Temporary ignored 5 bytes info;
	return data0;
}

char* get_AVCDecoderSpecificInfo(file_handle_t* mp4, file_source_t* source, MP4_BOX* stsd, int* avc_dec_info_size) {
	/*
	 * stsd:
	 * 		Fullbox - 12 byte
	 * 		entry_count - 4 byte
	 * VisualSampleEntry
	 * 		Box - 8 byte
	 * 		reserverd - 6 byte
	 * 		data_references_index - 2 byte
	 *
	 *		pre_defined - 2 byte
	 *		reserved - 2 byte
	 *		pre_defined - 12 byte
	 *		width - 2 byte
	 *		height - 2 byte
	 *		horizresolution - 4 byte
	 *		vertresolution - 4 byte
	 *		reserved - 4 byte
	 *		frame_count - 2 byte
	 *		compressorname - 32 byte
	 *		depth - 2 byte
	 *		pre_defined - 2 byte
	 *
	 * AVCSampleEntry
	 * 		AVCConfigurationBox
	 * 			Box - 8 byte ("avcC")
	 * 			AVCDecoderConfigurationRecord {
					configurationVersion  - 1 byte
					AVCProfileIndication - 1 byte
					profile_compatibility - 1 byte
					AVCLevelIndication - 1 byte
					reserved = ‘111111’b - 6 bit
					lengthSizeMinusOne - 2 bit
					reserved = ‘111’b - 3 bit
					numOfSequenceParameterSets - 5 bit
					for (i=0; i< numOfSequenceParameterSets; i++) {
						sequenceParameterSetLength - 2 byte
						sequenceParameterSetNALUnit - 1*(sequenceParameterSetLength) byte
					}
					numOfPictureParameterSets - 1 byte
					for (i=0; i< numOfPictureParameterSets; i++) {
						pictureParameterSetLength - 2 byte
						pictureParameterSetNALUnit - 1*(pictureParameterSetLength) byte
					}
				}
	 *
	 *
	 */
	int skip_bytes=stsd->box_first_byte+16;
	//int entry_count=0;
	//skip_bytes+=source->read(mp4, &entry_count, 4, skip_bytes, 0);
	//entry_count=ntohl(entry_count);
	skip_bytes+=86;
//	if (entry_count>1) {
		int box_size=0;
		char box_name[4]={0,0,0,0};
		while (memcmp("avcC", box_name, 4)!=0) {
			source->read(mp4, &box_size, 4, skip_bytes, 0);
			source->read(mp4, box_name, 4, skip_bytes+4, 0);
			box_size=ntohl(box_size);
			if (memcmp("avcC",box_name, 4)!=0)
				skip_bytes+=box_size;
		}
	//}
	skip_bytes+=13; // avcC before reserved 3 bits
	int numofSPS=0;
	skip_bytes+=source->read(mp4, &numofSPS, 1, skip_bytes, 0);
	numofSPS&=31;
	int* SPSLength;
	SPSLength = (int*) malloc (sizeof(int)*numofSPS);
	if (SPSLength==NULL) {
		printf("Couldn't allocate memory for SPSLength");
		exit(1);
	}
	for (int v=0; v<numofSPS; v++)
		SPSLength[v]=0;
	int sps_length_summ=0;
	int sps_count=skip_bytes;
	for (int i=0; i<numofSPS; i++) {
		source->read(mp4, &SPSLength[i], 2, sps_count, 0);
		SPSLength[i]=ntohs(SPSLength[i]);
		sps_length_summ+=SPSLength[i];
		sps_count+=2+SPSLength[i];
	}
	char* SPS=NULL;
	SPS = (char*) malloc (sizeof(char)*sps_length_summ+sizeof(char)*4*numofSPS);
	if (SPS==NULL) {
			printf("Couldn't allocate memory for SPS");
			exit(1);
	}
	sps_count=skip_bytes+2;
	char* temp_ptr=SPS;
	char annexb_syncword[4]={0,0,0,1};
	for (int i=0; i<numofSPS; i++) {
		memcpy(temp_ptr, annexb_syncword, 4);
		source->read(mp4, temp_ptr+4, SPSLength[i], sps_count, 0);
		sps_count+=SPSLength[i]+2;
		temp_ptr+=4+SPSLength[i];
	}
	skip_bytes=sps_count-2;

	int numofPPS=0;
	skip_bytes+=source->read(mp4, &numofPPS, 1, skip_bytes, 0);
	int* PPSLength;
	PPSLength = (int*) malloc (sizeof(int)*numofPPS);
	if (PPSLength==NULL) {
			printf("Couldn't allocate memory for PPSLength");
			exit(1);
		}
	for (int v=0; v<numofPPS; v++)
			PPSLength[v]=0;
	int pps_length_summ=0;
	int pps_count=skip_bytes;
	for (int i=0; i<numofPPS; i++) {
		source->read(mp4, &PPSLength[i], 2, pps_count, 0);
		PPSLength[i]=ntohs(PPSLength[i]);
		pps_length_summ+=PPSLength[i];
		pps_count+=2+PPSLength[i];
	}
	char* PPS;
	PPS = (char*) malloc (sizeof(char)*pps_length_summ+sizeof(char)*4*numofPPS);
	if (PPS==NULL) {
		printf("Couldn't allocate memory for PPS");
		exit(1);
	}
	pps_count=skip_bytes+2;
	temp_ptr=PPS;
	for (int i=0; i<numofPPS; i++) {
		memcpy(temp_ptr, annexb_syncword, 4);
		source->read(mp4, temp_ptr+4, PPSLength[i], pps_count, 0);
		pps_count+=PPSLength[i]+2;
		temp_ptr+=4+PPSLength[i];
	}

	char* avc_decoder_info=NULL; //SPS+PPS
	int SPS_size=sizeof(char)*sps_length_summ+sizeof(char)*4*numofSPS;
	int PPS_size=sizeof(char)*pps_length_summ+sizeof(char)*4*numofPPS;
	//avc_decoder_info= (char*) malloc (sizeof(char)*(sps_length_summ+pps_length_summ)+sizeof(char)*4*(numofSPS+numofPPS));
	avc_decoder_info= (char*) malloc (SPS_size+PPS_size);
	if (avc_decoder_info==NULL) {
		printf("Couldn't allocate memory for avc_decoder_info");
		exit(1);
	}
	*avc_dec_info_size=SPS_size+PPS_size;
	memcpy(avc_decoder_info, SPS, SPS_size);
	memcpy(avc_decoder_info+SPS_size, PPS, PPS_size);
	free(SPS);
	free(SPSLength);
	free(PPS);
	free(PPSLength);
	return avc_decoder_info;
}


void get_sounddata_to_file(file_handle_t* mp4, file_source_t* source, MP4_BOX* root) {
	FILE* mp4_sound;
	mp4_sound=fopen("mp4_sound.aac","wb");
	if(mp4_sound==NULL) {
		printf("\nCould'n create mp4_sound.aac file\n");
		exit(1);
	}
	MP4_BOX* find_trak;
	MP4_BOX* find_stsc;
	MP4_BOX* find_stco;
	MP4_BOX* find_stsz;
	MP4_BOX* find_stsd;

	find_trak=find_necessary_trak(mp4, source, root, "soun");
	find_stsc=(find_box(find_trak, "stsc"));
	find_stco=(find_box(find_trak, "stco"));
	find_stsz=(find_box(find_trak, "stsz"));
	find_stsd=(find_box(find_trak, "stsd"));

	int stsc_entry_count=0;
	int stco_entry_count=0;
	int stsz_entry_count=0;

	sample_to_chunk** stsc_data;
	stsc_data = read_stsc(mp4, source, find_stsc, &stsc_entry_count);

	int* stco_data;
	stco_data = read_stco(mp4, source, find_stco, &stco_entry_count);

	int* stsz_data;
	stsz_data = read_stsz(mp4, source, find_stsz, &stsz_entry_count);

	//Test getDecoderSpecificInfo
	int DecSpecInfo=0;
	long long BaseMask=0xFFF10200001FFC;
	DecSpecInfo = get_DecoderSpecificInfo(mp4, source, find_stsd);
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
	//	fseek(mp4,stco_data[i],SEEK_SET);
		int how_much=stco_data[i];
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
		//	fread(buffer,stsz_data[k],1,mp4);
			how_much+=source->read(mp4, buffer,stsz_data[k],how_much, 0);
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


int get_count_of_traks(file_handle_t* mp4, file_source_t* source, MP4_BOX* root, MP4_BOX*** moov_traks) {
	MP4_BOX* moov;
	*moov_traks = (MP4_BOX**) malloc (sizeof(MP4_BOX*)*2);
	if(*moov_traks==NULL) {
		printf("Couldn't allocate memory for moov_traks");
		exit(1);
	}
	moov = find_box(root, "moov");
	int trak_counter=0;
	for(int i=0; i<moov->child_count; i++) {
		if (trak_counter<2 && compare_box_type(moov->child_ptr[i],"trak") &&
			(handlerType(mp4, source, find_box(moov->child_ptr[i],"hdlr"), "soun") || handlerType(mp4, source, find_box(moov->child_ptr[i],"hdlr"), "vide"))) {
			(*moov_traks)[trak_counter]=moov->child_ptr[i];
			trak_counter++;
		}
	}
	return trak_counter;
}

int get_count_of_audio_traks(file_handle_t* mp4, file_source_t* source, MP4_BOX* root, MP4_BOX*** moov_traks) {
	MP4_BOX* moov;
	*moov_traks = (MP4_BOX**) malloc (sizeof(MP4_BOX*)*2);
	if(*moov_traks==NULL) {
		printf("Couldn't allocate memory for moov_traks");
		exit(1);
	}
	moov = find_box(root, "moov");
	int trak_counter=0;
	for(int i=0; i<moov->child_count; i++) {
		if (compare_box_type(moov->child_ptr[i],"trak") && handlerType(mp4,source, find_box(moov->child_ptr[i],"hdlr"),"soun")) {
			(*moov_traks)[trak_counter]=moov->child_ptr[i];
			trak_counter++;
		}
	}
	return trak_counter;
}

int get_codec(file_handle_t* mp4, file_source_t* source, MP4_BOX* trak) {
	MP4_BOX* stsd;
	stsd = find_box(trak, "stsd");
	char box_type[4];
//	fseek(mp4, stsd->box_first_byte+20, SEEK_SET);
//	fread(box_type, 1, 4, mp4);
	source->read(mp4, box_type, 4, stsd->box_first_byte+20, 0);
	char codec[4]="mp4a";
	char codec2[4]="avc1";
	/*for(int i=0; i<4; i++) {
		if (box_type[i]!=codec[i])
			return 0;
	}
	return 0x0F;
	*/
	if(memcmp(codec,box_type,4)==0)
		return 0x0F;
	if(memcmp(codec2,box_type,4)==0)
		return 0x1B;
	return 0;
}

int get_nframes(file_handle_t* mp4, file_source_t* source, MP4_BOX* trak) {
	MP4_BOX* stsz;
	stsz = find_box(trak, "stsz");
	int* data;
	int nframes=0;
	data = read_stsz(mp4, source, stsz, &nframes);
	free(data);
	return nframes;
}

void get_pts(file_handle_t* mp4, file_source_t* source, MP4_BOX* audio_trak, int nframes, int DecoderSpecificInfo,float* pts) {
	int* delta;
	delta = (int*) malloc (sizeof(int)*nframes);
	if (delta==NULL) {
			printf("\nCouldn't allocate memory for pts");
			exit(1);
		}
	MP4_BOX* stts;
	stts = find_box(audio_trak, "stts");
	int stts_entry_count=0;
//	fseek(mp4, stts->box_first_byte+12, SEEK_SET);
//	fread(&stts_entry_count, 4, 1, mp4);
	source->read(mp4, &stts_entry_count, 4, stts->box_first_byte+12, 0);
	stts_entry_count=ntohl(stts_entry_count);

	//TEST stts_entry_count
	//printf("\nstts_entry_count = %d\n",stts_entry_count);
	//fflush(stdout);
	//
	int sample_count=0;
	int sample_size=0;
	int delta_counter=0;
	int how_much=stts->box_first_byte+12+4;
	for(int i=0; i<stts_entry_count; i++) {
		//fread(&sample_count,4,1,mp4);
		//fread(&sample_size,4,1,mp4);
		how_much+=source->read(mp4, &sample_count, 4, how_much, 0);
		how_much+=source->read(mp4, &sample_size, 4, how_much, 0);
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
		//pts[i]=pts[i-1]+((float)1024/SamplingFrequencies[DecoderSpecificInfo]);
		pts[i]=pts[i-1]+((float)delta[i-1]/SamplingFrequencies[DecoderSpecificInfo]);
	}
	free(delta);

}

void get_video_dts(file_handle_t* mp4, file_source_t* source, MP4_BOX* video_trak, int nframes, float* dts) {
	int* delta;
	delta = (int*) malloc (sizeof(int)*nframes);
	if (delta==NULL) {
		printf("\nCouldn't allocate memory for TMP_video_dts");
		exit(1);
	}
	MP4_BOX* stts=NULL;
	stts = find_box(video_trak, "stts");
	int stts_entry_count=0;
//	fseek(mp4, stts->box_first_byte+12, SEEK_SET);
//	fread(&stts_entry_count, 4, 1, mp4);
	source->read(mp4, &stts_entry_count, 4, stts->box_first_byte+12, 0);
	stts_entry_count=ntohl(stts_entry_count);
	int sample_count=0;
	int sample_size=0;
	int delta_counter=0;
	int how_much=stts->box_first_byte+12+4;
	for(int i=0; i<stts_entry_count; i++) {
		//fread(&sample_count,4,1,mp4);
		//fread(&sample_size,4,1,mp4);
		how_much+=source->read(mp4, &sample_count, 4, how_much, 0);
		how_much+=source->read(mp4, &sample_size, 4, how_much, 0);
		sample_count=ntohl(sample_count);
		sample_size=ntohl(sample_size);
		for(int k=0; k<sample_count; k++) {
			delta[delta_counter]=sample_size;
			delta_counter++;
		}
	}
	MP4_BOX* mdhd=NULL;
	mdhd = find_box(video_trak, "mdhd");
	int mdhd_version=0;
	source->read(mp4, &mdhd_version, 1, mdhd->box_first_byte+8, 0);
	//printf("\nmdhd version = %d",mdhd_version);
	//fflush(stdout);
	int need_to_skip=0;
	if(mdhd_version==1) {
		need_to_skip=16;
	}
	else if(mdhd_version==0) {
		need_to_skip=8;
	}
	else if(mdhd_version!=0 && mdhd_version!=1) {
		printf("\ntrak's mdhd!=0 && mdhd!=1");
		exit(1);
	}
	int timescale=0;
	source->read(mp4, &timescale, 4, mdhd->box_first_byte+12+need_to_skip, 0);
	timescale=ntohl(timescale);
	dts[0]=0;
	for(int i=1; i<nframes; i++) {
		dts[i]=dts[i-1]+((float)delta[i-1]/timescale);
	}
	free(delta);
}

void get_video_pts(file_handle_t* mp4, file_source_t* source, MP4_BOX* video_trak, int nframes, float* pts, float* dts) {
	int* delta;
	delta = (int*) malloc (sizeof(int)*nframes);
	if (delta==NULL) {
		printf("\nCouldn't allocate memory for TMP_video_dts");
		exit(1);
	}
	MP4_BOX* ctts=NULL;
	ctts = find_box(video_trak, "ctts");
	if(ctts==NULL) {
		memcpy(pts,dts,nframes);
		return;
	}
	int ctts_entry_count=0;
	source->read(mp4, &ctts_entry_count, 4, ctts->box_first_byte+12, 0);
	ctts_entry_count=ntohl(ctts_entry_count);
	int sample_count=0;
	int sample_size=0;
	int delta_counter=0;
	int how_much=ctts->box_first_byte+12+4;
	for(int i=0; i<ctts_entry_count; i++) {
		//fread(&sample_count,4,1,mp4);
		//fread(&sample_size,4,1,mp4);
		how_much+=source->read(mp4, &sample_count, 4, how_much, 0);
		how_much+=source->read(mp4, &sample_size, 4, how_much, 0);
		sample_count=ntohl(sample_count);
		sample_size=ntohl(sample_size);
		for(int k=0; k<sample_count; k++) {
			delta[delta_counter]=sample_size;
			delta_counter++;
		}
	}
	MP4_BOX* mdhd=NULL;
	mdhd = find_box(video_trak, "mdhd");
	int mdhd_version=0;
	source->read(mp4, &mdhd_version, 1, mdhd->box_first_byte+8, 0);
	int need_to_skip=0;
	if(mdhd_version==1) {
		need_to_skip=16;
	}
	else if(mdhd_version==0) {
		need_to_skip=8;
	}
	else if(mdhd_version!=0 && mdhd_version!=1) {
		printf("\ntrak's mdhd!=0 && mdhd!=1");
		exit(1);
	}
	int timescale=0;
	source->read(mp4, &timescale, 4, mdhd->box_first_byte+12+need_to_skip, 0);
	timescale=ntohl(timescale);
	pts[0]=0;
	for(int i=0; i<nframes; i++) {
		pts[i]=dts[i]+(float)delta[i]/timescale;
	}
	free(delta);

}

int* get_stss(file_handle_t* mp4, file_source_t* source, MP4_BOX* video_trak, int* entry_count) {
	MP4_BOX* stss_ptr;
	stss_ptr=find_box(video_trak, "stss");
	int stss_entry_count=0;
	source->read(mp4, &stss_entry_count, 4, stss_ptr->box_first_byte+12, 0);
	stss_entry_count=ntohl(stss_entry_count);
	*entry_count=stss_entry_count;
	int how_much=stss_ptr->box_first_byte+16;
	int* stss;
	stss = (int*) malloc (sizeof(int)*stss_entry_count);
	if (stss==NULL) {
			printf("\nCouldn't allocate memory for stss");
			exit(1);
		}
	for (int i=0; i<stss_entry_count; i++) {
		how_much+=source->read(mp4, &stss[i], 4, how_much, 0);
		stss[i]=ntohl(stss[i]);
	}
	return stss;
}

void get_samplerate_and_nch(int DecoderSpecificInfo, int* sample_rate, int* n_ch) {
	int sample_rate_index = (DecoderSpecificInfo>>7) & 15;
	*sample_rate = SamplingFrequencies[sample_rate_index];
	int n_ch_index = (DecoderSpecificInfo>>3) & 15;
	*n_ch = ChannelConfigurations[n_ch_index];
}

int mp4_media_get_stats(file_handle_t* mp4, file_source_t* source, media_stats_t* m_stat_ptr, int output_buffer_size) {
	MP4_BOX* root=mp4_looking(mp4,source);
	int MediaStatsT=0;
	MP4_BOX** moov_traks=NULL;
	int n_tracks=get_count_of_traks(mp4, source, root, &moov_traks);
	track_t* track;

	if (m_stat_ptr==NULL) {
		MediaStatsT=sizeof(media_stats_t)+sizeof(track_t)*n_tracks;
		for (int i=0; i<n_tracks; i++) {
			if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"soun"))
				MediaStatsT+=((sizeof(float)+sizeof(int))*get_nframes(mp4, source, moov_traks[i]));
			if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"vide"))
				MediaStatsT+=(2*(sizeof(float)+sizeof(int))*get_nframes(mp4, source, moov_traks[i]));
		}
		return MediaStatsT;
	}

	m_stat_ptr->n_tracks=n_tracks;
	for (int i=0; i<n_tracks && i<2; i++) {
		if (i==0)
			m_stat_ptr->track[i]=(char*)m_stat_ptr+sizeof(media_stats_t);
		else {
			if(handlerType(mp4, source, find_box(moov_traks[i-1],"hdlr"),"soun"))
				m_stat_ptr->track[i]=(char*)m_stat_ptr->track[i-1]+sizeof(track_t)+((sizeof(float)+sizeof(int))*track[i-1].n_frames);
			if(handlerType(mp4, source, find_box(moov_traks[i-1],"hdlr"),"vide"))
				m_stat_ptr->track[i]=(char*)m_stat_ptr->track[i-1]+sizeof(track_t)+((2*sizeof(float)+sizeof(int))*track[i-1].n_frames);
		}
		track=m_stat_ptr->track[i];
		track->codec=get_codec(mp4, source, moov_traks[i]);
		track->n_frames=get_nframes(mp4, source, moov_traks[i]);
		track->bitrate=0;
		if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"soun")) {
			track->pts=(char*)track+sizeof(track_t);
			track->dts=track->pts;
			track->flags=(char*)track->pts+sizeof(float)*track->n_frames;
			for(int z=0; z<track->n_frames; z++)
				track->flags[z]=1;
			get_pts(mp4, source, moov_traks[i],track->n_frames,get_DecoderSpecificInfo(mp4, source, find_box(moov_traks[i],"stsd")),track->pts);
			get_samplerate_and_nch(get_DecoderSpecificInfo(mp4,source,find_box(moov_traks[i],"stsd")), &track->sample_rate, &track->n_ch);
			track->sample_size=16;
			//PTS Test PRINTF
			/*
			for(int vvv=0; vvv<track->n_frames; vvv++)
				printf("\nSOUND DTS = %f",track->dts[vvv]);
			*/


		}
		if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"vide")) {
			track->pts=(char*)track+sizeof(track_t);
			track->dts=(char*)track->pts+sizeof(float)*track->n_frames;
			track->flags=(char*)track->dts+sizeof(float)*track->n_frames;
			get_video_dts(mp4, source, moov_traks[i], track->n_frames, track->dts);
			get_video_pts(mp4, source, moov_traks[i], track->n_frames, track->pts, track->dts);
			for(int z=0; z<track->n_frames; z++)
				track->flags[z]=0;
			int stss_entry_count=0;
			int* stss=NULL;
			stss=get_stss(mp4, source, moov_traks[i], &stss_entry_count);
			for(int i=0; i<stss_entry_count; i++)
				track->flags[stss[i]-1]=1;
			track->n_ch=0;
			track->sample_rate=0;
			track->sample_size=0;



			//PTS Test PRINTF
			/*for(int vvv=0; vvv<track->n_frames; vvv++)
				printf("\nVIDEO DTS = %f",track->dts[vvv]);
			*/
		}
		track->repeat_for_every_segment=0;
		track->data_start_offset=0;
		track->type=0;

	}
	i_want_to_break_free(root);
	return output_buffer_size;
}

void convert_to_annexb(char* ptr, int buffer_size) {
	char annexb[4]={0,0,0,1};
	int nal_size=0;
	for(int i=0; i<buffer_size; ) {
	memcpy(&nal_size, ptr+i, 4);
	memcpy(ptr+i, annexb, 4);
	nal_size=ntohl(nal_size);
	i+=nal_size+4;
	}
}

int mp4_media_get_data(file_handle_t* mp4, file_source_t* source, media_stats_t* stats, int piece, media_data_t* output_buffer, int output_buffer_size) {

	//OUTPUT TESTING
	/*	FILE* mp4_sound;
			mp4_sound=fopen("testfile.aac","ab");
			if(mp4_sound==NULL) {
				printf("\nCould'n create mp4_sound.aac file\n");
				exit(1);
			}
	*/
	//

	MP4_BOX* root=mp4_looking(mp4,source);
	int MediaDataT=0;
	MP4_BOX** moov_traks=NULL;
	int n_tracks=get_count_of_traks(mp4, source, root, &moov_traks);
	int lenght=5; // recommended_lenght for test

	if(output_buffer==NULL) {
		MediaDataT=sizeof(media_data_t)+sizeof(track_data_t)*n_tracks;
		for (int i=0; i<n_tracks; i++) {
			int tmp_sf=0, tmp_ef=0, sample_count=0;
			int* stsz_data=read_stsz(mp4, source, find_box(moov_traks[i],"stsz"), &sample_count);
			int tmp_buffer_size=0;
			int temp_nframes=get_frames_in_piece(stats, piece, i, &tmp_sf, &tmp_ef, lenght);
			if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"soun")) {
				for(int j=tmp_sf; j<tmp_ef; j++)
					tmp_buffer_size+=stsz_data[j]+7; // 7 - adts header size
			}
			if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"vide")) {
				for(int j=tmp_sf; j<tmp_ef; j++)
					tmp_buffer_size+=stsz_data[j];
				char* temp_AVCDecInfo=NULL;
				int avc_decinfo_size=0;
				temp_AVCDecInfo=get_AVCDecoderSpecificInfo(mp4, source, find_box(moov_traks[i],"stsd"), &avc_decinfo_size);
				tmp_buffer_size+=avc_decinfo_size;
				free(temp_AVCDecInfo);
			}
			MediaDataT+=(sizeof(int/*int* size*/)+sizeof(int/*int* offset*/))*temp_nframes+sizeof(char/*char* buffer*/)*tmp_buffer_size;
			free(stsz_data);
		}
		return MediaDataT;
	}

	media_data_t* data;
	track_data_t* track_data;
	output_buffer->n_tracks=n_tracks;
	for(int i=0; i<n_tracks; i++) {
		if (i==0)
			output_buffer->track_data[i]=(char*)output_buffer+sizeof(media_data_t);
		else {
			output_buffer->track_data[i]=(char*)output_buffer->track_data[i-1]+sizeof(track_data_t)+sizeof(char)*output_buffer->track_data[i-1]->buffer_size+(sizeof(int)*output_buffer->track_data[i-1]->n_frames)*2;

		}
		track_data=output_buffer->track_data[i];
		int tmp_ef=0, tmp_sample_count=0;
		track_data->n_frames=get_frames_in_piece(stats, piece, i, &track_data->first_frame, &tmp_ef, lenght);
		int* stsz_data=read_stsz(mp4, source, find_box(moov_traks[i],"stsz"), &tmp_sample_count);

		track_data->buffer_size=0;
		int k=0;
		track_data->size=(char*)track_data+sizeof(track_data_t);
		track_data->offset=(char*)track_data->size+sizeof(int)*track_data->n_frames;
		track_data->buffer=(char*)track_data->offset+sizeof(int)*track_data->n_frames;
		if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"soun")) {
			for(int j=track_data->first_frame; j<tmp_ef; j++) {
				track_data->size[k]		=stsz_data[j]+7;// 7 - adts header size
				track_data->offset[k]	=track_data->buffer_size;
				track_data->buffer_size+=track_data->size[k];
				++k;
			}
		}
		if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"vide")) {
			for(int j=track_data->first_frame; j<tmp_ef; j++) {
				if(k!=0)
					track_data->size[k]		=stsz_data[j];
				else {
					char* temp_AVCDecInfo=NULL;
					int avc_decinfo_size=0;
					temp_AVCDecInfo=get_AVCDecoderSpecificInfo(mp4, source, find_box(moov_traks[i],"stsd"), &avc_decinfo_size);
					free(temp_AVCDecInfo);
					track_data->size[k]		=stsz_data[j]+avc_decinfo_size;
				}
				track_data->offset[k]	=track_data->buffer_size;
				track_data->buffer_size+=track_data->size[k];
				++k;
			}
		}
		track_data->frames_written=0;
		track_data->data_start_offset=0;
		track_data->cc=0;

		MP4_BOX* find_stsc;
		MP4_BOX* find_stco;
		MP4_BOX* find_stsz;
		MP4_BOX* find_stsd;

		find_stsc=(find_box(moov_traks[i], "stsc"));
		find_stco=(find_box(moov_traks[i], "stco"));
		find_stsz=(find_box(moov_traks[i], "stsz"));
		find_stsd=(find_box(moov_traks[i], "stsd"));

		int stsc_entry_count=0;
		int stco_entry_count=0;
		int stsz_entry_count=0;

		sample_to_chunk** stsc_dat;
		stsc_dat = read_stsc(mp4, source, find_stsc, &stsc_entry_count);

		int* stco_dat;
		stco_dat = read_stco(mp4, source, find_stco, &stco_entry_count);

		int* stsz_dat;
		stsz_dat = read_stsz(mp4, source, find_stsz, &stsz_entry_count);

		int* mp4_sample_offset;
		mp4_sample_offset = (int*) malloc (sizeof(int)*stsz_entry_count);
		if (mp4_sample_offset==NULL) {
			printf("\nCouldn't allocate memory for mp4_sample_offset\n");
			exit(1);
		}

		int mp4_sample_offset_counter=0;
		for (int stc=0; stc < stsc_entry_count; stc++) {
			for (int chunk_offset=stsc_dat[stc]->first_chunk; (stc < (stsc_entry_count-1) ? (chunk_offset < stsc_dat[stc+1]->first_chunk) : (chunk_offset<=stco_entry_count)); chunk_offset++) {
				for (int b=0; b<stsc_dat[stc]->samples_per_chunk; b++) {
					if(b==0) {
						mp4_sample_offset[mp4_sample_offset_counter]=stco_dat[chunk_offset-1];
						mp4_sample_offset_counter++;
					}
					else {
					mp4_sample_offset[mp4_sample_offset_counter]=mp4_sample_offset[mp4_sample_offset_counter-1]+stsz_dat[mp4_sample_offset_counter-1];
					mp4_sample_offset_counter++;
					}
				}
			}
		}
/*
	 	for (int kkk=0; kkk<30; kkk++) {
			printf("\nmp4_sample_offset[%d] = %d",kkk, mp4_sample_offset[kkk]);
		}
		printf("\nmp4_sample_offset_counter = %d", mp4_sample_offset_counter);
		fflush(stdout);
*/
		if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"soun")) {
			int DecSpecInfo=0;
			long long BaseMask=0xFFF10200001FFC;
			DecSpecInfo = get_DecoderSpecificInfo(mp4, source, find_stsd);
			long long objecttype=0;
			long long frequency_index=0;
			long long channel_count=0;
			long long TempMask=0;
			objecttype = 0;//(DecSpecInfo >> 11) - 1;
			frequency_index = (DecSpecInfo >> 7) & 15;
			channel_count = (DecSpecInfo >> 3) & 15;

			objecttype = objecttype << 38;
			frequency_index = frequency_index << 34;
			channel_count = channel_count << 30;
			BaseMask = BaseMask | objecttype | frequency_index | channel_count;

			for (int z = track_data->first_frame; z < track_data->first_frame+track_data->n_frames; z++) {
				TempMask = BaseMask | ((stsz_data[z] + 7) << 13);
				char for_write=0;
				for (int g=6, t=0; g>=0; g--, t++) {
					for_write=TempMask >> g*8;
					track_data->buffer[track_data->offset[z-track_data->first_frame]+t]=for_write;
				}
				source->read(mp4, (char*)track_data->buffer+track_data->offset[z-track_data->first_frame]+7,track_data->size[z-track_data->first_frame] - 7, mp4_sample_offset[z], 0);
			}
		}
		if(handlerType(mp4, source, find_box(moov_traks[i],"hdlr"),"vide")) {
			char* AVCDecInfo=NULL;
			int avc_decinfo_size=0;
			AVCDecInfo=get_AVCDecoderSpecificInfo(mp4, source, find_box(moov_traks[i],"stsd"), &avc_decinfo_size);
			for (int z = track_data->first_frame; z < track_data->first_frame+track_data->n_frames; z++) {
				if (z==track_data->first_frame) {
					int nal_size=0;
					for(int m=0; m<avc_decinfo_size; m++)
						track_data->buffer[m]=AVCDecInfo[m];
					source->read(mp4,(char*)track_data->buffer+avc_decinfo_size, track_data->size[z-track_data->first_frame]-avc_decinfo_size,mp4_sample_offset[z],0);
					convert_to_annexb((char*)track_data->buffer+avc_decinfo_size, track_data->size[z-track_data->first_frame]-avc_decinfo_size);
					free(AVCDecInfo);
				}
				else {
					source->read(mp4,(char*)track_data->buffer+track_data->offset[z-track_data->first_frame], track_data->size[z-track_data->first_frame],mp4_sample_offset[z],0);
					convert_to_annexb((char*)track_data->buffer+track_data->offset[z-track_data->first_frame], track_data->size[z-track_data->first_frame]);
				}
			}



		}
		//OUTPUT TESTING
		//fwrite(track_data->buffer, track_data->buffer_size, 1, mp4_sound);
		//

		free(mp4_sample_offset);
	}
	i_want_to_break_free(root);
	return output_buffer_size;
}

media_handler_t mp4_file_handler = {
										.get_media_stats = mp4_media_get_stats,
										.get_media_data  = mp4_media_get_data
									};

