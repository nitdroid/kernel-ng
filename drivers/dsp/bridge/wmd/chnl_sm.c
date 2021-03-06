/*
 * chnl_sm.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implements upper edge functions for WMD channel module.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 *      The lower edge functions must be implemented by the WMD writer, and
 *      are declared in chnl_sm.h.
 *
 *      Care is taken in this code to prevent simulataneous access to channel
 *      queues from
 *      1. Threads.
 *      2. io_dpc(), scheduled from the io_isr() as an event.
 *
 *      This is done primarily by:
 *      - Semaphores.
 *      - state flags in the channel object; and
 *      - ensuring the IO_Dispatch() routine, which is called from both
 *        CHNL_AddIOReq() and the DPC(if implemented), is not re-entered.
 *
 *  Channel Invariant:
 *      There is an important invariant condition which must be maintained per
 *      channel outside of bridge_chnl_get_ioc() and IO_Dispatch(), violation of
 *      which may cause timeouts and/or failure offunction sync_wait_on_event.
 *      This invariant condition is:
 *
 *          LST_Empty(pchnl->pio_completions) ==> pchnl->sync_event is reset
 *      and
 *          !LST_Empty(pchnl->pio_completions) ==> pchnl->sync_event is set.
 */

/*  ----------------------------------- OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>
#include <dspbridge/cfg.h>
#include <dspbridge/sync.h>

/*  ----------------------------------- Mini-Driver */
#include <dspbridge/wmd.h>
#include <dspbridge/wmdchnl.h>
#include "_tiomap.h"

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>

/*  ----------------------------------- Others */
#include <dspbridge/io_sm.h>

/*  ----------------------------------- Define for This */
#define USERMODE_ADDR   PAGE_OFFSET

#define MAILBOX_IRQ INT_MAIL_MPU_IRQ

/*  ----------------------------------- Function Prototypes */
static struct lst_list *create_chirp_list(u32 uChirps);

static void free_chirp_list(struct lst_list *pList);

static struct chnl_irp *make_new_chirp(void);

static dsp_status search_free_channel(struct chnl_mgr *chnl_mgr_obj,
				      OUT u32 *pdwChnl);

/*
 *  ======== bridge_chnl_add_io_req ========
 *      Enqueue an I/O request for data transfer on a channel to the DSP.
 *      The direction (mode) is specified in the channel object. Note the DSP
 *      address is specified for channels opened in direct I/O mode.
 */
