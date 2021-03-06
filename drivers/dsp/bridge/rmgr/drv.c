/*
 * drv.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge resource allocation module.
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

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/reg.h>

/*  ----------------------------------- This */
#include <dspbridge/drv.h>
#include <dspbridge/dev.h>

#include <dspbridge/node.h>
#include <dspbridge/proc.h>
#include <dspbridge/strm.h>
#include <dspbridge/nodepriv.h>
#include <dspbridge/wmdchnl.h>
#include <dspbridge/resourcecleanup.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define SIGNATURE   0x5f52474d	/* "DRV_" (in reverse) */

struct drv_object {
	u32 dw_signature;
	struct lst_list *dev_list;
	struct lst_list *dev_node_string;
};

/*
 *  This is the Device Extension. Named with the Prefix
 *  DRV_ since it is living in this module
 */
struct drv_ext {
	struct list_head link;
	char sz_string[MAXREGPATHLENGTH];
};

/*  ----------------------------------- Globals */
static s32 refs;

/*  ----------------------------------- Function Prototypes */
static dsp_status request_bridge_resources(u32 dw_context, s32 fRequest);
static dsp_status request_bridge_resources_dsp(u32 dw_context, s32 fRequest);

/* GPP PROCESS CLEANUP CODE */

static dsp_status drv_proc_free_node_res(bhandle hPCtxt);
extern enum node_state node_get_state(bhandle hnode);

/* Allocate and add a node resource element
* This function is called from .Node_Allocate. */
dsp_status drv_insert_node_res_element(bhandle hnode, bhandle hNodeRes,
				       bhandle hPCtxt)
{
	struct node_res_object **node_res_obj =
	    (struct node_res_object **)hNodeRes;
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	dsp_status status = DSP_SOK;
	struct node_res_object *temp_node_res = NULL;

	*node_res_obj = (struct node_res_object *)mem_calloc
	    (1 * sizeof(struct node_res_object), MEM_PAGED);
	if (*node_res_obj == NULL)
		status = DSP_EHANDLE;

	if (DSP_SUCCEEDED(status)) {
		if (mutex_lock_interruptible(&ctxt->node_mutex)) {
			kfree(*node_res_obj);
			return DSP_EFAIL;
		}
		(*node_res_obj)->hnode = hnode;
		if (ctxt->node_list != NULL) {
			temp_node_res = ctxt->node_list;
			while (temp_node_res->next != NULL)
				temp_node_res = temp_node_res->next;

			temp_node_res->next = *node_res_obj;
		} else {
			ctxt->node_list = *node_res_obj;
		}
		mutex_unlock(&ctxt->node_mutex);
	}

	return status;
}

/* Release all Node resources and its context
* This is called from .Node_Delete. */
dsp_status drv_remove_node_res_element(bhandle hNodeRes, bhandle hPCtxt)
{
	struct node_res_object *node_res_obj =
	    (struct node_res_object *)hNodeRes;
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	struct node_res_object *temp_node;
	dsp_status status = DSP_SOK;

	if (mutex_lock_interruptible(&ctxt->node_mutex))
		return DSP_EFAIL;
	temp_node = ctxt->node_list;
	if (temp_node == node_res_obj) {
		ctxt->node_list = node_res_obj->next;
	} else {
		while (temp_node && temp_node->next != node_res_obj)
			temp_node = temp_node->next;
		if (!temp_node)
			status = DSP_ENOTFOUND;
		else
			temp_node->next = node_res_obj->next;
	}
	mutex_unlock(&ctxt->node_mutex);
	kfree(node_res_obj);
	return status;
}

/* Actual Node De-Allocation */
static dsp_status drv_proc_free_node_res(bhandle hPCtxt)
{
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	dsp_status status = DSP_SOK;
	struct node_res_object *node_list = NULL;
	struct node_res_object *node_res_obj = NULL;
	u32 node_state;

	node_list = ctxt->node_list;
	while (node_list != NULL) {
		node_res_obj = node_list;
		node_list = node_list->next;
		if (node_res_obj->node_allocated) {
			node_state = node_get_state(node_res_obj->hnode);
			if (node_state <= NODE_DELETING) {
				if ((node_state == NODE_RUNNING) ||
				    (node_state == NODE_PAUSED) ||
				    (node_state == NODE_TERMINATING))
					status = node_terminate
					    (node_res_obj->hnode, &status);
				status = node_delete(node_res_obj->hnode, ctxt);
			}
		}
	}
	return status;
}

