// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QCARCAMQCX_H__
#define __GST_QCARCAMQCX_H__

#include <inttypes.h>
#include <qcarcam.h>
#include <gst/gst.h>

gboolean qcarcamqcx_init(const QCarCamInit_t *pInitParams);

gboolean qcarcamqcx_uninit(void);

gboolean qcarcamqcx_query_inputs(QCarCamInput_t *pInputs, const uint32_t size, uint32_t *pRetSize);

gboolean qcarcamqcx_open(const QCarCamOpen_t* pOpenParams, QCarCamHndl_t* pHndl);

gboolean qcarcamqcx_close(const QCarCamHndl_t hndl);

gboolean qcarcamqcx_set_bufs(const QCarCamHndl_t hndl, const QCarCamBufferList_t *pBuffers);

gboolean qcarcamqcx_release_bufs(const QCarCamHndl_t hndl, const uint32_t bufferlistId);

gboolean qcarcamqcx_start(const QCarCamHndl_t hndl);

gboolean qcarcamqcx_stop(const QCarCamHndl_t hndl);

gboolean qcarcamqcx_pause(const QCarCamHndl_t hndl);

gboolean qcarcamqcx_resume(const QCarCamHndl_t hndl);

gboolean qcarcamqcx_get_frame(const QCarCamHndl_t hndl,
         QCarCamFrameInfo_t *pFrameInfo,
         const uint64_t timeout,
         const uint32_t flags);

gboolean qcarcamqcx_release_frame(const QCarCamHndl_t hndl,
         const uint32_t id,
         const uint32_t bufferIndex);

gboolean qcarcamqcx_register_event_callback(const QCarCamHndl_t hndl,
         const QCarCamEventCallback_t callbackFunc,
         void  *pPrivateData);

gboolean qcarcamqcx_unregister_event_callback(const QCarCamHndl_t hndl);

gboolean qcarcamqcx_reserve(const QCarCamHndl_t hndl);

gboolean qcarcamqcx_release(const QCarCamHndl_t hndl);

gboolean qcarcamqcx_query_input_modes(const uint32_t inputId, QCarCamInputModes_t* pInputModes);

gboolean qcarcamqcx_setparam(
        const QCarCamHndl_t hndl,
        const QCarCamParamType_e param,
        const void *pValue,
        const uint32_t size);
gboolean qcarcamqcx_submit_request(const QCarCamHndl_t hndl, const QCarCamRequest_t* pRequest);

#endif /* __GST_QCARCAMQCX_H__ */