dsp_status bridge_chnl_add_io_req(struct chnl_object *chnl_obj, void *pHostBuf,
			       u32 byte_size, u32 buf_size,
			       OPTIONAL u32 dw_dsp_addr, u32 dw_arg)
{
	dsp_status status = DSP_SOK;
	struct chnl_object *pchnl = (struct chnl_object *)chnl_obj;
	struct chnl_irp *chnl_packet_obj = NULL;
	struct wmd_dev_context *dev_ctxt;
	struct dev_object *dev_obj;
	u32 dw_state;
	bool is_eos;
	struct chnl_mgr *chnl_mgr_obj = pchnl->chnl_mgr_obj;
	u8 *host_sys_buf = NULL;
	bool sched_dpc = false;
	u16 mb_val = 0;

	is_eos = (byte_size == 0);

	/* Validate args */
	if (!pHostBuf) {
		status = DSP_EPOINTER;
	} else if (!MEM_IS_VALID_HANDLE(pchnl, CHNL_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else if (is_eos && CHNL_IS_INPUT(pchnl->chnl_mode)) {
		status = CHNL_E_NOEOS;
	} else {
		/*
		 * Check the channel state: only queue chirp if channel state
		 * allows it.
		 */
		dw_state = pchnl->dw_state;
		if (dw_state != CHNL_STATEREADY) {
			if (dw_state & CHNL_STATECANCEL)
				status = CHNL_E_CANCELLED;
			else if ((dw_state & CHNL_STATEEOS) &&
				 CHNL_IS_OUTPUT(pchnl->chnl_mode))
				status = CHNL_E_EOS;
			else
				/* No other possible states left */
				DBC_ASSERT(0);
		}
	}

	dev_obj = dev_get_first();
	dev_get_wmd_context(dev_obj, &dev_ctxt);
	if (!dev_ctxt)
		status = DSP_EHANDLE;

	if (DSP_FAILED(status))
		goto func_end;

	if (pchnl->chnl_type == CHNL_PCPY && pchnl->chnl_id > 1 && pHostBuf) {
		if (!(pHostBuf < (void *)USERMODE_ADDR)) {
			host_sys_buf = pHostBuf;
			goto func_cont;
		}
		/* if addr in user mode, then copy to kernel space */
		host_sys_buf = mem_alloc(buf_size, MEM_NONPAGED);
		if (host_sys_buf == NULL) {
			status = DSP_EMEMORY;
			goto func_end;
		}
		if (CHNL_IS_OUTPUT(pchnl->chnl_mode)) {
			status = copy_from_user(host_sys_buf, pHostBuf,
						buf_size);
			if (status) {
				kfree(host_sys_buf);
				host_sys_buf = NULL;
				status = DSP_EPOINTER;
				goto func_end;
			}
		}
	}
func_cont:
	/* Mailbox IRQ is disabled to avoid race condition with DMA/ZCPY
	 * channels. DPCCS is held to avoid race conditions with PCPY channels.
	 * If DPC is scheduled in process context (iosm_schedule) and any
	 * non-mailbox interrupt occurs, that DPC will run and break CS. Hence
	 * we disable ALL DPCs. We will try to disable ONLY IO DPC later. */
	sync_enter_cs(chnl_mgr_obj->hcs_obj);
	omap_mbox_disable_irq(dev_ctxt->mbox, IRQ_RX);
	if (pchnl->chnl_type == CHNL_PCPY) {
		/* This is a processor-copy channel. */
		if (DSP_SUCCEEDED(status) && CHNL_IS_OUTPUT(pchnl->chnl_mode)) {
			/* Check buffer size on output channels for fit. */
			if (byte_size >
			    io_buf_size(pchnl->chnl_mgr_obj->hio_mgr))
				status = CHNL_E_BUFSIZE;

		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Get a free chirp: */
		chnl_packet_obj =
		    (struct chnl_irp *)lst_get_head(pchnl->free_packets_list);
		if (chnl_packet_obj == NULL)
			status = CHNL_E_NOIORPS;

	}
	if (DSP_SUCCEEDED(status)) {
		/* Enqueue the chirp on the chnl's IORequest queue: */
		chnl_packet_obj->host_user_buf = chnl_packet_obj->host_sys_buf =
		    pHostBuf;
		if (pchnl->chnl_type == CHNL_PCPY && pchnl->chnl_id > 1)
			chnl_packet_obj->host_sys_buf = host_sys_buf;

		/*
		 * Note: for dma chans dw_dsp_addr contains dsp address
		 * of SM buffer.
		 */
		DBC_ASSERT(chnl_mgr_obj->word_size != 0);
		/* DSP address */
		chnl_packet_obj->dsp_tx_addr =
		    dw_dsp_addr / chnl_mgr_obj->word_size;
		chnl_packet_obj->byte_size = byte_size;
		chnl_packet_obj->buf_size = buf_size;
		/* Only valid for output channel */
		chnl_packet_obj->dw_arg = dw_arg;
		chnl_packet_obj->status = (is_eos ? CHNL_IOCSTATEOS :
					   CHNL_IOCSTATCOMPLETE);
		lst_put_tail(pchnl->pio_requests,
			     (struct list_head *)chnl_packet_obj);
		pchnl->cio_reqs++;
		DBC_ASSERT(pchnl->cio_reqs <= pchnl->chnl_packets);
		/*
		 * If end of stream, update the channel state to prevent
		 * more IOR's.
		 */
		if (is_eos)
			pchnl->dw_state |= CHNL_STATEEOS;

		/* Legacy DSM Processor-Copy */
		DBC_ASSERT(pchnl->chnl_type == CHNL_PCPY);
		/* Request IO from the DSP */
		io_request_chnl(chnl_mgr_obj->hio_mgr, pchnl,
				(CHNL_IS_INPUT(pchnl->chnl_mode) ? IO_INPUT :
				 IO_OUTPUT), &mb_val);
		sched_dpc = true;

	}
	omap_mbox_enable_irq(dev_ctxt->mbox, IRQ_RX);
	sync_leave_cs(chnl_mgr_obj->hcs_obj);
	if (mb_val != 0)
		io_intr_dsp2(chnl_mgr_obj->hio_mgr, mb_val);

	/* Schedule a DPC, to do the actual data transfer */
	if (sched_dpc)
		iosm_schedule(chnl_mgr_obj->hio_mgr);

func_end:
	return status;
}

/*
 *  ======== bridge_chnl_cancel_io ========
 *      Return all I/O requests to the client which have not yet been
 *      transferred.  The channel's I/O completion object is
 *      signalled, and all the I/O requests are queued as IOC's, with the
 *      status field set to CHNL_IOCSTATCANCEL.
 *      This call is typically used in abort situations, and is a prelude to
 *      chnl_close();
 */
dsp_status bridge_chnl_cancel_io(struct chnl_object *chnl_obj)
{
	dsp_status status = DSP_SOK;
	struct chnl_object *pchnl = (struct chnl_object *)chnl_obj;
	u32 chnl_id = -1;
	short int chnl_mode;
	struct chnl_irp *chnl_packet_obj;
	struct chnl_mgr *chnl_mgr_obj = NULL;

	/* Check args: */
	if (MEM_IS_VALID_HANDLE(pchnl, CHNL_SIGNATURE) && pchnl->chnl_mgr_obj) {
		chnl_id = pchnl->chnl_id;
		chnl_mode = pchnl->chnl_mode;
		chnl_mgr_obj = pchnl->chnl_mgr_obj;
	} else {
		status = DSP_EHANDLE;
	}
	if (DSP_FAILED(status))
		goto func_end;

	/*  Mark this channel as cancelled, to prevent further IORequests or
	 *  IORequests or dispatching. */
	sync_enter_cs(chnl_mgr_obj->hcs_obj);
	pchnl->dw_state |= CHNL_STATECANCEL;
	if (LST_IS_EMPTY(pchnl->pio_requests))
		goto func_cont;

	if (pchnl->chnl_type == CHNL_PCPY) {
		/* Indicate we have no more buffers available for transfer: */
		if (CHNL_IS_INPUT(pchnl->chnl_mode)) {
			io_cancel_chnl(chnl_mgr_obj->hio_mgr, chnl_id);
		} else {
			/* Record that we no longer have output buffers
			 * available: */
			chnl_mgr_obj->dw_output_mask &= ~(1 << chnl_id);
		}
	}
	/* Move all IOR's to IOC queue: */
	while (!LST_IS_EMPTY(pchnl->pio_requests)) {
		chnl_packet_obj =
		    (struct chnl_irp *)lst_get_head(pchnl->pio_requests);
		if (chnl_packet_obj) {
			chnl_packet_obj->byte_size = 0;
			chnl_packet_obj->status |= CHNL_IOCSTATCANCEL;
			lst_put_tail(pchnl->pio_completions,
				     (struct list_head *)chnl_packet_obj);
			pchnl->cio_cs++;
			pchnl->cio_reqs--;
			DBC_ASSERT(pchnl->cio_reqs >= 0);
		}
	}
func_cont:
	sync_leave_cs(chnl_mgr_obj->hcs_obj);
func_end:
	return status;
}

/*
 *  ======== bridge_chnl_close ========
 *  Purpose:
 *      Ensures all pending I/O on this channel is cancelled, discards all
 *      queued I/O completion notifications, then frees the resources allocated
 *      for this channel, and makes the corresponding logical channel id
 *      available for subsequent use.
 */
dsp_status bridge_chnl_close(struct chnl_object *chnl_obj)
{
	dsp_status status;
	struct chnl_object *pchnl = (struct chnl_object *)chnl_obj;

	/* Check args: */
	if (!MEM_IS_VALID_HANDLE(pchnl, CHNL_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_cont;
	}
	{
		/* Cancel IO: this ensures no further IO requests or
		 * notifications. */
		status = bridge_chnl_cancel_io(chnl_obj);
	}
func_cont:
	if (DSP_SUCCEEDED(status)) {
		/* Assert I/O on this channel is now cancelled: Protects
		 * from io_dpc. */
		DBC_ASSERT((pchnl->dw_state & CHNL_STATECANCEL));
		/* Invalidate channel object: Protects from
		 * CHNL_GetIOCompletion(). */
		pchnl->dw_signature = 0x0000;
		/* Free the slot in the channel manager: */
		pchnl->chnl_mgr_obj->ap_channel[pchnl->chnl_id] = NULL;
		pchnl->chnl_mgr_obj->open_channels -= 1;
		if (pchnl->ntfy_obj) {
			ntfy_delete(pchnl->ntfy_obj);
			pchnl->ntfy_obj = NULL;
		}
		/* Reset channel event: (NOTE: user_event freed in user
		 * context.). */
		if (pchnl->sync_event) {
			sync_reset_event(pchnl->sync_event);
			sync_close_event(pchnl->sync_event);
			pchnl->sync_event = NULL;
		}
		/* Free I/O request and I/O completion queues: */
		if (pchnl->pio_completions) {
			free_chirp_list(pchnl->pio_completions);
			pchnl->pio_completions = NULL;
			pchnl->cio_cs = 0;
		}
		if (pchnl->pio_requests) {
			free_chirp_list(pchnl->pio_requests);
			pchnl->pio_requests = NULL;
			pchnl->cio_reqs = 0;
		}
		if (pchnl->free_packets_list) {
			free_chirp_list(pchnl->free_packets_list);
			pchnl->free_packets_list = NULL;
		}
		/* Release channel object. */
		MEM_FREE_OBJECT(pchnl);
		pchnl = NULL;
	}
	DBC_ENSURE(DSP_FAILED(status) ||
		   !MEM_IS_VALID_HANDLE(pchnl, CHNL_SIGNATURE));
	return status;
}

/*
 *  ======== bridge_chnl_create ========
 *      Create a channel manager object, responsible for opening new channels
 *      and closing old ones for a given board.
 */
dsp_status bridge_chnl_create(OUT struct chnl_mgr **phChnlMgr,
			      struct dev_object *hdev_obj,
			      IN CONST struct chnl_mgrattrs *pMgrAttrs)
{
	dsp_status status = DSP_SOK;
	struct chnl_mgr *chnl_mgr_obj = NULL;
	s32 max_channels;

	/* Check DBC requirements: */
	DBC_REQUIRE(phChnlMgr != NULL);
	DBC_REQUIRE(pMgrAttrs != NULL);
	DBC_REQUIRE(pMgrAttrs->max_channels > 0);
	DBC_REQUIRE(pMgrAttrs->max_channels <= CHNL_MAXCHANNELS);
	DBC_REQUIRE(pMgrAttrs->word_size != 0);

	/* Allocate channel manager object */
	MEM_ALLOC_OBJECT(chnl_mgr_obj, struct chnl_mgr, CHNL_MGRSIGNATURE);
	if (chnl_mgr_obj) {
		/*
		 * The max_channels attr must equal the # of supported chnls for
		 * each transport(# chnls for PCPY = DDMA = ZCPY): i.e.
		 *      pMgrAttrs->max_channels = CHNL_MAXCHANNELS =
		 *                       DDMA_MAXDDMACHNLS = DDMA_MAXZCPYCHNLS.
		 */
		DBC_ASSERT(pMgrAttrs->max_channels == CHNL_MAXCHANNELS);
		max_channels = CHNL_MAXCHANNELS + CHNL_MAXCHANNELS * CHNL_PCPY;
		/* Create array of channels */
		chnl_mgr_obj->ap_channel =
		    mem_calloc(sizeof(struct chnl_object *) * max_channels,
			       MEM_NONPAGED);
		if (chnl_mgr_obj->ap_channel) {
			/* Initialize chnl_mgr object */
			chnl_mgr_obj->dw_type = CHNL_TYPESM;
			chnl_mgr_obj->word_size = pMgrAttrs->word_size;
			/* Total # chnls supported */
			chnl_mgr_obj->max_channels = max_channels;
			chnl_mgr_obj->open_channels = 0;
			chnl_mgr_obj->dw_output_mask = 0;
			chnl_mgr_obj->dw_last_output = 0;
			chnl_mgr_obj->hdev_obj = hdev_obj;
			if (DSP_SUCCEEDED(status))
				status =
				    sync_initialize_dpccs
				    (&chnl_mgr_obj->hcs_obj);
		} else {
			status = DSP_EMEMORY;
		}
	} else {
		status = DSP_EMEMORY;
	}

	if (DSP_FAILED(status)) {
		bridge_chnl_destroy(chnl_mgr_obj);
		*phChnlMgr = NULL;
	} else {
		/* Return channel manager object to caller... */
		*phChnlMgr = chnl_mgr_obj;
	}
	return status;
}

/*
 *  ======== bridge_chnl_destroy ========
 *  Purpose:
 *      Close all open channels, and destroy the channel manager.
 */
dsp_status bridge_chnl_destroy(struct chnl_mgr *hchnl_mgr)
{
	dsp_status status = DSP_SOK;
	struct chnl_mgr *chnl_mgr_obj = hchnl_mgr;
	u32 chnl_id;

	if (MEM_IS_VALID_HANDLE(hchnl_mgr, CHNL_MGRSIGNATURE)) {
		/* Close all open channels: */
		for (chnl_id = 0; chnl_id < chnl_mgr_obj->max_channels;
		     chnl_id++) {
			status =
			    bridge_chnl_close(chnl_mgr_obj->ap_channel
					      [chnl_id]);
			if (DSP_FAILED(status))
				dev_dbg(bridge, "%s: Error status 0x%x\n",
					__func__, status);
		}
		/* release critical section */
		if (chnl_mgr_obj->hcs_obj)
			sync_delete_cs(chnl_mgr_obj->hcs_obj);

		/* Free channel manager object: */
		kfree(chnl_mgr_obj->ap_channel);

		/* Set hchnl_mgr to NULL in device object. */
		dev_set_chnl_mgr(chnl_mgr_obj->hdev_obj, NULL);
		/* Free this Chnl Mgr object: */
		MEM_FREE_OBJECT(hchnl_mgr);
	} else {
		status = DSP_EHANDLE;
	}
	return status;
}

/*
 *  ======== bridge_chnl_flush_io ========
 *  purpose:
 *      Flushes all the outstanding data requests on a channel.
 */
dsp_status bridge_chnl_flush_io(struct chnl_object *chnl_obj, u32 dwTimeOut)
{
	dsp_status status = DSP_SOK;
	struct chnl_object *pchnl = (struct chnl_object *)chnl_obj;
	short int chnl_mode = -1;
	struct chnl_mgr *chnl_mgr_obj;
	struct chnl_ioc chnl_ioc_obj;
	/* Check args: */
	if (MEM_IS_VALID_HANDLE(pchnl, CHNL_SIGNATURE)) {
		if ((dwTimeOut == CHNL_IOCNOWAIT)
		    && CHNL_IS_OUTPUT(pchnl->chnl_mode)) {
			status = DSP_EINVALIDARG;
		} else {
			chnl_mode = pchnl->chnl_mode;
			chnl_mgr_obj = pchnl->chnl_mgr_obj;
		}
	} else {
		status = DSP_EHANDLE;
	}
	if (DSP_SUCCEEDED(status)) {
		/* Note: Currently, if another thread continues to add IO
		 * requests to this channel, this function will continue to
		 * flush all such queued IO requests. */
		if (CHNL_IS_OUTPUT(chnl_mode)
		    && (pchnl->chnl_type == CHNL_PCPY)) {
			/* Wait for IO completions, up to the specified
			 * timeout: */
			while (!LST_IS_EMPTY(pchnl->pio_requests) &&
			       DSP_SUCCEEDED(status)) {
				status = bridge_chnl_get_ioc(chnl_obj, dwTimeOut,
							  &chnl_ioc_obj);
				if (DSP_FAILED(status))
					continue;

				if (chnl_ioc_obj.status & CHNL_IOCSTATTIMEOUT)
					status = CHNL_E_WAITTIMEOUT;

			}
		} else {
			status = bridge_chnl_cancel_io(chnl_obj);
			/* Now, leave the channel in the ready state: */
			pchnl->dw_state &= ~CHNL_STATECANCEL;
		}
	}
	DBC_ENSURE(DSP_FAILED(status) || LST_IS_EMPTY(pchnl->pio_requests));
	return status;
}

/*
 *  ======== bridge_chnl_get_info ========
 *  Purpose:
 *      Retrieve information related to a channel.
 */
dsp_status bridge_chnl_get_info(struct chnl_object *chnl_obj,
			     OUT struct chnl_info *pInfo)
{
	dsp_status status = DSP_SOK;
	struct chnl_object *pchnl = (struct chnl_object *)chnl_obj;
	if (pInfo != NULL) {
		if (MEM_IS_VALID_HANDLE(pchnl, CHNL_SIGNATURE)) {
			/* Return the requested information: */
			pInfo->hchnl_mgr = pchnl->chnl_mgr_obj;
			pInfo->event_obj = pchnl->user_event;
			pInfo->cnhl_id = pchnl->chnl_id;
			pInfo->dw_mode = pchnl->chnl_mode;
			pInfo->bytes_tx = pchnl->bytes_moved;
			pInfo->process = pchnl->process;
			pInfo->sync_event = pchnl->sync_event;
			pInfo->cio_cs = pchnl->cio_cs;
			pInfo->cio_reqs = pchnl->cio_reqs;
			pInfo->dw_state = pchnl->dw_state;
		} else {
			status = DSP_EHANDLE;
		}
	} else {
		status = DSP_EPOINTER;
	}
	return status;
}

/*
 *  ======== bridge_chnl_get_ioc ========
 *      Optionally wait for I/O completion on a channel.  Dequeue an I/O
 *      completion record, which contains information about the completed
 *      I/O request.
 *      Note: Ensures Channel Invariant (see notes above).
 */
dsp_status bridge_chnl_get_ioc(struct chnl_object *chnl_obj, u32 dwTimeOut,
			    OUT struct chnl_ioc *pIOC)
{
	dsp_status status = DSP_SOK;
	struct chnl_object *pchnl = (struct chnl_object *)chnl_obj;
	struct chnl_irp *chnl_packet_obj;
	dsp_status stat_sync;
	bool dequeue_ioc = true;
	struct chnl_ioc ioc = { NULL, 0, 0, 0, 0 };
	u8 *host_sys_buf = NULL;
	struct wmd_dev_context *dev_ctxt;
	struct dev_object *dev_obj;

	/* Check args: */
	if (pIOC == NULL) {
		status = DSP_EPOINTER;
	} else if (!MEM_IS_VALID_HANDLE(pchnl, CHNL_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else if (dwTimeOut == CHNL_IOCNOWAIT) {
		if (LST_IS_EMPTY(pchnl->pio_completions))
			status = CHNL_E_NOIOC;

	}

	dev_obj = dev_get_first();
	dev_get_wmd_context(dev_obj, &dev_ctxt);
	if (!dev_ctxt)
		status = DSP_EHANDLE;

	if (DSP_FAILED(status))
		goto func_end;

	ioc.status = CHNL_IOCSTATCOMPLETE;
	if (dwTimeOut !=
	    CHNL_IOCNOWAIT && LST_IS_EMPTY(pchnl->pio_completions)) {
		if (dwTimeOut == CHNL_IOCINFINITE)
			dwTimeOut = SYNC_INFINITE;

		stat_sync = sync_wait_on_event(pchnl->sync_event, dwTimeOut);
		if (stat_sync == DSP_ETIMEOUT) {
			/* No response from DSP */
			ioc.status |= CHNL_IOCSTATTIMEOUT;
			dequeue_ioc = false;
		} else if (stat_sync == DSP_EFAIL) {
			/* This can occur when the user mode thread is
			 * aborted (^C), or when _VWIN32_WaitSingleObject()
			 * fails due to unkown causes. */
			/* Even though Wait failed, there may be something in
			 * the Q: */
			if (LST_IS_EMPTY(pchnl->pio_completions)) {
				ioc.status |= CHNL_IOCSTATCANCEL;
				dequeue_ioc = false;
			}
		}
	}
	/* See comment in AddIOReq */
	sync_enter_cs(pchnl->chnl_mgr_obj->hcs_obj);
	omap_mbox_disable_irq(dev_ctxt->mbox, IRQ_RX);
	if (dequeue_ioc) {
		/* Dequeue IOC and set pIOC; */
		DBC_ASSERT(!LST_IS_EMPTY(pchnl->pio_completions));
		chnl_packet_obj =
		    (struct chnl_irp *)lst_get_head(pchnl->pio_completions);
		/* Update pIOC from channel state and chirp: */
		if (chnl_packet_obj) {
			pchnl->cio_cs--;
			/*  If this is a zero-copy channel, then set IOC's pbuf
			 *  to the DSP's address. This DSP address will get
			 *  translated to user's virtual addr later. */
			{
				host_sys_buf = chnl_packet_obj->host_sys_buf;
				ioc.pbuf = chnl_packet_obj->host_user_buf;
			}
			ioc.byte_size = chnl_packet_obj->byte_size;
			ioc.buf_size = chnl_packet_obj->buf_size;
			ioc.dw_arg = chnl_packet_obj->dw_arg;
			ioc.status |= chnl_packet_obj->status;
			/* Place the used chirp on the free list: */
			lst_put_tail(pchnl->free_packets_list,
				     (struct list_head *)chnl_packet_obj);
		} else {
			ioc.pbuf = NULL;
			ioc.byte_size = 0;
		}
	} else {
		ioc.pbuf = NULL;
		ioc.byte_size = 0;
		ioc.dw_arg = 0;
		ioc.buf_size = 0;
	}
	/* Ensure invariant: If any IOC's are queued for this channel... */
	if (!LST_IS_EMPTY(pchnl->pio_completions)) {
		/*  Since DSPStream_Reclaim() does not take a timeout
		 *  parameter, we pass the stream's timeout value to
		 *  bridge_chnl_get_ioc. We cannot determine whether or not
		 *  we have waited in User mode. Since the stream's timeout
		 *  value may be non-zero, we still have to set the event.
		 *  Therefore, this optimization is taken out.
		 *
		 *  if (dwTimeOut == CHNL_IOCNOWAIT) {
		 *    ... ensure event is set..
		 *      sync_set_event(pchnl->sync_event);
		 *  } */
		sync_set_event(pchnl->sync_event);
	} else {
		/* else, if list is empty, ensure event is reset. */
		sync_reset_event(pchnl->sync_event);
	}
	omap_mbox_enable_irq(dev_ctxt->mbox, IRQ_RX);
	sync_leave_cs(pchnl->chnl_mgr_obj->hcs_obj);
	if (dequeue_ioc
	    && (pchnl->chnl_type == CHNL_PCPY && pchnl->chnl_id > 1)) {
		if (!(ioc.pbuf < (void *)USERMODE_ADDR))
			goto func_cont;

		/* If the addr is in user mode, then copy it */
		if (!host_sys_buf || !ioc.pbuf) {
			status = DSP_EPOINTER;
			goto func_cont;
		}
		if (!CHNL_IS_INPUT(pchnl->chnl_mode))
			goto func_cont1;

		/*host_user_buf */
		status = copy_to_user(ioc.pbuf, host_sys_buf, ioc.byte_size);
		if (status) {
			if (current->flags & PF_EXITING)
				status = 0;
		}
		if (status)
			status = DSP_EPOINTER;
func_cont1:
		kfree(host_sys_buf);
	}
func_cont:
	/* Update User's IOC block: */
	*pIOC = ioc;
func_end:
	return status;
}

/*
 *  ======== bridge_chnl_get_mgr_info ========
 *      Retrieve information related to the channel manager.
 */
dsp_status bridge_chnl_get_mgr_info(struct chnl_mgr *hchnl_mgr, u32 uChnlID,
				 OUT struct chnl_mgrinfo *pMgrInfo)
{
	dsp_status status = DSP_SOK;
	struct chnl_mgr *chnl_mgr_obj = (struct chnl_mgr *)hchnl_mgr;

	if (pMgrInfo != NULL) {
		if (uChnlID <= CHNL_MAXCHANNELS) {
			if (MEM_IS_VALID_HANDLE(hchnl_mgr, CHNL_MGRSIGNATURE)) {
				/* Return the requested information: */
				pMgrInfo->chnl_obj =
				    chnl_mgr_obj->ap_channel[uChnlID];
				pMgrInfo->open_channels =
				    chnl_mgr_obj->open_channels;
				pMgrInfo->dw_type = chnl_mgr_obj->dw_type;
				/* total # of chnls */
				pMgrInfo->max_channels =
				    chnl_mgr_obj->max_channels;
			} else {
				status = DSP_EHANDLE;
			}
		} else {
			status = CHNL_E_BADCHANID;
		}
	} else {
		status = DSP_EPOINTER;
	}

	return status;
}

/*
 *  ======== bridge_chnl_idle ========
 *      Idles a particular channel.
 */
dsp_status bridge_chnl_idle(struct chnl_object *chnl_obj, u32 dwTimeOut,
			    bool fFlush)
{
	short int chnl_mode;
	struct chnl_mgr *chnl_mgr_obj;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(MEM_IS_VALID_HANDLE(chnl_obj, CHNL_SIGNATURE));

	chnl_mode = chnl_obj->chnl_mode;
	chnl_mgr_obj = chnl_obj->chnl_mgr_obj;

	if (CHNL_IS_OUTPUT(chnl_mode) && !fFlush) {
		/* Wait for IO completions, up to the specified timeout: */
		status = bridge_chnl_flush_io(chnl_obj, dwTimeOut);
	} else {
		status = bridge_chnl_cancel_io(chnl_obj);

		/* Reset the byte count and put channel back in ready state. */
		chnl_obj->bytes_moved = 0;
		chnl_obj->dw_state &= ~CHNL_STATECANCEL;
	}

	return status;
}

/*
 *  ======== bridge_chnl_open ========
 *      Open a new half-duplex channel to the DSP board.
 */
dsp_status bridge_chnl_open(OUT struct chnl_object **phChnl,
			    struct chnl_mgr *hchnl_mgr, short int chnl_mode,
			    u32 uChnlId, CONST IN struct chnl_attr *pattrs)
{
	dsp_status status = DSP_SOK;
	struct chnl_mgr *chnl_mgr_obj = hchnl_mgr;
	struct chnl_object *pchnl = NULL;
	struct sync_attrs *sync_attr_obj = NULL;
	struct sync_object *sync_event = NULL;
	/* Ensure DBC requirements: */
	DBC_REQUIRE(phChnl != NULL);
	DBC_REQUIRE(pattrs != NULL);
	DBC_REQUIRE(hchnl_mgr != NULL);
	*phChnl = NULL;
	/* Validate Args: */
	if (pattrs->uio_reqs == 0) {
		status = DSP_EINVALIDARG;
	} else {
		if (!MEM_IS_VALID_HANDLE(hchnl_mgr, CHNL_MGRSIGNATURE)) {
			status = DSP_EHANDLE;
		} else {
			if (uChnlId != CHNL_PICKFREE) {
				if (uChnlId >= chnl_mgr_obj->max_channels)
					status = CHNL_E_BADCHANID;
				else if (chnl_mgr_obj->ap_channel[uChnlId] !=
					 NULL)
					status = CHNL_E_CHANBUSY;
			} else {
				/* Check for free channel */
				status =
				    search_free_channel(chnl_mgr_obj, &uChnlId);
			}
		}
	}
	if (DSP_FAILED(status))
		goto func_end;

	DBC_ASSERT(uChnlId < chnl_mgr_obj->max_channels);
	/* Create channel object: */
	MEM_ALLOC_OBJECT(pchnl, struct chnl_object, 0x0000);
	if (!pchnl) {
		status = DSP_EMEMORY;
		goto func_end;
	}
	/* Protect queues from io_dpc: */
	pchnl->dw_state = CHNL_STATECANCEL;
	/* Allocate initial IOR and IOC queues: */
	pchnl->free_packets_list = create_chirp_list(pattrs->uio_reqs);
	pchnl->pio_requests = create_chirp_list(0);
	pchnl->pio_completions = create_chirp_list(0);
	pchnl->chnl_packets = pattrs->uio_reqs;
	pchnl->cio_cs = 0;
	pchnl->cio_reqs = 0;
	status = sync_open_event(&sync_event, sync_attr_obj);
	if (DSP_SUCCEEDED(status))
		status = ntfy_create(&pchnl->ntfy_obj);

	if (DSP_SUCCEEDED(status)) {
		if (pchnl->pio_completions && pchnl->pio_requests &&
		    pchnl->free_packets_list) {
			/* Initialize CHNL object fields: */
			pchnl->chnl_mgr_obj = chnl_mgr_obj;
			pchnl->chnl_id = uChnlId;
			pchnl->chnl_mode = chnl_mode;
			pchnl->user_event = sync_event;	/* for Linux */
			pchnl->sync_event = sync_event;
			/* Get the process handle */
			pchnl->process = current->tgid;
			pchnl->pcb_arg = 0;
			pchnl->bytes_moved = 0;
			/* Default to proc-copy */
			pchnl->chnl_type = CHNL_PCPY;
		} else {
			status = DSP_EMEMORY;
		}
	}

	if (DSP_FAILED(status)) {
		/* Free memory */
		if (pchnl->pio_completions) {
			free_chirp_list(pchnl->pio_completions);
			pchnl->pio_completions = NULL;
			pchnl->cio_cs = 0;
		}
		if (pchnl->pio_requests) {
			free_chirp_list(pchnl->pio_requests);
			pchnl->pio_requests = NULL;
		}
		if (pchnl->free_packets_list) {
			free_chirp_list(pchnl->free_packets_list);
			pchnl->free_packets_list = NULL;
		}
		if (sync_event) {
			sync_close_event(sync_event);
			sync_event = NULL;
		}
		if (pchnl->ntfy_obj) {
			ntfy_delete(pchnl->ntfy_obj);
			pchnl->ntfy_obj = NULL;
		}
		MEM_FREE_OBJECT(pchnl);
	} else {
		/* Insert channel object in channel manager: */
		chnl_mgr_obj->ap_channel[pchnl->chnl_id] = pchnl;
		sync_enter_cs(chnl_mgr_obj->hcs_obj);
		chnl_mgr_obj->open_channels++;
		sync_leave_cs(chnl_mgr_obj->hcs_obj);
		/* Return result... */
		pchnl->dw_signature = CHNL_SIGNATURE;
		pchnl->dw_state = CHNL_STATEREADY;
		*phChnl = pchnl;
	}
func_end:
	DBC_ENSURE((DSP_SUCCEEDED(status) &&
		    MEM_IS_VALID_HANDLE(pchnl, CHNL_SIGNATURE)) ||
		   (*phChnl == NULL));
	return status;
}

/*
 *  ======== bridge_chnl_register_notify ========
 *      Registers for events on a particular channel.
 */
dsp_status bridge_chnl_register_notify(struct chnl_object *chnl_obj,
				    u32 event_mask, u32 notify_type,
				    struct dsp_notification *hnotification)
{
	dsp_status status = DSP_SOK;

	DBC_ASSERT(!(event_mask & ~(DSP_STREAMDONE | DSP_STREAMIOCOMPLETION)));

	status = ntfy_register(chnl_obj->ntfy_obj, hnotification, event_mask,
			       notify_type);

	return status;
}

/*
 *  ======== create_chirp_list ========
 *  Purpose:
 *      Initialize a queue of channel I/O Request/Completion packets.
 *  Parameters:
 *      uChirps:    Number of Chirps to allocate.
 *  Returns:
 *      Pointer to queue of IRPs, or NULL.
 *  Requires:
 *  Ensures:
 */
static struct lst_list *create_chirp_list(u32 uChirps)
{
	struct lst_list *chirp_list;
	struct chnl_irp *chnl_packet_obj;
	u32 i;

	chirp_list = mem_calloc(sizeof(struct lst_list), MEM_NONPAGED);

	if (chirp_list) {
		INIT_LIST_HEAD(&chirp_list->head);
		/* Make N chirps and place on queue. */
		for (i = 0; (i < uChirps)
		     && ((chnl_packet_obj = make_new_chirp()) != NULL); i++) {
			lst_put_tail(chirp_list,
				     (struct list_head *)chnl_packet_obj);
		}

		/* If we couldn't allocate all chirps, free those allocated: */
		if (i != uChirps) {
			free_chirp_list(chirp_list);
			chirp_list = NULL;
		}
	}

	return chirp_list;
}

/*
 *  ======== free_chirp_list ========
 *  Purpose:
 *      Free the queue of Chirps.
 */
static void free_chirp_list(struct lst_list *chirp_list)
{
	DBC_REQUIRE(chirp_list != NULL);

	while (!LST_IS_EMPTY(chirp_list))
		kfree(lst_get_head(chirp_list));

	kfree(chirp_list);
}

/*
 *  ======== make_new_chirp ========
 *      Allocate the memory for a new channel IRP.
 */
static struct chnl_irp *make_new_chirp(void)
{
	struct chnl_irp *chnl_packet_obj;

	chnl_packet_obj =
	    (struct chnl_irp *)mem_calloc(sizeof(struct chnl_irp),
					  MEM_NONPAGED);
	if (chnl_packet_obj != NULL) {
		/* lst_init_elem only resets the list's member values. */
		lst_init_elem(&chnl_packet_obj->link);
	}

	return chnl_packet_obj;
}

/*
 *  ======== search_free_channel ========
 *      Search for a free channel slot in the array of channel pointers.
 */
static dsp_status search_free_channel(struct chnl_mgr *chnl_mgr_obj,
				      OUT u32 *pdwChnl)
{
	dsp_status status = CHNL_E_OUTOFSTREAMS;
	u32 i;

	DBC_REQUIRE(MEM_IS_VALID_HANDLE(chnl_mgr_obj, CHNL_MGRSIGNATURE));

	for (i = 0; i < chnl_mgr_obj->max_channels; i++) {
		if (chnl_mgr_obj->ap_channel[i] == NULL) {
			status = DSP_SOK;
			*pdwChnl = i;
			break;
		}
	}

	return status;
}
