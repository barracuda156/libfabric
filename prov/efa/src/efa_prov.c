/*
 * Copyright (c) 2019 Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <ofi_prov.h>
#include "efa.h"
#include "efa_prov_info.h"
#include "rdm/rxr_env.h"

#ifndef _WIN32

void efa_win_lib_finalize(void)
{
	// Nothing to do when we are not compiling for Windows
}

int efa_win_lib_initialize(void)
{
	return 0;
}

#else // _WIN32

#include "efawin.h"

/**
 * @brief open efawin.dll and load the symbols on windows platform
 *
 * This function is a no-op on windows
 */
int efa_win_lib_initialize(void)
{
	/* On Windows we need to load efawin dll to interact with
 	* efa device as there is no built-in verbs integration in the OS.
	* efawin dll provides all the ibv_* functions on Windows.
	* efa_load_efawin_lib function will replace stub ibv_* functions with
	* functions from efawin dll
	*/
	return efa_load_efawin_lib();
}

/**
 * @brief close efawin.dll on windows
 *
 * This function is a no-op on windows
 */
void efa_win_lib_finalize(void) {
	efa_free_efawin_lib();
}

#endif // _WIN32

static void efa_prov_finalize();

struct fi_provider efa_prov = {
	.name = "efa",
	.version = OFI_VERSION_DEF_PROV,
	.fi_version = OFI_VERSION_LATEST,
	.getinfo = efa_getinfo,
	.fabric = efa_fabric,
	.cleanup = efa_prov_finalize
};

struct util_prov efa_util_prov = {
	.prov = &efa_prov,
	.flags = 0,
};

/**
 * @brief initialize global variable: efa_util_prov
 *
 * efa_util_prov is the util_provider with its
 * info pointing a linked list of info object
 */
static int efa_util_prov_initialize()
{
	int i, err;
	struct fi_info *head, *tail, *prov_info_rdm, *prov_info_dgram;

	head = NULL;
	tail = NULL;
	for (i = 0; i < g_device_cnt; ++i) {
		err = efa_prov_info_alloc_for_rxr(&prov_info_rdm, &g_device_list[i]);
		if (err) {
			EFA_WARN(FI_LOG_DOMAIN, "Failed to allocate prov_info for rdm. error: %d\n",
				 err);
			continue;
		}

		if (!head) {
			head = prov_info_rdm;
		} else {
			assert(tail);
			tail->next = prov_info_rdm;
		}

		tail = prov_info_rdm;
	}

	for (i = 0; i < g_device_cnt; ++i) {
		prov_info_dgram = fi_dupinfo(g_device_list[i].dgram_info);
		if (!prov_info_dgram) {
			EFA_WARN(FI_LOG_DOMAIN, "Failed to allocate prov_info for dgram\n");
			continue;
		}

		if (!head) {
			head = prov_info_dgram;
		} else {
			assert(tail);
			tail->next = prov_info_dgram;
		}

		tail = prov_info_dgram;
	}

	if (!head)
		return -FI_ENODATA;

	efa_util_prov.info = head;
	return 0;
}

static void efa_util_prov_finalize()
{
	struct fi_info *prov_info;

	prov_info = (struct fi_info *)efa_util_prov.info;
	fi_freeinfo(prov_info);
	efa_util_prov.info = NULL;
}

EFA_INI
{
#if HAVE_EFA_DL
	ofi_mem_init();
	ofi_hmem_init();
	ofi_monitors_init();
#endif
	int err;

	err = efa_device_list_initialize();
	if (err)
		return NULL;

	if (g_device_cnt <= 0)
		return NULL;

	err = efa_win_lib_initialize();
	if (err)
		goto err_free;

	err = efa_util_prov_initialize();
	if (err)
		goto err_free;

	dlist_init(&g_efa_domain_list);

	rxr_env_initialize();
	return &efa_prov;

err_free:
	efa_prov_finalize();
	return NULL;
}

/**
 * @brief release the resources of global variables of provider
 *
 * This function calls various functions to release
 * util_prov, device_list, pd_list, win_lib
 */
static void efa_prov_finalize(void)
{
	efa_util_prov_finalize();

	efa_shm_info_finalize();

	efa_device_list_finalize();

	efa_win_lib_finalize();

#if HAVE_EFA_DL
	smr_cleanup();
	ofi_monitors_cleanup();
	ofi_hmem_cleanup();
	ofi_mem_fini();
#endif
}

