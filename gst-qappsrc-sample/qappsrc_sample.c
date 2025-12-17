/*-------------------------------------------------------------------
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear

Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

--------------------------------------------------------------------*/

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/syscall.h>

#define gettid() syscall(SYS_gettid)
#define getpid() syscall(SYS_getpid)

enum {
  PRIO_ERROR=0x1,
  PRIO_INFO=0x2,
  PRIO_DEBUG=0x4,
  PRIO_LOW=0x8
};

static int s_qappsrc_debug_level = PRIO_ERROR | PRIO_INFO;
void qappsrc_debug_level_init(void)
{
  char *ptr = getenv("QAPPSRC_DEBUG_LEVEL");
  s_qappsrc_debug_level = ptr ? atoi(ptr) : s_qappsrc_debug_level;
  printf("s_qappsrc_debug_level=0x%x\n", s_qappsrc_debug_level);
}

#define DEBUG_PRINT_CTL(level, fmt, args...)   \
  do {                                        \
    if (level & s_qappsrc_debug_level)           \
      printf("[%ld:%ld][%s:%d] " fmt "\n", getpid(), \
        gettid(), __func__, __LINE__, ##args); \
  } while(0)
#define ERROR_PRINT(fmt,args...) DEBUG_PRINT_CTL(PRIO_ERROR, fmt, ##args)
#define INFO_PRINT(fmt, args...) DEBUG_PRINT_CTL(PRIO_INFO, fmt, ##args)
#define DEBUG_PRINT(fmt, args...) DEBUG_PRINT_CTL(PRIO_DEBUG, fmt, ##args)
#define DETAIL_PRINT(fmt,args...) DEBUG_PRINT_CTL(PRIO_LOW, fmt, ##args)

#define GST_PRT_ERROR(fmt, args...)	\
  do {					\
    GST_ERROR(fmt, ##args);		\
    ERROR_PRINT(fmt, ##args);		\
  } while(0)

#define GST_PRT_WARNING(fmt, args...)	\
  do {					\
    GST_WARNING(fmt, ##args);		\
    INFO_PRINT(fmt, ##args);		\
  } while(0)

#define GST_PRT_DEBUG(fmt, args...)	\
  do {					\
    GST_DEBUG(fmt, ##args);		\
    DEBUG_PRINT(fmt, ##args);		\
  } while(0)

GST_DEBUG_CATEGORY (qappsrc_debug);
#define GST_CAT_DEFAULT qappsrc_debug

/************************************************************************/
/*              #DEFINES                            */
/************************************************************************/
#define H264_START_CODE 0x00000001
#define NALU_TYPE_SPS       7
#define NALU_TYPE_PPS       8
#define NALU_TYPE_SEI       6
#define NALU_TYPE_IDR       5
#define NALU_TYPE_NON_IDR   1
#define NALU_TYPE_AUD       9

#define H265_START_CODE 0x00000001
#define HEVC_NALU_TYPE_MASK 0x7F
#define HEVC_NAL_UNIT_TYPE_TRAIL_N 0x00
#define HEVC_NALU_TYPE_NON_IDR 1
#define HEVC_NALU_TYPE_IDR 19
#define HEVC_NAL_UNIT_TYPE_IDR_N_LP 20
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_N10 10
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_N12 12
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_N14 14
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_R11 11
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_R13 13
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_R15 15
#define HEVC_NAL_UNIT_TYPE_RSV_IRAP_VCL22 22
#define HEVC_NAL_UNIT_TYPE_RSV_IRAP_VCL23 23
#define HEVC_NAL_UNIT_TYPE_VCL_LIMIT 23
#define HEVC_NAL_UNIT_TYPE_RESERVED_START 0x18
#define HEVC_NAL_UNIT_TYPE_RESERVED_END 0x1F
#define HEVC_NALU_TYPE_VPS 32
#define HEVC_NALU_TYPE_SPS 33
#define HEVC_NALU_TYPE_PPS 34
#define HEVC_NALU_TYPE_AUD 35
#define HEVC_NALU_TYPE_SEI 39
#define HEVC_NAL_UNIT_TYPE_SUFFIX_SEI 0x28
#define HEVC_NAL_UNIT_TYPE_RESERVED_UNSPECIFIED 0x29
#define HEVC_FIRST_MB_IN_SLICE_MASK 0x80

typedef enum {
  QAPPSRC_WAYLANDSINK         = 0,
  QAPPSRC_WAYLANDSINK_APPSINK = 1,
  QAPPSRC_APPSINK             = 2,
} qappsrc_sink_type;

typedef struct _qappsrc {
  GMainLoop *loop;
} qappsrc;

static FILE *s_input_fp = NULL;
static FILE *s_length_fp = NULL;
static uint8_t *s_parser_buffer = NULL;
static int s_max_parser_buffer_size = 0;
static FILE *s_output_fp = NULL;
static int64_t s_timeStamp_fromIVF = -1;
static uint32_t s_ts_scaler_fromIVF_d = 0;
static uint32_t s_ts_scaler_fromIVF_n = 0;
static gboolean s_fps_setting = FALSE;
static float s_fps = 30.0;
static uint64_t s_pts = 0;
static uint32_t s_timestampInterval = 33333333;  //ns
static int s_codec_type = 0;
static int (*Read_One_Frame)(uint8_t *data);

static inline void update_gstbuf_pts(GstBuffer* gstbuf)
{
  // For IVF file, use TS from file if fps is not set
  if (s_codec_type == 3) {
    if (s_timeStamp_fromIVF >= 0) {
      GST_BUFFER_PTS(gstbuf) = s_timeStamp_fromIVF;
    } else {
      GST_PRT_ERROR("TS from IVF has eror %lld", (long long)s_timeStamp_fromIVF);
    }
  }

  // calculate pts by setting fps
  if (s_fps_setting) {
    GST_BUFFER_PTS(gstbuf) = s_pts;
    s_pts += s_timestampInterval;
  }
  return;
}

/* The function is triggered when element GST_APPSRC queue is empty. It requires more buffers to
   fill. Firstly, it reads raw video data, then copies the data to input buffer of OMX input
   port.
*/
static void onNeedData(GstElement *appsrc, guint dataSize, qappsrc *qappsrc)
{
  gboolean ret = FALSE;
  int fd = -1;
  int length = Read_One_Frame(s_parser_buffer);
  if (length <= 0) {
    gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
    GST_PRT_DEBUG("sent eos !!!");
    return;
  }

  GST_PRT_DEBUG ("etb %d bytes", length);
  GstBuffer* gstbuf = gst_buffer_new_allocate(NULL, length, NULL);
  gst_buffer_fill (gstbuf, 0, s_parser_buffer, length);
  update_gstbuf_pts(gstbuf);
  gst_app_src_push_buffer(GST_APP_SRC(appsrc), gstbuf);
  return;
}


static GstFlowReturn onNewSample(GstElement *appsink, qappsrc *qappsrc)
{
  GstSample *sample;
  GstBuffer *buffer;
  GstMapInfo map;
  int ret = 0;
  int length = 0;

  //Retrieve the buffer
  g_signal_emit_by_name(appsink, "pull-sample", &sample);
  if (sample) {
    buffer = gst_sample_get_buffer(sample);

    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      //yuv data is NV12_UBWC format
      length = map.size;
      ret = fwrite(map.data, 1, length, s_output_fp);
      gst_buffer_unmap (buffer, &map);
    } else {
      GST_PRT_ERROR("gst buffer map error");
    }

    if (ret == length) {
      GST_PRT_DEBUG("Successed to write %d bytes to the file ", ret);
    } else {
      GST_PRT_ERROR("Failed to write to the file, want %d bytes, ret %d", length, ret);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  } else {
    GST_PRT_ERROR("pull-sample error");
    return GST_FLOW_ERROR;
  }
}

static gboolean msg_handler(GstBus *bus, GstMessage *msg, gpointer data)
{
  qappsrc *appsrc = (qappsrc *)data;
  GMainLoop *loop = appsrc->loop;
  gint buf_idx;
  if (msg->type == GST_MESSAGE_EOS) {
    g_main_loop_quit (loop);
    GST_PRT_DEBUG ("the pipeline will be ended because of the EOS message");
  } else if (msg->type == GST_MESSAGE_ERROR) {
    g_main_loop_quit (loop);
    GST_ERROR ("the pipiple post a error");
  } else {
    GST_PRT_DEBUG("didn't handle msg type:%d", msg->type);
  }

  return TRUE;
}

static int Read_Buffer_By_Length_File(uint8_t *data)
{
  char len[10];
  char * lenptr;
  int le = -1;
  int read = -1;
  lenptr = fgets(len, sizeof(len), s_length_fp);
  if (lenptr) {
    le = atoi(lenptr);
    DEBUG_PRINT ("+++ get len %d from file len", le);
  }

  if (le > 0 ) {
    read = fread(data, 1, le, s_input_fp);
    if (read != le) {
      DEBUG_PRINT ("+++ read %d from file input, not equal to %d from file len", read, le);
    }
  }

  return le;
}

static int Read_Buffer_From_H264_Start_Code_File(uint8_t *data)
{
  int byte_read = 0;
  int cnt = 0;
  char abyte=0;
  gboolean hasFrameContent = FALSE;
  gboolean done = FALSE;
  unsigned int code = 0;
  int naluType = 0;
  int newFrame = 0;
  int startcode=0;
  int adjust = 0;
  char *dataptr = (char *)data;

  DEBUG_PRINT("Inside");
  do
  {
    newFrame = 0;
    byte_read = fread(&abyte, 1, 1, s_input_fp);
    DETAIL_PRINT("00 READ Byte[%d] = 0x%x", cnt, dataptr[cnt]);
    if (!byte_read)
    {
      DEBUG_PRINT("Bytes read Zero, cnt=%d", cnt);
      done = TRUE;
    }

    if (!done)
    {
      code <<= 8;
      code |= (0x000000FF & abyte);
      if (code == H264_START_CODE)
      {
        startcode = 4;
      }
      else if ((code & 0x00ffffff) == H264_START_CODE)
      {
        startcode = 3;
      }
      else
      {
        startcode = 0;
      }

      if (cnt != 0)
      {
        dataptr[cnt++]=abyte;
      }

      if (startcode != 0)
      {
        DETAIL_PRINT("Read_Buffer_From_H264_File.Found H264_START_CODE");
        byte_read = fread(&abyte, 1, 1, s_input_fp);
        if (byte_read == 0)
        {
          DEBUG_PRINT("Bytes read Zero, cnt=%d", cnt);
          done = TRUE;
        }

        if (!done)
        {
          DETAIL_PRINT("Read_Buffer_From_H264_File.READ.Byte[%d] = 0x%x", cnt, abyte);
          naluType = abyte & 0x1F;
          if (cnt == 0)
          {
            // dataptr does not have any
            // need to determine where to start capture
            switch(naluType)
            {
            case NALU_TYPE_IDR:
            case NALU_TYPE_NON_IDR:
              // set frame_content and go through to start store
              hasFrameContent = TRUE;
            case NALU_TYPE_SPS:
            case NALU_TYPE_PPS:
              // start collecting
              dataptr[cnt++] = 0;
              dataptr[cnt++] = 0;
              if (startcode == 3)
              {
                dataptr[cnt++] = 1;
              }
              else
              {
                dataptr[cnt++] = 0;
                dataptr[cnt++] = 1;
              }
              dataptr[cnt++]=abyte;
              adjust = startcode + 1;
              break;
            default:
              // do not update cnt: not start of frame yet
              break;
            } // switch
          }
          else
          {
            /* dataptr has something; already start collecting
            need to determine a frame start and end */
            dataptr[cnt++]=abyte; // store the nal type
            switch(naluType)
            {
            case NALU_TYPE_IDR:
            case NALU_TYPE_NON_IDR:
              if (!hasFrameContent)
              {
                hasFrameContent = TRUE;
              }
              else
              {
                /* dataptr has frame content
                need to determine the frame boundary at IDR/NIDR NAL */
                byte_read = fread(&abyte, 1, 1, s_input_fp);
                if (byte_read == 0)
                {
                  done = TRUE;
                }

                if (!done)
                {
                  dataptr[cnt++]=abyte; // store it
                  newFrame = abyte & 0x80;
                  if (newFrame)
                  {
                    /* first mb address in the slice is 0 => assume non-ASO,
                    frame-based clip and non-error stream */
                    DEBUG_PRINT("newFrame startcode:%d", startcode);
                    adjust = -(startcode + 2);
                    fseek(s_input_fp, adjust, SEEK_CUR);
                    cnt += adjust;
                    done = TRUE;
                  }
                }
              }
              break;
            case NALU_TYPE_SPS:
            case NALU_TYPE_PPS:
            case NALU_TYPE_SEI:
            case NALU_TYPE_AUD:
              if (hasFrameContent)
              {
                // dataptr has frame content
                // it is the frame boundary if SPS, PPS, SEI or AUD
                DEBUG_PRINT("hasFrameContent startcode:%d", startcode);
                adjust = -(startcode + 1);
                fseek(s_input_fp, adjust, SEEK_CUR);
                cnt += adjust;
                done = TRUE;
              }
            } // switch
          } // if cnt == 0
        } // !done
      } // (startcode != 0)
    } // (!done)
  } while ((!done) && (cnt < s_max_parser_buffer_size));

  return cnt;
}

static int Read_Buffer_From_H265_Start_Code_File(uint8_t *data)
{
  int byte_read = 0;
  int cnt = 0;
  char abyte=0;
  gboolean hasFrameContent = FALSE;
  gboolean done = FALSE;
  unsigned int code = 0;
  int naluType = 0;
  int newFrame = 0;
  int startcode=0;
  int adjust = 0;
  char *dataptr = (char *)data;

  DEBUG_PRINT("Inside");
  do
  {
    newFrame = 0;
    byte_read = fread(&abyte, 1, 1, s_input_fp);
    DETAIL_PRINT("00 READ Byte[%d] = 0x%x", cnt, dataptr[cnt]);
    if (!byte_read)
    {
      DEBUG_PRINT("Bytes read Zero, cnt=%d", cnt);
      done = TRUE;
    }

    if (!done)
    {
      code <<= 8;
      code |= (0x000000FF & abyte);
      if (code == H265_START_CODE)
      {
        startcode = 4;
      }
      else if ((code & 0x00ffffff) == H265_START_CODE)
      {
        startcode = 3;
      }
      else
      {
        startcode = 0;
      }

      if (cnt != 0)
      {
        dataptr[cnt++]=abyte;
      }

      if (startcode != 0)
      {
        DETAIL_PRINT("Read_Buffer_From_H265_File.Found H265_START_CODE");
        byte_read = fread(&abyte, 1, 1, s_input_fp);
        if (byte_read == 0)
        {
          DEBUG_PRINT("Bytes read Zero, cnt=%d", cnt);
          done = TRUE;
        }

        if (!done)
        {
          DETAIL_PRINT("Read_Buffer_From_H265_File.READ.Byte[%d] = 0x%x", cnt, abyte);
          naluType = (abyte & 0x7F) >> 1;
          if (cnt == 0)
          {
            // dataptr does not have any
            // need to determine where to start capture
            switch(naluType)
            {
            case HEVC_NALU_TYPE_IDR:
            case HEVC_NALU_TYPE_NON_IDR:
              // set frame_content and go through to start store
              hasFrameContent = TRUE;
            case HEVC_NALU_TYPE_SPS:
            case HEVC_NALU_TYPE_PPS:
            case HEVC_NALU_TYPE_VPS:
              // start collecting
              dataptr[cnt++] = 0;
              dataptr[cnt++] = 0;
              if (startcode == 3)
              {
                dataptr[cnt++] = 1;
              }
              else
              {
                dataptr[cnt++] = 0;
                dataptr[cnt++] = 1;
              }
              dataptr[cnt++]=abyte;
              //2 Bytes header for H265, so need to read next byte
              byte_read = fread(&abyte, 1, 1, s_input_fp);
              if (byte_read == 0)
              {
                done = TRUE;
              }

              dataptr[cnt++]=abyte;
              adjust = startcode + 2;
              break;
            default:
              // do not update cnt: not start of frame yet
              break;
            } // switch
          }
          else
          {
            /* dataptr has something; already start collecting
            need to determine a frame start and end */
            dataptr[cnt++]=abyte; // store the nal type
            switch(naluType)
            {
            case HEVC_NALU_TYPE_IDR:
            case HEVC_NALU_TYPE_NON_IDR:
              if (!hasFrameContent)
              {
                hasFrameContent = TRUE;
              }
              else
              {
                //2 Bytes header for H265, so need to read next byte
                byte_read = fread(&abyte, 1, 1, s_input_fp);
                if (byte_read == 0)
                {
                  done = TRUE;
                }

                dataptr[cnt++]=abyte;

                /* dataptr has frame content
                need to determine the frame boundary at IDR/NIDR NAL */
                byte_read = fread(&abyte, 1, 1, s_input_fp);
                if (byte_read == 0)
                {
                  done = TRUE;
                }

                if (!done)
                {
                  dataptr[cnt++]=abyte; // store it
                  newFrame = abyte & 0x80;
                  if (newFrame)
                  {
                    /* first mb address in the slice is 0 => assume non-ASO,
                    frame-based clip and non-error stream */
                    DEBUG_PRINT("newFrame startcode:%d", startcode);
                    adjust = -(startcode + 3);
                    fseek(s_input_fp, adjust, SEEK_CUR);
                    cnt += adjust;
                    done = TRUE;
                  }
                }
              }
              break;
            case HEVC_NALU_TYPE_SPS:
            case HEVC_NALU_TYPE_PPS:
            case HEVC_NALU_TYPE_VPS:
            case HEVC_NALU_TYPE_SEI:
            case HEVC_NALU_TYPE_AUD:
              if (hasFrameContent)
              {
                // dataptr has frame content
                // it is the frame boundary if SPS, PPS, SEI or AUD
                DEBUG_PRINT("hasFrameContent startcode:%d", startcode);
                adjust = -(startcode + 1);
                fseek(s_input_fp, adjust, SEEK_CUR);
                cnt += adjust;
                done = TRUE;
              }
            } // switch
          } // if cnt == 0
        } // !done
      } // (startcode != 0)
    } // (!done)
  } while ((!done) && (cnt < s_max_parser_buffer_size));

  return cnt;
}

static int Read_Frame_Length_From_Ivf_File(int bytes_count, int *frame_length)
{
  int length = 0;
  int len = 0;
  int bytes_read = 0;
  int ret = 0;

  for (int i = 0; i < bytes_count; i++) {
    len = 0;
    ret = fread(&len, 1, 1, s_input_fp);
    if (ret > 0) {
      length += ( len<< (8*i));
      bytes_read++;
    }
    else {
      break;
    }
  }

  *frame_length = length;
  return bytes_read;
}

static int Read_Frame_Timestamp_From_Ivf_File(int bytes_count, uint64_t *frame_timestamp)
{
  uint64_t timestamp = 0;
  uint64_t len = 0;
  int bytes_read = 0;
  int ret = 0;

  for (int i = 0; i < bytes_count; i++) {
    len = 0;
    ret = fread(&len, 1, 1, s_input_fp);
    if (ret > 0) {
      timestamp += ( len<< (8*i));
      bytes_read++;
    }
    else {
      break;
    }
  }

  *frame_timestamp = timestamp;
  return bytes_read;
}

static int Read_Buffer_From_Ivf_File(uint8_t *data)
{
  int length = 0;
  long frame_ts = 0;
  int bytes_read = 0;

  DEBUG_PRINT("Inside");
  bytes_read = Read_Frame_Length_From_Ivf_File(4, &length);
  if ( bytes_read > 0 && bytes_read < 4) {
    ERROR_PRINT("Reading IVF frame size, %d bytes read, not equal to 4 bytes, treat as EOF", bytes_read);
    return 0;
  }else if ( 0 == bytes_read ) {
    printf("0 bytes read from IVF file, really meet EOF\n");
    return 0;
  }
  bytes_read = Read_Frame_Timestamp_From_Ivf_File(8, &frame_ts);
  if ( 8 != bytes_read ) {
    ERROR_PRINT("Reading IVF frame ts, %d bytes read, not equal to 8 bytes, treat as EOF", bytes_read);
    return 0;
  }
  bytes_read = fread(data, 1, length, s_input_fp);
  if (bytes_read != length) {
    ERROR_PRINT("Reading IVF frame data, %d bytes read, not equal to %d bytes, treat as EOF", bytes_read, length);
    return 0;
  }

  if (s_ts_scaler_fromIVF_d != 0 && s_ts_scaler_fromIVF_n != 0) {
    s_timeStamp_fromIVF = frame_ts * 1000000000 * s_ts_scaler_fromIVF_n / s_ts_scaler_fromIVF_d;
  }

  return bytes_read;
}

static int Parse_Ivf_FileHeader()
{
  unsigned char ivfheader[32] = {0};
  int width;
  int height;
  unsigned int length = 0;
  unsigned long ts1, ts2 = 0;
  int bytes_read = 0;

  int ivfheaderlen = fread(ivfheader, 1, 32, s_input_fp);
  if(ivfheaderlen != 32 || !(ivfheader[0] == 'D' && ivfheader[1] == 'K' && ivfheader[2] == 'I' && ivfheader[3] == 'F')) {
    printf("IVF file not begin with \"DKIF\", it's corrupted IVF file\n");
    return -1;
  }
  width = ivfheader[12] + ((unsigned int)ivfheader[13] << 8);
  height = ivfheader[14] + ((unsigned int)ivfheader[15] << 8);
  printf("Parsed from IVF file header, W x H is %d x %d\n", width, height);
  s_ts_scaler_fromIVF_d = ivfheader[16] + ((unsigned int)ivfheader[17]<<8) + ((unsigned int)ivfheader[18]<<16) + ((unsigned int)ivfheader[19]<<24);
  s_ts_scaler_fromIVF_n = ivfheader[20] + ((unsigned int)ivfheader[21]<<8) + ((unsigned int)ivfheader[22]<<16) + ((unsigned int)ivfheader[23]<<24);
  printf("Parsed from IVF file header, time base denominator %d, time base numerator %d\n", s_ts_scaler_fromIVF_d, s_ts_scaler_fromIVF_n);

  bytes_read = Read_Frame_Length_From_Ivf_File(4, &length);
  if ( bytes_read > 0 && bytes_read < 4) {
    ERROR_PRINT("Reading IVF frame size, %d bytes read, not equal to 4 bytes, treat as EOF", bytes_read);
    return -1;
  } else if ( 0 == bytes_read ) {
    printf("0 bytes read from IVF file, really meet EOF\n");
    return -1;
  }
  bytes_read = Read_Frame_Timestamp_From_Ivf_File(8, &ts1);
  if ( 8 != bytes_read ) {
    ERROR_PRINT("Reading IVF frame ts, %d bytes read, not equal to 8 bytes, treat as EOF", bytes_read);
    return -1;
  }
  fseek(s_input_fp, length + 4, SEEK_CUR);
  bytes_read = Read_Frame_Timestamp_From_Ivf_File(8, &ts2);
  if ( 8 != bytes_read ) {
    ERROR_PRINT("This IVF not contain 2 frames, won't continue decoding");
    return -1;
  }
  fseek(s_input_fp, 32, SEEK_SET);

  printf("first 2 frames timestamps are %ld, %ld\n", ts1, ts2);
  if ((ts2 - ts1) != 0 && s_ts_scaler_fromIVF_n != 0)
    s_fps = (float) s_ts_scaler_fromIVF_d / (s_ts_scaler_fromIVF_n * (ts2 - ts1));
  printf("IVF FPS = %.2f\n", s_fps);
}

static void print_usage(void)
{
  printf ("Error command line argument passed, should be:\n");
  printf ("qappsrc_sample <codec-id> <width> <height> <stream file path> [s0|s1|s2] [o:output file path] [f:fps]\n");
  printf ("codec-id:          1:h264 raw stream; 2:h265 raw stream; 3:vp9 ivf\n");
  printf ("width:             video's width\n");
  printf ("height:            video's height\n");
  printf ("stream file path:  input video file path\n");
  printf ("s0|s1|s2:          s0:waylandsink(default); s1:waylandsink+appsink; s2:appsink\n");
  printf ("o:output file path: output file path when s1 or s2, default is output.ubwc\n");
  printf ("l:length file path: read input buffer by length file, default is not set\n");
  printf ("f:fps:             video's fps, valid for no B frame video, default is unavailable\n");
  printf ("example:\n");
  printf ("qappsrc_sample 1 1920 1080 test.h264\n");
  printf ("qappsrc_sample 2 1920 1080 test.h265 s1\n");
  printf ("qappsrc_sample 2 1920 1080 test.h265 s1 l:/data/length.txt\n");
  printf ("qappsrc_sample 3 1920 1080 vp9.ivf s2 o:/data/output.dat\n");
  printf ("qappsrc_sample 1 640 480 IPonly.h264 s0 f:29.97\n");
}

int main(int argc, char **argv)
{
  gst_init(NULL, NULL);
  GMainLoop *loop;
  GstElement *appsrc;
  GstElement *tee;
  GstElement *waylandsink;
  GstElement *appsink;
  GstElement *decode;
  GstElement *waylandsink_queue;
  GstElement *appsink_queue;
  GstElement *pipeline;
  GstPad *tee_waylandsink_pad;
  GstPad *tee_appsink_pad;
  GstPad *waylandsink_pad;
  GstPad *appsink_pad;
  GstCaps *caps;
  char *stream_file;
  char *length_file = NULL;
  char *output_file;
  guint bus_watch_id;
  int width;
  int height;
  int max_output_buffer_size = 0;
  char in_caps[512] = {0,};
  char outputfilename[512] = {0,};
  qappsrc_sink_type sink_type = QAPPSRC_WAYLANDSINK;

  if (argc < 5) {
    print_usage();
    return 0;
  } else {
    s_codec_type = atoi(argv[1]);
    width = atoi(argv[2]);
    height = atoi(argv[3]);
    stream_file = argv[4];

    //parse optional param
    int option_param_count = argc - 5;
    int i = 0;
    snprintf(outputfilename, sizeof(outputfilename), "output.ubwc");
    while(i < option_param_count) {
     if (!memcmp(argv[5 + i], "s0", 2)) {
        sink_type = QAPPSRC_WAYLANDSINK;
      } else if (!memcmp(argv[5 + i], "s1", 2)) {
        sink_type = QAPPSRC_WAYLANDSINK_APPSINK;
      } else if (!memcmp(argv[5 + i], "s2", 2)) {
        sink_type = QAPPSRC_APPSINK;
      } else if (!memcmp(argv[5 + i], "o:", 2)) {
        //remove pre "o:"
        char *filename = argv[5 + i];
        memset(outputfilename, 0, sizeof(filename));
        memcpy(outputfilename, &filename[2], strlen(filename) - 2);
      } else if (!memcmp(argv[5 + i], "f:", 2)) {
        //remove pre "f:"
        char *fps_str = argv[5 + i];
        s_fps = atof(&fps_str[2]);
        s_timestampInterval = round(1000000000/s_fps);  //ns
        s_fps_setting = TRUE;
        printf ("setting fps:%.2f, timestampInterval:%dns\n", s_fps, s_timestampInterval);
      } else if (!memcmp(argv[5 + i], "l:", 2)) {
        //remove pre "l:"
        char *length_file_str = argv[5 + i];
        length_file = &length_file_str[2];
        printf ("length file:%s\n", length_file);
      }
      i++;
    }

    if (s_codec_type > 3 || s_codec_type < 1 || stream_file == NULL) {
      printf ("codec_type or stream file input error\n");
      return 0;
    }
  }
  qappsrc_debug_level_init();
  GST_DEBUG_CATEGORY_INIT (qappsrc_debug, "qappsrc", 0, "qcom appsrc");

  switch (s_codec_type) {
    case 1:
      Read_One_Frame = Read_Buffer_From_H264_Start_Code_File;
      snprintf(in_caps, sizeof(in_caps), "video/x-h264, stream-format=(string)byte-stream, alignment=(string)au, \
          width=(int)%d, height=(int)%d, interlace-mode=(string)progressive, chroma-format=(string)4:2:0, \
        bit-depth-luma=(uint)8, bit-depth-chroma=(uint)8, parsed=(boolean)true", width, height);
      break;
    case 2:
      Read_One_Frame = Read_Buffer_From_H265_Start_Code_File;
      snprintf(in_caps, sizeof(in_caps), "video/x-h265, stream-format=(string)byte-stream, alignment=(string)au, \
          width=(int)%d, height=(int)%d, interlace-mode=(string)progressive, chroma-format=(string)4:2:0, \
        bit-depth-luma=(uint)8, bit-depth-chroma=(uint)8, parsed=(boolean)true", width, height);
      break;
    case 3:
      Read_One_Frame = Read_Buffer_From_Ivf_File;
      snprintf(in_caps, sizeof(in_caps), "video/x-vp9, stream-format=(string)byte-stream, alignment=(string)au, \
        width=(int)%d, height=(int)%d, interlace-mode=(string)progressive, chroma-format=(string)4:2:0, \
        bit-depth-luma=(uint)8, bit-depth-chroma=(uint)8, parsed=(boolean)true", width, height);
      break;
  }

  if (length_file) {
    s_length_fp = fopen( length_file , "r" );
    Read_One_Frame = Read_Buffer_By_Length_File;
    if (!s_length_fp) {
      printf ("Failed to open length file\n");
      return 0;
    }
  }

  s_input_fp = fopen( stream_file , "r" );
  if (!s_input_fp) {
    printf ("Failed to open input file\n");
    return 0;
  }

  s_output_fp = fopen (outputfilename, "wb");
  if (!s_output_fp) {
    fclose(s_input_fp);
    s_input_fp = NULL;
    printf ("Failed to open output file\n");
    return 0;
  }

  s_max_parser_buffer_size = (width * height * 3 / 2) / 2;//it's a rough estimation.
  s_parser_buffer = g_malloc0(s_max_parser_buffer_size);
  if(!s_parser_buffer) {
    fclose(s_input_fp);
    s_input_fp = NULL;
    if (s_output_fp) {
      fclose(s_output_fp);
      s_output_fp = NULL;
    }
    printf ("Failed to malloc input buffer\n");
    return 0;
  }

  if (s_codec_type == 3) {
    Parse_Ivf_FileHeader();
  }

  qappsrc *appsrc_struct = g_new0(qappsrc, 1);


  gboolean rett = FALSE;

  loop = g_main_loop_new(NULL, FALSE);
  appsrc_struct->loop = loop;
  appsrc = gst_element_factory_make("appsrc", "appsrc");

  caps = gst_caps_from_string (in_caps);
  g_object_set (appsrc, "caps", caps, NULL);
  gst_caps_unref (caps);

  if (sink_type == QAPPSRC_WAYLANDSINK) {
    waylandsink = gst_element_factory_make("waylandsink", "waylandsink");
  } else if (sink_type == QAPPSRC_WAYLANDSINK_APPSINK) {
    tee = gst_element_factory_make("tee", "tee");
    waylandsink_queue = gst_element_factory_make("queue", "waylandsink_queue");
    appsink_queue = gst_element_factory_make("queue", "appsink_queue");
    waylandsink = gst_element_factory_make("waylandsink", "waylandsink");
    appsink = gst_element_factory_make("appsink", "appsink");
    g_object_set (appsink, "emit-signals", TRUE, NULL);
  } else if (sink_type == QAPPSRC_APPSINK) {
    appsink = gst_element_factory_make("appsink", "appsink");
    g_object_set (appsink, "emit-signals", TRUE, NULL);
  }

  if (s_codec_type == 1) {
    decode = gst_element_factory_make("qcodec2h264dec", "qcodec2h264dec");
  } else if (s_codec_type == 2) {
    decode = gst_element_factory_make("qcodec2h265dec", "qcodec2h265dec");
  } else if (s_codec_type == 3) {
    decode = gst_element_factory_make("qcodec2vp9dec", "qcodec2vp9dec");
  }

  pipeline = gst_pipeline_new("pipeline");

  if (sink_type == QAPPSRC_WAYLANDSINK) {
    gst_bin_add_many(GST_BIN(pipeline), appsrc, decode, waylandsink, NULL);
    gst_element_link_many(appsrc, decode, waylandsink, NULL);
  } else if (sink_type == QAPPSRC_WAYLANDSINK_APPSINK) {
    gst_bin_add_many(GST_BIN(pipeline), appsrc, decode, tee, waylandsink_queue, appsink_queue, waylandsink, appsink, NULL);
    gst_element_link_many(appsrc, decode, tee, NULL);
    gst_element_link_many(waylandsink_queue, waylandsink, NULL);
    gst_element_link_many(appsink_queue, appsink, NULL);

    tee_waylandsink_pad = gst_element_get_request_pad(tee, "src_%u");
    INFO_PRINT ("Obtained request pad %s for waylandsink branch.", gst_pad_get_name(tee_waylandsink_pad));
    waylandsink_pad = gst_element_get_static_pad(waylandsink_queue, "sink");
    tee_appsink_pad = gst_element_get_request_pad(tee, "src_%u");
    INFO_PRINT ("Obtained request pad %s for appsink branch.", gst_pad_get_name(tee_appsink_pad));
    appsink_pad = gst_element_get_static_pad(appsink_queue, "sink");
    gst_pad_link(tee_waylandsink_pad, waylandsink_pad);
    gst_pad_link(tee_appsink_pad, appsink_pad);
  } else if (sink_type == QAPPSRC_APPSINK) {
    gst_bin_add_many(GST_BIN(pipeline), appsrc, decode, appsink, NULL);
    gst_element_link_many(appsrc, decode, appsink, NULL);
  }

  g_signal_connect(G_OBJECT(appsrc), "need-data", G_CALLBACK(onNeedData), appsrc_struct);
  if (sink_type != QAPPSRC_WAYLANDSINK) {
    g_signal_connect(G_OBJECT(appsink), "new-sample", G_CALLBACK(onNewSample), appsrc_struct);
  }

  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  bus_watch_id = gst_bus_add_watch (bus, msg_handler, appsrc_struct);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  printf ("pipeline set playing\n");
  g_main_loop_run(loop);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  printf ("pipeline set null\n");
  g_source_remove (bus_watch_id);
  gst_object_unref (GST_OBJECT (pipeline));

  g_main_loop_unref(loop);

  g_free(appsrc_struct);

  if (s_length_fp) {
    fclose(s_length_fp);
    s_length_fp = NULL;
  }

  if (s_input_fp) {
    fclose(s_input_fp);
    s_input_fp = NULL;
  }

  if (s_output_fp) {
    fclose(s_output_fp);
    s_output_fp = NULL;
  }

  if (s_parser_buffer) {
    g_free(s_parser_buffer);
    s_parser_buffer = NULL;
  }

  printf("\nProgram is finished!\n");
  return 0;
}