/* Release all Mapped and Reserved DMM resources */
dsp_status drv_remove_all_dmm_res_elements(bhandle hPCtxt)
{
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	dsp_status status = DSP_SOK;
	struct dmm_map_object *temp_map, *map_obj;
	struct dmm_rsv_object *temp_rsv, *rsv_obj;

	/* Free DMM mapped memory resources */
	list_for_each_entry_safe(map_obj, temp_map, &ctxt->dmm_map_list, link) {
		status = proc_un_map(ctxt->hprocessor,
				     (void *)map_obj->dsp_addr, ctxt);
		if (DSP_FAILED(status))
			pr_err("%s: proc_un_map failed!"
			       " status = 0x%xn", __func__, status);
	}

	/* Free DMM reserved memory resources */
	list_for_each_entry_safe(rsv_obj, temp_rsv, &ctxt->dmm_rsv_list, link) {
		status = proc_un_reserve_memory(ctxt->hprocessor, (void *)
						rsv_obj->dsp_reserved_addr,
						ctxt);
		if (DSP_FAILED(status))
			pr_err("%s: proc_un_reserve_memory failed!"
			       " status = 0x%xn", __func__, status);
	}
	return status;
}

/* Update Node allocation status */
void drv_proc_node_update_status(bhandle hNodeRes, s32 status)
{
	struct node_res_object *node_res_obj =
	    (struct node_res_object *)hNodeRes;
	DBC_ASSERT(hNodeRes != NULL);
	node_res_obj->node_allocated = status;
}

/* Update Node Heap status */
void drv_proc_node_update_heap_status(bhandle hNodeRes, s32 status)
{
	struct node_res_object *node_res_obj =
	    (struct node_res_object *)hNodeRes;
	DBC_ASSERT(hNodeRes != NULL);
	node_res_obj->heap_allocated = status;
}

/* Release all Node resources and its context
* This is called from .bridge_release.
 */
dsp_status drv_remove_all_node_res_elements(bhandle hPCtxt)
{
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	dsp_status status = DSP_SOK;
	struct node_res_object *temp_node2 = NULL;
	struct node_res_object *temp_node = NULL;

	drv_proc_free_node_res(ctxt);
	temp_node = ctxt->node_list;
	while (temp_node != NULL) {
		temp_node2 = temp_node;
		temp_node = temp_node->next;
		kfree(temp_node2);
	}
	ctxt->node_list = NULL;
	return status;
}

/* Getting the node resource element */
dsp_status drv_get_node_res_element(bhandle hnode, bhandle hNodeRes,
				    bhandle hPCtxt)
{
	struct node_res_object **node_res = (struct node_res_object **)hNodeRes;
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	dsp_status status = DSP_SOK;
	struct node_res_object *temp_node2 = NULL;
	struct node_res_object *temp_node = NULL;

	if (mutex_lock_interruptible(&ctxt->node_mutex))
		return DSP_EFAIL;

	temp_node = ctxt->node_list;
	while ((temp_node != NULL) && (temp_node->hnode != hnode)) {
		temp_node2 = temp_node;
		temp_node = temp_node->next;
	}

	mutex_unlock(&ctxt->node_mutex);

	if (temp_node != NULL)
		*node_res = temp_node;
	else
		status = DSP_ENOTFOUND;

	return status;
}

/* Allocate the STRM resource element
* This is called after the actual resource is allocated
 */
dsp_status drv_proc_insert_strm_res_element(bhandle hStreamHandle,
					    bhandle hstrm_res, bhandle hPCtxt)
{
	struct strm_res_object **pstrm_res =
	    (struct strm_res_object **)hstrm_res;
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	dsp_status status = DSP_SOK;
	struct strm_res_object *temp_strm_res = NULL;

	*pstrm_res = (struct strm_res_object *)
	    mem_calloc(1 * sizeof(struct strm_res_object), MEM_PAGED);
	if (*pstrm_res == NULL)
		status = DSP_EHANDLE;

	if (DSP_SUCCEEDED(status)) {
		if (mutex_lock_interruptible(&ctxt->strm_mutex)) {
			kfree(*pstrm_res);
			return DSP_EFAIL;
		}
		(*pstrm_res)->hstream = hStreamHandle;
		if (ctxt->pstrm_list != NULL) {
			temp_strm_res = ctxt->pstrm_list;
			while (temp_strm_res->next != NULL)
				temp_strm_res = temp_strm_res->next;

			temp_strm_res->next = *pstrm_res;
		} else {
			ctxt->pstrm_list = *pstrm_res;
		}
		mutex_unlock(&ctxt->strm_mutex);
	}
	return status;
}

/* Release Stream resource element context
* This function called after the actual resource is freed
 */
dsp_status drv_proc_remove_strm_res_element(bhandle hstrm_res, bhandle hPCtxt)
{
	struct strm_res_object *pstrm_res = (struct strm_res_object *)hstrm_res;
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	struct strm_res_object *temp_strm_res;
	dsp_status status = DSP_SOK;

	if (mutex_lock_interruptible(&ctxt->strm_mutex))
		return DSP_EFAIL;
	temp_strm_res = ctxt->pstrm_list;

	if (ctxt->pstrm_list == pstrm_res) {
		ctxt->pstrm_list = pstrm_res->next;
	} else {
		while (temp_strm_res && temp_strm_res->next != pstrm_res)
			temp_strm_res = temp_strm_res->next;
		if (temp_strm_res == NULL)
			status = DSP_ENOTFOUND;
		else
			temp_strm_res->next = pstrm_res->next;
	}
	mutex_unlock(&ctxt->strm_mutex);
	kfree(pstrm_res);
	return status;
}

