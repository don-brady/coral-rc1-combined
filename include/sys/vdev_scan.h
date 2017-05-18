/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2016, Intel Corporation.
 */

#ifndef	_SYS_VDEV_SCAN_H
#define	_SYS_VDEV_SCAN_H

#include <sys/types.h>
#include <sys/spa.h>
#include <sys/dsl_pool.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct spa_vdev_scan {
	vdev_t		*svs_vd;
	kthread_t	*svs_thread;
	kmutex_t	svs_lock;
	kcondvar_t	svs_cv;
	boolean_t	svs_thread_exit;
	uint64_t	svs_dtl_max;
	int		svs_msi;
	int		svs_msi_synced;
	int		*svs_ms_done;
} spa_vdev_scan_t;

extern boolean_t spa_vdev_scan_enabled(const spa_t *);
extern void spa_vdev_scan_setup_sync(dmu_tx_t *);
extern void spa_vdev_scan_start(spa_t *, vdev_t *, int, uint64_t);
extern int spa_vdev_scan_restart(vdev_t *);
extern int spa_vdev_scan_rebuild_cb(dsl_pool_t *,
    const blkptr_t *, const zbookmark_phys_t *);
extern void spa_vdev_scan_suspend(spa_t *);
extern void spa_vdev_scan_destroy(spa_t *);

#define	DSL_SCAN_IS_REBUILD(scn)((scn)->scn_phys.scn_func == POOL_SCAN_REBUILD)
#define	SPA_VDEV_SCAN_MS(scn)	((scn)->scn_phys.scn_bookmark.zb_level)
#define	SPA_VDEV_SCAN_TVD(scn)	((scn)->scn_phys.scn_bookmark.zb_objset)
#define	SPA_VDEV_SCAN_OLDVD(scn)((scn)->scn_phys.scn_bookmark.zb_object)

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_VDEV_SCAN_H */
