#include "mpeg4-avc.h"
#include <memory.h>
#include <assert.h>

/*
ISO/IEC 14496-15:2010(E) 5.2.4.1.1 Syntax (p16)

aligned(8) class AVCDecoderConfigurationRecord {
	unsigned int(8) configurationVersion = 1;
	unsigned int(8) AVCProfileIndication;
	unsigned int(8) profile_compatibility;
	unsigned int(8) AVCLevelIndication;
	bit(6) reserved = ��111111��b;
	unsigned int(2) lengthSizeMinusOne;
	bit(3) reserved = ��111��b;

	unsigned int(5) numOfSequenceParameterSets;
	for (i=0; i< numOfSequenceParameterSets; i++) {
		unsigned int(16) sequenceParameterSetLength ;
		bit(8*sequenceParameterSetLength) sequenceParameterSetNALUnit;
	}

	unsigned int(8) numOfPictureParameterSets;
	for (i=0; i< numOfPictureParameterSets; i++) {
		unsigned int(16) pictureParameterSetLength;
		bit(8*pictureParameterSetLength) pictureParameterSetNALUnit;
	}

	if( profile_idc == 100 || profile_idc == 110 || 
		profile_idc == 122 || profile_idc == 144 )
	{
		bit(6) reserved = ��111111��b;
		unsigned int(2) chroma_format;
		bit(5) reserved = ��11111��b;
		unsigned int(3) bit_depth_luma_minus8;
		bit(5) reserved = ��11111��b;
		unsigned int(3) bit_depth_chroma_minus8;
		unsigned int(8) numOfSequenceParameterSetExt;
		for (i=0; i< numOfSequenceParameterSetExt; i++) {
			unsigned int(16) sequenceParameterSetExtLength;
			bit(8*sequenceParameterSetExtLength) sequenceParameterSetExtNALUnit;
		}
	}
}
*/
int mpeg4_avc_decoder_configuration_record_load(const uint8_t* data, size_t bytes, struct mpeg4_avc_t* avc)
{
	uint8_t i;
	uint32_t j;
	uint16_t len;
	
	if (bytes < 7) return -1;
	assert(1 == data[0]);
//	avc->version = data[0];
	avc->profile = data[1];
	avc->compatibility = data[2];
	avc->level = data[3];
	avc->nalu = (data[4] & 0x03) + 1;
	avc->nb_sps = data[5] & 0x1F;
	if (avc->nb_sps > sizeof(avc->sps) / sizeof(avc->sps[0]))
	{
		assert(0);
		return -1; // sps <= 32
	}

	j = 6;
	for (i = 0; i < avc->nb_sps && j + 2 < bytes; ++i)
	{
		len = (data[j] << 8) | data[j + 1];
		if (len + j + 2 >= bytes // data length + sps length
			|| len >= sizeof(avc->sps[i].data))
		{
			assert(0);
			return -1;
		}

		memcpy(avc->sps[i].data, data + j + 2, len);
		avc->sps[i].bytes = len;
		j += len + 2;
	}

	if (j+1 >= bytes || data[j] > sizeof(avc->pps) / sizeof(avc->pps[0]))
	{
		assert(0);
		return -1;
	}

	avc->nb_pps = data[j++]; 
	for (i = 0; i < avc->nb_pps && j + 2 < bytes; i++)
	{
		len = (data[j] << 8) | data[j + 1];
		if (len + j + 2 > bytes // data length + pps length
			|| len >= sizeof(avc->pps[i].data))
			return -1;

		memcpy(avc->pps[i].data, data + j + 2, len);
		avc->pps[i].bytes = len;
		j += len + 2;
	}

	return j;
}