/* Release all Stream resources and its context
* This is called from .bridge_release.
 */
dsp_status drv_remove_all_strm_res_elements(bhandle hPCtxt)
{
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	dsp_status status = DSP_SOK;
	struct strm_res_object *strm_res = NULL;
	struct strm_res_object *strm_tmp = NULL;
	struct stream_info strm_info;
	struct dsp_streaminfo user;
	u8 **ap_buffer = NULL;
	u8 *buf_ptr;
	u32 ul_bytes;
	u32 dw_arg;
	s32 ul_buf_size;

	strm_tmp = ctxt->pstrm_list;
	while (strm_tmp) {
		strm_res = strm_tmp;
		strm_tmp = strm_tmp->next;
		if (strm_res->num_bufs) {
			ap_buffer = mem_alloc((strm_res->num_bufs *
					       sizeof(u8 *)), MEM_NONPAGED);
			if (ap_buffer) {
				status = strm_free_buffer(strm_res->hstream,
							  ap_buffer,
							  strm_res->num_bufs,
							  ctxt);
				kfree(ap_buffer);
			}
		}
		strm_info.user_strm = &user;
		user.number_bufs_in_stream = 0;
		strm_get_info(strm_res->hstream, &strm_info, sizeof(strm_info));
		while (user.number_bufs_in_stream--)
			strm_reclaim(strm_res->hstream, &buf_ptr, &ul_bytes,
				     (u32 *) &ul_buf_size, &dw_arg);
		status = strm_close(strm_res->hstream, ctxt);
	}
	return status;
}

/* Getting the stream resource element */
dsp_status drv_get_strm_res_element(bhandle hStrm, bhandle hstrm_res,
				    bhandle hPCtxt)
{
	struct strm_res_object **strm_res =
	    (struct strm_res_object **)hstrm_res;
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	dsp_status status = DSP_SOK;
	struct strm_res_object *temp_strm2 = NULL;
	struct strm_res_object *temp_strm;

	if (mutex_lock_interruptible(&ctxt->strm_mutex))
		return DSP_EFAIL;

	temp_strm = ctxt->pstrm_list;
	while ((temp_strm != NULL) && (temp_strm->hstream != hStrm)) {
		temp_strm2 = temp_strm;
		temp_strm = temp_strm->next;
	}

	mutex_unlock(&ctxt->strm_mutex);

	if (temp_strm != NULL)
		*strm_res = temp_strm;
	else
		status = DSP_ENOTFOUND;

	return status;
}

/* Updating the stream resource element */
dsp_status drv_proc_update_strm_res(u32 num_bufs, bhandle hstrm_res)
{
	dsp_status status = DSP_SOK;
	struct strm_res_object **strm_res =
	    (struct strm_res_object **)hstrm_res;

	(*strm_res)->num_bufs = num_bufs;
	return status;
}

/* GPP PROCESS CLEANUP CODE END */

/*
 *  ======== = drv_create ======== =
 *  Purpose:
 *      DRV Object gets created only once during Driver Loading.
 */
dsp_status drv_create(OUT struct drv_object **phDRVObject)
{
	dsp_status status = DSP_SOK;
	struct drv_object *pdrv_object = NULL;

	DBC_REQUIRE(phDRVObject != NULL);
	DBC_REQUIRE(refs > 0);

	MEM_ALLOC_OBJECT(pdrv_object, struct drv_object, SIGNATURE);
	if (pdrv_object) {
		/* Create and Initialize List of device objects */
		pdrv_object->dev_list = mem_calloc(sizeof(struct lst_list),
						   MEM_NONPAGED);
		if (pdrv_object->dev_list) {
			/* Create and Initialize List of device Extension */
			pdrv_object->dev_node_string =
					mem_calloc(sizeof(struct lst_list),
						   MEM_NONPAGED);
			if (!(pdrv_object->dev_node_string)) {
				status = DSP_EFAIL;
			} else {
				INIT_LIST_HEAD(&pdrv_object->
					       dev_node_string->head);
				INIT_LIST_HEAD(&pdrv_object->dev_list->head);
			}
		} else {
			status = DSP_EMEMORY;
		}
	} else {
		status = DSP_EMEMORY;
	}
	/* Store the DRV Object in the Registry */
	if (DSP_SUCCEEDED(status))
		status = cfg_set_object((u32) pdrv_object, REG_DRV_OBJECT);
	if (DSP_SUCCEEDED(status)) {
		*phDRVObject = pdrv_object;
	} else {
		kfree(pdrv_object->dev_list);
		kfree(pdrv_object->dev_node_string);
		/* Free the DRV Object */
		kfree(pdrv_object);
	}

	DBC_ENSURE(DSP_FAILED(status) ||
		   MEM_IS_VALID_HANDLE(pdrv_object, SIGNATURE));
	return status;
}

