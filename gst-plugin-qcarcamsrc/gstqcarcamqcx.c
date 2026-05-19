// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqcarcamqcx.h"
#include "gstqcarcamutils.h"

#include <gst/gstinfo.h>

GST_DEBUG_CATEGORY_EXTERN (gst_qcarcam_src_debug);
#define GST_CAT_DEFAULT gst_qcarcam_src_debug

gboolean qcarcamqcx_init(const QCarCamInit_t *pInitParams)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM init params:%p", pInitParams);
  if (!pInitParams) {
    SG_ERR_LITE("error input parameter pInitParams:%p", pInitParams);
    return ret;
  }

  qcarcam_ret = QCarCamInitialize(pInitParams);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_init error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_uninit(void)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM uninit");

  qcarcam_ret = QCarCamUninitialize();
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_uninit error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_query_inputs(QCarCamInput_t *pInputs, const uint32_t size, uint32_t *pRetSize)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM query inputs :%p, retsize :%p", pInputs, pRetSize);
  if (!pRetSize) {
    SG_ERR_LITE("error input parameter retseze:%p", pRetSize);
    return ret;
  }

  qcarcam_ret = QCarCamQueryInputs(pInputs, size, pRetSize);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_query_inputs error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_open(const QCarCamOpen_t* pOpenParams, QCarCamHndl_t* pHndl)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM open param :%p, pHndl :%p", pOpenParams, pHndl);
  if (!pOpenParams || !pHndl) {
    SG_ERR_LITE("error input parameter param:%p, pHndl:%p", pOpenParams, pHndl);
    return ret;
  }

  qcarcam_ret = QCarCamOpen(pOpenParams, pHndl);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_open error: %d", qcarcam_ret);

  GST_DEBUG("QCARCAM open pHndl :%ld", *pHndl);
  return ret;
}

gboolean qcarcamqcx_close(const QCarCamHndl_t hndl)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM close hndl :%ld", hndl);

  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%ld", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamClose(hndl);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_close error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_set_bufs(const QCarCamHndl_t hndl, const QCarCamBufferList_t *pBuffers)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM set bufs hndl :%ld", hndl);
  if (!pBuffers || !hndl) {
    SG_ERR_LITE("error input parameter pbuf:%p, hndl:%ld", pBuffers, hndl);
    return ret;
  }

  qcarcam_ret = QCarCamSetBuffers(hndl, pBuffers);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_set_bufs error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_release_bufs(const QCarCamHndl_t hndl, const uint32_t bufferlistId)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM release bufs hndl :%d", hndl);
  if (0 == bufferlistId || 0 == hndl) {
    SG_ERR_LITE("error input parameter buf list id:%d, hndl:%ld", bufferlistId, hndl);
    return ret;
  }

  qcarcam_ret = QCarCamReleaseBuffers(hndl, bufferlistId);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_release_bufs error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_start(const QCarCamHndl_t hndl)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM start hndl :%ld", hndl);

  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%ld", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamStart(hndl);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_start error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_stop(const QCarCamHndl_t hndl)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM stop hndl :%ld", hndl);

  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%ld", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamStop(hndl);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_stop error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_pause(const QCarCamHndl_t hndl)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM pause hndl :%ld", hndl);

  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%ld", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamPause(hndl);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_pause error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_resume(const QCarCamHndl_t hndl)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM resume hndl :%ld", hndl);

  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%ld", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamResume(hndl);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_resume error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_get_frame(const QCarCamHndl_t hndl,
         QCarCamFrameInfo_t *pFrameInfo,
         const uint64_t timeout,
         const uint32_t flags)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM get frame hndl :%ld", hndl);
  if (!pFrameInfo || 0 == hndl) {
    SG_ERR_LITE("error input parameter frameinfo:%p, hndl:%d", pFrameInfo, hndl);
    return ret;
  }

  qcarcam_ret = QCarCamGetFrame(hndl, pFrameInfo, timeout, flags);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_get_frame error: %d", qcarcam_ret);

  return ret;
}


gboolean qcarcamqcx_release_frame(const QCarCamHndl_t hndl,
         const uint32_t id,
         const uint32_t bufferIndex)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM release frame hndl :%ld", hndl);
  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%d", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamReleaseFrame(hndl, id, bufferIndex);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_release_frame error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_register_event_callback(const QCarCamHndl_t hndl,
         const QCarCamEventCallback_t callbackFunc,
         void  *pPrivateData)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM register event callback hndl :%ld", hndl);
  if (0 == hndl || !pPrivateData) {
    SG_ERR_LITE("error input parameter hndl:%ld, private data: %p", hndl, pPrivateData);
    return ret;
  }

  qcarcam_ret = QCarCamRegisterEventCallback(hndl, callbackFunc, pPrivateData);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_register_event_callback error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_unregister_event_callback(const QCarCamHndl_t hndl)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM unregister event callback hndl :%ld", hndl);
  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%d", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamUnregisterEventCallback(hndl);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_unregister_event_callback error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_reserve(const QCarCamHndl_t hndl)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM reserve hndl :%ld", hndl);
  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%ld", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamReserve(hndl);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_reserve error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_release(const QCarCamHndl_t hndl)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM release hndl :%ld", hndl);
  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%ld", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamRelease(hndl);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_release error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_query_input_modes(const uint32_t inputId, QCarCamInputModes_t* pInputModes)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM query input modes inputid :%d", inputId);
  if (!pInputModes) {
    SG_ERR_LITE("error input parameter input mode:%p", pInputModes);
    return ret;
  }

  qcarcam_ret = QCarCamQueryInputModes(inputId, pInputModes);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_query_input_modes error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_setparam(
        const QCarCamHndl_t hndl,
        const QCarCamParamType_e param,
        const void *pValue,
        const uint32_t size)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM set param hndl :%ld", hndl);
  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%ld", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamSetParam (hndl, param, pValue, size);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_setparam error: %d", qcarcam_ret);

  return ret;
}

gboolean qcarcamqcx_submit_request(const QCarCamHndl_t hndl, const QCarCamRequest_t* pRequest)
{
  gboolean ret = FALSE;
  QCarCamRet_e qcarcam_ret = QCARCAM_RET_OK;

  GST_DEBUG("QCARCAM submit request hndl :%ld", hndl);
  if (0 == hndl) {
    SG_ERR_LITE("error input parameter hndl:%ld", hndl);
    return ret;
  }

  qcarcam_ret = QCarCamSubmitRequest(hndl, pRequest);
  if (qcarcam_ret == QCARCAM_RET_OK)
    ret = TRUE;
  else
    SG_ERR_LITE ("qcarcamqcx_submit_request error: %d", qcarcam_ret);

  return ret;
}