int mpeg4_avc_decoder_configuration_record_save(const struct mpeg4_avc_t* avc, uint8_t* data, size_t bytes)
{
	uint8_t i;
	uint8_t *p = data;

	assert(0 != avc->nalu);
	if (bytes < 7 || avc->nb_sps > 32) return -1;
	bytes -= 7;

	// AVCDecoderConfigurationRecord
	// ISO/IEC 14496-15:2010
	// 5.2.4.1.1 Syntax
	p[0] = 1; // configurationVersion
	p[1] = avc->profile; // AVCProfileIndication
	p[2] = avc->compatibility; // profile_compatibility
	p[3] = avc->level; // AVCLevelIndication
	p[4] = 0xFC | avc->nalu; // lengthSizeMinusOne: 3
	p += 5;

	// sps
	*p++ = 0xE0 | avc->nb_sps;
	for (i = 0; i < avc->nb_sps && bytes >= (size_t)avc->sps[i].bytes + 2; i++)
	{
		*p++ = (avc->sps[i].bytes >> 8) & 0xFF;
		*p++ = avc->sps[i].bytes & 0xFF;
		memcpy(p, avc->sps[i].data, avc->sps[i].bytes);

		p += avc->sps[i].bytes;
		bytes -= avc->sps[i].bytes + 2;
	}
	if (i < avc->nb_sps) return -1; // check length

	// pps
	*p++ = avc->nb_pps;
	for (i = 0; i < avc->nb_pps && bytes >= (size_t)avc->pps[i].bytes + 2; i++)
	{
		*p++ = (avc->pps[i].bytes >> 8) & 0xFF;
		*p++ = avc->pps[i].bytes & 0xFF;
		memcpy(p, avc->pps[i].data, avc->pps[i].bytes);

		p += avc->pps[i].bytes;
		bytes -= avc->pps[i].bytes + 2;
	}
	if (i < avc->nb_pps) return -1; // check length

	if (bytes >= 4)
	{
		if (avc->profile == 100 || avc->profile == 110 ||
			avc->profile == 122 || avc->profile == 244 || avc->profile == 44 ||
			avc->profile == 83 || avc->profile == 86 || avc->profile == 118 ||
			avc->profile == 128 || avc->profile == 138 || avc->profile == 139 ||
			avc->profile == 134)
		{
			*p++ = 0xFC | avc->chroma_format_idc;
			*p++ = 0xF8 | avc->bit_depth_luma_minus8;
			*p++ = 0xF8 | avc->bit_depth_chroma_minus8;
			*p++ = 0; // numOfSequenceParameterSetExt
		}
	}

	return (int)(p - data);
}

#define H264_STARTCODE(p) (p[0]==0 && p[1]==0 && (p[2]==1 || (p[2]==0 && p[3]==1)))

int mpeg4_avc_to_nalu(const struct mpeg4_avc_t* avc, uint8_t* data, size_t bytes)
{
	uint8_t i;
	size_t k = 0;
	uint8_t* h264 = data;

	// sps
	for (i = 0; i < avc->nb_sps && bytes >= k + avc->sps[i].bytes + 4; i++)
	{
		if (avc->sps[i].bytes < 4 || !H264_STARTCODE(avc->sps[i].data))
		{
			h264[k++] = 0;
			h264[k++] = 0;
			h264[k++] = 0;
			h264[k++] = 1;
		}
		memcpy(h264 + k, avc->sps[i].data, avc->sps[i].bytes);

		k += avc->sps[i].bytes;
	}
	if (i < avc->nb_sps) return -1; // check length

	// pps
	for (i = 0; i < avc->nb_pps && bytes >= k + avc->pps[i].bytes + 2; i++)
	{
		if (avc->pps[i].bytes < 4 || !H264_STARTCODE(avc->pps[i].data))
		{
			h264[k++] = 0;
			h264[k++] = 0;
			h264[k++] = 0;
			h264[k++] = 1;
		}
		memcpy(h264 + k, avc->pps[i].data, avc->pps[i].bytes);

		k += avc->pps[i].bytes;
	}
	if (i < avc->nb_pps) return -1; // check length

	return k;
}