/*
 *  ======== drv_exit ========
 *  Purpose:
 *      Discontinue usage of the DRV module.
 */
void drv_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== = drv_destroy ======== =
 *  purpose:
 *      Invoked during bridge de-initialization
 */
dsp_status drv_destroy(struct drv_object *hDRVObject)
{
	dsp_status status = DSP_SOK;
	struct drv_object *pdrv_object = (struct drv_object *)hDRVObject;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(MEM_IS_VALID_HANDLE(pdrv_object, SIGNATURE));

	/*
	 *  Delete the List if it exists.Should not come here
	 *  as the drv_remove_dev_object and the Last drv_request_resources
	 *  removes the list if the lists are empty.
	 */
	kfree(pdrv_object->dev_list);
	kfree(pdrv_object->dev_node_string);
	MEM_FREE_OBJECT(pdrv_object);
	/* Update the DRV Object in Registry to be 0 */
	(void)cfg_set_object(0, REG_DRV_OBJECT);
	DBC_ENSURE(!MEM_IS_VALID_HANDLE(pdrv_object, SIGNATURE));
	return status;
}

/*
 *  ======== drv_get_dev_object ========
 *  Purpose:
 *      Given a index, returns a handle to DevObject from the list.
 */
dsp_status drv_get_dev_object(u32 index, struct drv_object *hdrv_obj,
			      struct dev_object **phDevObject)
{
	dsp_status status = DSP_SOK;
#ifdef CONFIG_BRIDGE_DEBUG
	/* used only for Assertions and debug messages */
	struct drv_object *pdrv_obj = (struct drv_object *)hdrv_obj;
#endif
	struct dev_object *dev_obj;
	u32 i;
	DBC_REQUIRE(MEM_IS_VALID_HANDLE(pdrv_obj, SIGNATURE));
	DBC_REQUIRE(phDevObject != NULL);
	DBC_REQUIRE(index >= 0);
	DBC_REQUIRE(refs > 0);
	DBC_ASSERT(!(LST_IS_EMPTY(pdrv_obj->dev_list)));

	dev_obj = (struct dev_object *)drv_get_first_dev_object();
	for (i = 0; i < index; i++) {
		dev_obj =
		    (struct dev_object *)drv_get_next_dev_object((u32) dev_obj);
	}
	if (dev_obj) {
		*phDevObject = (struct dev_object *)dev_obj;
	} else {
		*phDevObject = NULL;
		status = DSP_EFAIL;
	}

	return status;
}

/*
 *  ======== drv_get_first_dev_object ========
 *  Purpose:
 *      Retrieve the first Device Object handle from an internal linked list of
 *      of DEV_OBJECTs maintained by DRV.
 */
u32 drv_get_first_dev_object(void)
{
	u32 dw_dev_object = 0;
	struct drv_object *pdrv_obj;

	if (DSP_SUCCEEDED(cfg_get_object((u32 *) &pdrv_obj, REG_DRV_OBJECT))) {
		if ((pdrv_obj->dev_list != NULL) &&
		    !LST_IS_EMPTY(pdrv_obj->dev_list))
			dw_dev_object = (u32) lst_first(pdrv_obj->dev_list);
	}

	return dw_dev_object;
}

/*
 *  ======== DRV_GetFirstDevNodeString ========
 *  Purpose:
 *      Retrieve the first Device Extension from an internal linked list of
 *      of Pointer to dev_node Strings maintained by DRV.
 */
u32 drv_get_first_dev_extension(void)
{
	u32 dw_dev_extension = 0;
	struct drv_object *pdrv_obj;

	if (DSP_SUCCEEDED(cfg_get_object((u32 *) &pdrv_obj, REG_DRV_OBJECT))) {

		if ((pdrv_obj->dev_node_string != NULL) &&
		    !LST_IS_EMPTY(pdrv_obj->dev_node_string)) {
			dw_dev_extension =
			    (u32) lst_first(pdrv_obj->dev_node_string);
		}
	}

	return dw_dev_extension;
}

/*
 *  ======== drv_get_next_dev_object ========
 *  Purpose:
 *      Retrieve the next Device Object handle from an internal linked list of
 *      of DEV_OBJECTs maintained by DRV, after having previously called
 *      drv_get_first_dev_object() and zero or more DRV_GetNext.
 */
u32 drv_get_next_dev_object(u32 hdev_obj)
{
	u32 dw_next_dev_object = 0;
	struct drv_object *pdrv_obj;

	DBC_REQUIRE(hdev_obj != 0);

	if (DSP_SUCCEEDED(cfg_get_object((u32 *) &pdrv_obj, REG_DRV_OBJECT))) {

		if ((pdrv_obj->dev_list != NULL) &&
		    !LST_IS_EMPTY(pdrv_obj->dev_list)) {
			dw_next_dev_object = (u32) lst_next(pdrv_obj->dev_list,
							    (struct list_head *)
							    hdev_obj);
		}
	}
	return dw_next_dev_object;
}

/*
 *  ======== drv_get_next_dev_extension ========
 *  Purpose:
 *      Retrieve the next Device Extension from an internal linked list of
 *      of pointer to DevNodeString maintained by DRV, after having previously
 *      called drv_get_first_dev_extension() and zero or more
 *      drv_get_next_dev_extension().
 */
u32 drv_get_next_dev_extension(u32 hDevExtension)
{
	u32 dw_dev_extension = 0;
	struct drv_object *pdrv_obj;

	DBC_REQUIRE(hDevExtension != 0);

	if (DSP_SUCCEEDED(cfg_get_object((u32 *) &pdrv_obj, REG_DRV_OBJECT))) {
		if ((pdrv_obj->dev_node_string != NULL) &&
		    !LST_IS_EMPTY(pdrv_obj->dev_node_string)) {
			dw_dev_extension =
			    (u32) lst_next(pdrv_obj->dev_node_string,
					   (struct list_head *)hDevExtension);
		}
	}

	return dw_dev_extension;
}

/*
 *  ======== drv_init ========
 *  Purpose:
 *      Initialize DRV module private state.
 */
dsp_status drv_init(void)
{
	s32 ret = 1;		/* function return value */

	DBC_REQUIRE(refs >= 0);

	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs >= 0)));

	return ret;
}

/*
 *  ======== drv_insert_dev_object ========
 *  Purpose:
 *      Insert a DevObject into the list of Manager object.
 */
dsp_status drv_insert_dev_object(struct drv_object *hDRVObject,
				 struct dev_object *hdev_obj)
{
	dsp_status status = DSP_SOK;
	struct drv_object *pdrv_object = (struct drv_object *)hDRVObject;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hdev_obj != NULL);
	DBC_REQUIRE(MEM_IS_VALID_HANDLE(pdrv_object, SIGNATURE));
	DBC_ASSERT(pdrv_object->dev_list);

	lst_put_tail(pdrv_object->dev_list, (struct list_head *)hdev_obj);

	DBC_ENSURE(DSP_SUCCEEDED(status)
		   && !LST_IS_EMPTY(pdrv_object->dev_list));

	return status;
}

/*
 *  ======== drv_remove_dev_object ========
 *  Purpose:
 *      Search for and remove a DeviceObject from the given list of DRV
 *      objects.
 */
dsp_status drv_remove_dev_object(struct drv_object *hDRVObject,
				 struct dev_object *hdev_obj)
{
	dsp_status status = DSP_EFAIL;
	struct drv_object *pdrv_object = (struct drv_object *)hDRVObject;
	struct list_head *cur_elem;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(MEM_IS_VALID_HANDLE(pdrv_object, SIGNATURE));
	DBC_REQUIRE(hdev_obj != NULL);

	DBC_REQUIRE(pdrv_object->dev_list != NULL);
	DBC_REQUIRE(!LST_IS_EMPTY(pdrv_object->dev_list));

	/* Search list for p_proc_object: */
	for (cur_elem = lst_first(pdrv_object->dev_list); cur_elem != NULL;
	     cur_elem = lst_next(pdrv_object->dev_list, cur_elem)) {
		/* If found, remove it. */
		if ((struct dev_object *)cur_elem == hdev_obj) {
			lst_remove_elem(pdrv_object->dev_list, cur_elem);
			status = DSP_SOK;
			break;
		}
	}
	/* Remove list if empty. */
	if (LST_IS_EMPTY(pdrv_object->dev_list)) {
		kfree(pdrv_object->dev_list);
		pdrv_object->dev_list = NULL;
	}
	DBC_ENSURE((pdrv_object->dev_list == NULL) ||
		   !LST_IS_EMPTY(pdrv_object->dev_list));

	return status;
}

/*
 *  ======== drv_request_resources ========
 *  Purpose:
 *      Requests  resources from the OS.
 */
dsp_status drv_request_resources(u32 dw_context, u32 *pDevNodeString)
{
	dsp_status status = DSP_SOK;
	struct drv_object *pdrv_object;
	struct drv_ext *pszdev_node;

	DBC_REQUIRE(dw_context != 0);
	DBC_REQUIRE(pDevNodeString != NULL);

	/*
	 *  Allocate memory to hold the string. This will live untill
	 *  it is freed in the Release resources. Update the driver object
	 *  list.
	 */

	status = cfg_get_object((u32 *) &pdrv_object, REG_DRV_OBJECT);
	if (DSP_SUCCEEDED(status)) {
		pszdev_node = mem_calloc(sizeof(struct drv_ext), MEM_NONPAGED);
		if (pszdev_node) {
			lst_init_elem(&pszdev_node->link);
			strncpy(pszdev_node->sz_string,
				(char *)dw_context, MAXREGPATHLENGTH - 1);
			pszdev_node->sz_string[MAXREGPATHLENGTH - 1] = '\0';
			/* Update the Driver Object List */
			*pDevNodeString = (u32) pszdev_node->sz_string;
			lst_put_tail(pdrv_object->dev_node_string,
				     (struct list_head *)pszdev_node);
		} else {
			status = DSP_EMEMORY;
			*pDevNodeString = 0;
		}
	} else {
		dev_dbg(bridge, "%s: Failed to get Driver Object from Registry",
			__func__);
		*pDevNodeString = 0;
	}

	if (!(strcmp((char *)dw_context, "TIOMAP1510"))) {
		dev_dbg(bridge, "%s: Allocating resources for UMA\n", __func__);
		status = request_bridge_resources_dsp(dw_context, DRV_ASSIGN);
	} else {
		status = DSP_EFAIL;
		dev_dbg(bridge, "%s: Unknown Device\n", __func__);
	}

	if (DSP_FAILED(status))
		dev_dbg(bridge, "%s: Failed to reserve bridge resources\n",
			__func__);

	DBC_ENSURE((DSP_SUCCEEDED(status) && pDevNodeString != NULL &&
		    !LST_IS_EMPTY(pdrv_object->dev_node_string)) ||
		   (DSP_FAILED(status) && *pDevNodeString == 0));

	return status;
}

/*
 *  ======== drv_release_resources ========
 *  Purpose:
 *      Releases  resources from the OS.
 */
dsp_status drv_release_resources(u32 dw_context, struct drv_object *hdrv_obj)
{
	dsp_status status = DSP_SOK;
	struct drv_object *pdrv_object = (struct drv_object *)hdrv_obj;
	struct drv_ext *pszdev_node;

	if (!(strcmp((char *)((struct drv_ext *)dw_context)->sz_string,
		     "TIOMAP1510")))
		status = request_bridge_resources(dw_context, DRV_RELEASE);
	else
		dev_dbg(bridge, "%s: Unknown device\n", __func__);

	if (DSP_FAILED(status))
		dev_dbg(bridge, "%s: Failed to relese bridge resources\n",
			__func__);

	/*
	 *  Irrespective of the status go ahead and clean it
	 *  The following will over write the status.
	 */
	for (pszdev_node = (struct drv_ext *)drv_get_first_dev_extension();
	     pszdev_node != NULL; pszdev_node = (struct drv_ext *)
	     drv_get_next_dev_extension((u32) pszdev_node)) {
		if (!pdrv_object->dev_node_string) {
			/* When this could happen? */
			continue;
		}
		if ((u32) pszdev_node == dw_context) {
			/* Found it */
			/* Delete from the Driver object list */
			lst_remove_elem(pdrv_object->dev_node_string,
					(struct list_head *)pszdev_node);
			kfree((void *)pszdev_node);
			break;
		}
		/* Delete the List if it is empty */
		if (LST_IS_EMPTY(pdrv_object->dev_node_string)) {
			kfree(pdrv_object->dev_node_string);
			pdrv_object->dev_node_string = NULL;
		}
	}
	return status;
}

/*
 *  ======== request_bridge_resources ========
 *  Purpose:
 *      Reserves shared memory for bridge.
 */
static dsp_status request_bridge_resources(u32 dw_context, s32 bRequest)
{
	dsp_status status = DSP_SOK;
	struct cfg_hostres *host_res;
	u32 dw_buff_size;

	struct drv_ext *driver_ext;
	u32 shm_size;

	DBC_REQUIRE(dw_context != 0);

	if (!bRequest) {
		driver_ext = (struct drv_ext *)dw_context;
		/* Releasing resources by deleting the registry key */
		dw_buff_size = sizeof(struct cfg_hostres);
		host_res = mem_calloc(dw_buff_size, MEM_NONPAGED);
		if (host_res != NULL) {
			if (DSP_FAILED(reg_get_value(CURRENTCONFIG,
						     (u8 *) host_res,
						     &dw_buff_size))) {
				status = CFG_E_RESOURCENOTAVAIL;
			}

			dw_buff_size = sizeof(shm_size);
			status = reg_get_value(SHMSIZE, (u8 *) &shm_size,
					       &dw_buff_size);
			if (DSP_SUCCEEDED(status)) {
				if ((host_res->dw_mem_base[1]) &&
				    (host_res->dw_mem_phys[1])) {
					mem_free_phys_mem((void *)
							  host_res->dw_mem_base
							  [1],
							  host_res->dw_mem_phys
							  [1], shm_size);
				}
			} else {
				dev_dbg(bridge, "%s: Error getting shm size "
					"from registry: %x. Not calling "
					"mem_free_phys_mem\n", __func__,
					status);
			}
			host_res->dw_mem_base[1] = 0;
			host_res->dw_mem_phys[1] = 0;

			if (host_res->dw_prm_base)
				iounmap(host_res->dw_prm_base);
			if (host_res->dw_cm_base)
				iounmap(host_res->dw_cm_base);
			if (host_res->dw_mem_base[0])
				iounmap((void *)host_res->dw_mem_base[0]);
			if (host_res->dw_mem_base[2])
				iounmap((void *)host_res->dw_mem_base[2]);
			if (host_res->dw_mem_base[3])
				iounmap((void *)host_res->dw_mem_base[3]);
			if (host_res->dw_mem_base[4])
				iounmap((void *)host_res->dw_mem_base[4]);
			if (host_res->dw_dmmu_base)
				iounmap(host_res->dw_dmmu_base);
			if (host_res->dw_per_base)
				iounmap(host_res->dw_per_base);
			if (host_res->dw_per_pm_base)
				iounmap((void *)host_res->dw_per_pm_base);
			if (host_res->dw_core_pm_base)
				iounmap((void *)host_res->dw_core_pm_base);
			if (host_res->dw_sys_ctrl_base)
				iounmap(host_res->dw_sys_ctrl_base);

			host_res->dw_prm_base = NULL;
			host_res->dw_cm_base = NULL;
			host_res->dw_mem_base[0] = (u32) NULL;
			host_res->dw_mem_base[2] = (u32) NULL;
			host_res->dw_mem_base[3] = (u32) NULL;
			host_res->dw_mem_base[4] = (u32) NULL;
			host_res->dw_dmmu_base = NULL;
			host_res->dw_sys_ctrl_base = NULL;

			dw_buff_size = sizeof(struct cfg_hostres);
			status = reg_set_value(CURRENTCONFIG, (u8 *) host_res,
					       (u32) dw_buff_size);
			/*  Set all the other entries to NULL */
			kfree(host_res);
		} else {
			status = DSP_EMEMORY;
		}
		return status;
	}
	dw_buff_size = sizeof(struct cfg_hostres);
	host_res = mem_calloc(dw_buff_size, MEM_NONPAGED);
	if (host_res != NULL) {
		/* num_mem_windows must not be more than CFG_MAXMEMREGISTERS */
		host_res->num_mem_windows = 2;
		/* First window is for DSP internal memory */

		host_res->dw_prm_base = ioremap(OMAP_IVA2_PRM_BASE,
						OMAP_IVA2_PRM_SIZE);
		host_res->dw_cm_base = ioremap(OMAP_IVA2_CM_BASE,
					       OMAP_IVA2_CM_SIZE);
		host_res->dw_sys_ctrl_base = ioremap(OMAP_SYSC_BASE,
						     OMAP_SYSC_SIZE);
		dev_dbg(bridge, "dw_mem_base[0] 0x%x\n",
			host_res->dw_mem_base[0]);
		dev_dbg(bridge, "dw_mem_base[3] 0x%x\n",
			host_res->dw_mem_base[3]);
		dev_dbg(bridge, "dw_prm_base %p\n", host_res->dw_prm_base);
		dev_dbg(bridge, "dw_cm_base %p\n", host_res->dw_cm_base);
		dev_dbg(bridge, "dw_dmmu_base %p\n", host_res->dw_dmmu_base);

		/* for 24xx base port is not mapping the mamory for DSP
		 * internal memory TODO Do a ioremap here */
		/* Second window is for DSP external memory shared with MPU */

		/* For Linux, these are hard-coded values */
		host_res->birq_registers = 0;
		host_res->birq_attrib = 0;
		host_res->dw_offset_for_monitor = 0;
		host_res->dw_chnl_offset = 0;
		/* CHNL_MAXCHANNELS */
		host_res->dw_num_chnls = CHNL_MAXCHANNELS;
		host_res->dw_chnl_buf_size = 0x400;
		dw_buff_size = sizeof(struct cfg_hostres);
		status = reg_set_value(CURRENTCONFIG, (u8 *) host_res,
				       sizeof(struct cfg_hostres));
		if (DSP_FAILED(status)) {
			dev_dbg(bridge, "%s: Failed to set the registry value "
				"for CURRENTCONFIG\n", __func__);
		}
		kfree(host_res);
	}
	/* End Mem alloc */
	return status;
}

/*
 *  ======== request_bridge_resources_dsp ========
 *  Purpose:
 *      Reserves shared memory for bridge.
 */
static dsp_status request_bridge_resources_dsp(u32 dw_context, s32 bRequest)
{
	dsp_status status = DSP_SOK;
	struct cfg_hostres *host_res;
	u32 dw_buff_size;
	u32 dma_addr;
	u32 shm_size;

	DBC_REQUIRE(dw_context != 0);

	dw_buff_size = sizeof(struct cfg_hostres);

	host_res = mem_calloc(dw_buff_size, MEM_NONPAGED);

	if (host_res != NULL) {
		if (DSP_FAILED(cfg_get_host_resources((struct cfg_devnode *)
						      dw_context, host_res))) {
			status = request_bridge_resources(dw_context, bRequest);
			if (DSP_SUCCEEDED(status)) {
				status = cfg_get_host_resources
				    ((struct cfg_devnode *)dw_context,
				     host_res);
			}
		}
		/* num_mem_windows must not be more than CFG_MAXMEMREGISTERS */
		host_res->num_mem_windows = 4;

		host_res->dw_mem_base[0] = 0;
		host_res->dw_mem_base[2] = (u32) ioremap(OMAP_DSP_MEM1_BASE,
							 OMAP_DSP_MEM1_SIZE);
		host_res->dw_mem_base[3] = (u32) ioremap(OMAP_DSP_MEM2_BASE,
							 OMAP_DSP_MEM2_SIZE);
		host_res->dw_mem_base[4] = (u32) ioremap(OMAP_DSP_MEM3_BASE,
							 OMAP_DSP_MEM3_SIZE);
		host_res->dw_per_base = ioremap(OMAP_PER_CM_BASE,
						OMAP_PER_CM_SIZE);
		host_res->dw_per_pm_base = (u32) ioremap(OMAP_PER_PRM_BASE,
							 OMAP_PER_PRM_SIZE);
		host_res->dw_core_pm_base = (u32) ioremap(OMAP_CORE_PRM_BASE,
							  OMAP_CORE_PRM_SIZE);
		host_res->dw_dmmu_base = ioremap(OMAP_DMMU_BASE,
						 OMAP_DMMU_SIZE);

		dev_dbg(bridge, "dw_mem_base[0] 0x%x\n",
			host_res->dw_mem_base[0]);
		dev_dbg(bridge, "dw_mem_base[1] 0x%x\n",
			host_res->dw_mem_base[1]);
		dev_dbg(bridge, "dw_mem_base[2] 0x%x\n",
			host_res->dw_mem_base[2]);
		dev_dbg(bridge, "dw_mem_base[3] 0x%x\n",
			host_res->dw_mem_base[3]);
		dev_dbg(bridge, "dw_mem_base[4] 0x%x\n",
			host_res->dw_mem_base[4]);
		dev_dbg(bridge, "dw_prm_base %p\n", host_res->dw_prm_base);
		dev_dbg(bridge, "dw_cm_base %p\n", host_res->dw_cm_base);
		dev_dbg(bridge, "dw_dmmu_base %p\n", host_res->dw_dmmu_base);
		dw_buff_size = sizeof(shm_size);
		status =
		    reg_get_value(SHMSIZE, (u8 *) &shm_size, &dw_buff_size);
		if (DSP_SUCCEEDED(status)) {
			/* Allocate Physically contiguous,
			 * non-cacheable  memory */
			host_res->dw_mem_base[1] =
			    (u32) mem_alloc_phys_mem(shm_size, 0x100000,
						     &dma_addr);
			if (host_res->dw_mem_base[1] == 0) {
				status = DSP_EMEMORY;
				pr_err("shm reservation Failed\n");
			} else {
				host_res->dw_mem_length[1] = shm_size;
				host_res->dw_mem_phys[1] = dma_addr;

				dev_dbg(bridge, "%s: Bridge shm address 0x%x "
					"dma_addr %x size %x\n", __func__,
					host_res->dw_mem_base[1],
					dma_addr, shm_size);
			}
		}
		if (DSP_SUCCEEDED(status)) {
			/* for Linux, these are hard-coded values */
			host_res->birq_registers = 0;
			host_res->birq_attrib = 0;
			host_res->dw_offset_for_monitor = 0;
			host_res->dw_chnl_offset = 0;
			/* CHNL_MAXCHANNELS */
			host_res->dw_num_chnls = CHNL_MAXCHANNELS;
			host_res->dw_chnl_buf_size = 0x400;
			dw_buff_size = sizeof(struct cfg_hostres);
			status = reg_set_value(CURRENTCONFIG, (u8 *) host_res,
					       sizeof(struct cfg_hostres));
			if (DSP_FAILED(status)) {
				dev_dbg(bridge, "%s: Failed to set the registry"
					" value for CURRENTCONFIG\n", __func__);
			}
		}
		kfree(host_res);
	}
	/* End Mem alloc */
	return status;
}
