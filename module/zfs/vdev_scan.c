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

#include <sys/vdev_impl.h>
#include <sys/vdev_draid_impl.h>
#include <sys/spa_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/dsl_scan.h>
#include <sys/vdev_scan.h>
#include <sys/zio.h>
#include <sys/dmu_tx.h>

static void
spa_vdev_scan_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	dsl_scan_t *scn = spa->spa_dsl_pool->dp_scan;

	ASSERT(zio->io_bp != NULL);

	abd_free(zio->io_abd);

	scn->scn_phys.scn_examined += DVA_GET_ASIZE(&zio->io_bp->blk_dva[0]);
	spa->spa_scan_pass_exam += DVA_GET_ASIZE(&zio->io_bp->blk_dva[0]);

	mutex_enter(&spa->spa_scrub_lock);

	spa->spa_scrub_inflight--;
	cv_broadcast(&spa->spa_scrub_io_cv);

	if (zio->io_error && (zio->io_error != ECKSUM ||
	    !(zio->io_flags & ZIO_FLAG_SPECULATIVE))) {
		spa->spa_dsl_pool->dp_scan->scn_phys.scn_errors++;
	}

	mutex_exit(&spa->spa_scrub_lock);
}

static int spa_vdev_scan_max_rebuild = 512;
static int spa_vdev_scan_delay = 64; /* number of ticks to delay rebuild */
static int spa_vdev_scan_idle = 512; /* idle window in clock ticks */

static void
spa_vdev_scan_rebuild_block(zio_t *pio, vdev_t *vd,
    uint64_t offset, uint64_t asize)
{
	blkptr_t blk, *bp = &blk;
	dva_t *dva = bp->blk_dva;
	int delay = spa_vdev_scan_delay;
	uint64_t psize;
	spa_t *spa = vd->vdev_spa;
	boolean_t draid_mirror = B_FALSE;
	ASSERTV(uint64_t ashift = vd->vdev_top->vdev_ashift);

	ASSERT(vd->vdev_ops == &vdev_draid_ops ||
	    vd->vdev_ops == &vdev_mirror_ops);

	/* Calculate psize from asize */
	if (vd->vdev_ops == &vdev_mirror_ops) {
		psize = asize;
	} else {
		int c, faulted;

		/*
		 * Initialize faulted to 1, to count the spare vdev we're
		 * rebuilding, which is not in faulted state.
		 */
		for (c = 0, faulted = 1; c < vd->vdev_children; c++) {
			vdev_t *child = vd->vdev_child[c];

			if (!vdev_readable(child) ||
			    (!vdev_writeable(child) && spa_writeable(spa)))
				faulted++;
		}

		if (faulted == vd->vdev_nparity)
			delay /= 2; /* critical, go faster */

		if (vdev_draid_ms_mirrored(vd, offset >> vd->vdev_ms_shift)) {
			ASSERT0((asize >> ashift) % (1 + vd->vdev_nparity));
			psize = asize / (1 + vd->vdev_nparity);
			draid_mirror = B_TRUE;
		} else {
			struct vdev_draid_configuration *cfg = vd->vdev_tsd;

			ASSERT0((asize >> ashift) %
			    (cfg->dcf_data + vd->vdev_nparity));
			psize = (asize / (cfg->dcf_data + vd->vdev_nparity)) *
			    cfg->dcf_data;
		}
	}
	ASSERT3U(asize, ==, vdev_psize_to_asize(vd, psize, offset));

	BP_ZERO(bp);

	DVA_SET_VDEV(&dva[0], vd->vdev_id);
	DVA_SET_OFFSET(&dva[0], offset);
	DVA_SET_GANG(&dva[0], 0);
	DVA_SET_ASIZE(&dva[0], asize);
	if (draid_mirror)
		DVA_SET_DRAID_MIRROR(&dva[0], 1);

	BP_SET_BIRTH(bp, TXG_INITIAL, TXG_INITIAL);
	BP_SET_LSIZE(bp, psize);
	BP_SET_PSIZE(bp, psize);
	BP_SET_COMPRESS(bp, ZIO_COMPRESS_OFF);
	BP_SET_CHECKSUM(bp, ZIO_CHECKSUM_OFF);
	BP_SET_TYPE(bp, DMU_OT_NONE);
	BP_SET_LEVEL(bp, 0);
	BP_SET_DEDUP(bp, 0);
	BP_SET_BYTEORDER(bp, ZFS_HOST_BYTEORDER);

	zfs_scan_delay(spa, vd,
	    spa_vdev_scan_max_rebuild, delay, spa_vdev_scan_idle);

	zio_nowait(zio_read(pio, spa, bp,
	    abd_alloc(psize, B_FALSE), psize, spa_vdev_scan_done, NULL,
	    ZIO_PRIORITY_SCRUB, ZIO_FLAG_SCAN_THREAD | ZIO_FLAG_RAW |
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_RESILVER, NULL));
}

static void
spa_vdev_scan_rebuild(const spa_vdev_scan_t *svs, zio_t *pio,
    vdev_t *vd, uint64_t offset, uint64_t length)
{
	uint64_t max_asize = vdev_psize_to_asize(vd, SPA_MAXBLOCKSIZE, offset);

	while (length > 0 && !svs->svs_thread_exit) {
		uint64_t chunksz = MIN(length, max_asize);

		spa_vdev_scan_rebuild_block(pio, vd, offset, chunksz);

		length -= chunksz;
		offset += chunksz;
	}
}

static void
spa_vdev_scan_draid_rebuild(const spa_vdev_scan_t *svs, zio_t *pio,
    vdev_t *vd, vdev_t *oldvd, uint64_t offset, uint64_t length)
{
	uint64_t msi = offset >> vd->vdev_ms_shift;
	boolean_t mirror;

	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_ops);
	ASSERT3U(msi, ==, (offset + length - 1) >> vd->vdev_ms_shift);

	mirror = vdev_draid_ms_mirrored(vd, msi);

	while (length > 0 && !svs->svs_thread_exit) {
		uint64_t group, group_left, chunksz;
		char *action = "Skipping";

		/*
		 * Make sure we don't cross redundancy group boundary
		 */
		group = vdev_draid_offset2group(vd, offset, mirror);
		group_left = vdev_draid_group2offset(vd,
		    group + 1, mirror) - offset;

		ASSERT(!vdev_draid_is_remainder_group(vd, group, mirror));
		ASSERT3U(group_left, <=, vdev_draid_get_groupsz(vd, mirror));

		chunksz = MIN(length, group_left);
		if (vdev_draid_group_degraded(vd, oldvd,
		    offset, chunksz, mirror)) {
			action = "Fixing";
			spa_vdev_scan_rebuild(svs, pio, vd, offset, chunksz);
		}

		draid_dbg(1, "\t%s: "U64FMT"K + "U64FMT"K (%s)\n",
		    action, offset >> 10, chunksz >> 10,
		    mirror ? "mirrored" : "dRAID");

		length -= chunksz;
		offset += chunksz;
	}
}

static void
spa_vdev_scan_thread(void *arg)
{
	vdev_t *vd = arg;
	spa_t *spa = vd->vdev_spa;
	spa_vdev_scan_t *svs = spa->spa_vdev_scan;
	zio_t *pio = zio_root(spa, NULL, NULL, 0);
	range_tree_t *allocd_segs;
	uint64_t msi;
	int err;

	ASSERT(svs != NULL);
	ASSERT3P(svs->svs_vdev, ==, vd);

	vd = vd->vdev_top;
	ASSERT3U(svs->svs_msi, <, vd->vdev_ms_count);
	/*
	 * Wait for newvd's DTL to propagate upward when
	 * spa_vdev_attach()->spa_vdev_exit() calls vdev_dtl_reassess().
	 */
	txg_wait_synced(spa->spa_dsl_pool, svs->svs_dtl_max);
	allocd_segs = range_tree_create(NULL, NULL, &svs->svs_lock);

	mutex_enter(&svs->svs_lock);

	for (msi = svs->svs_msi;
	    msi < vd->vdev_ms_count && !svs->svs_thread_exit; msi++) {
		metaslab_t *msp = vd->vdev_ms[msi];

		ASSERT0(range_tree_space(allocd_segs));

		mutex_enter(&msp->ms_sync_lock);
		mutex_enter(&msp->ms_lock);

		while (msp->ms_condensing) {
			mutex_exit(&msp->ms_lock);

			zfs_sleep_until(gethrtime() + 100 * MICROSEC);

			mutex_enter(&msp->ms_lock);
		}

		VERIFY(!msp->ms_condensing);
		VERIFY(!msp->ms_rebuilding);
		msp->ms_rebuilding = B_TRUE;

		/*
		 * If the metaslab has ever been allocated from (ms_sm!=NULL),
		 * read the allocated segments from the space map object
		 * into svr_allocd_segs. Since we do this while holding
		 * svr_lock and ms_sync_lock, concurrent frees (which
		 * would have modified the space map) will wait for us
		 * to finish loading the spacemap, and then take the
		 * appropriate action (see free_from_removing_vdev()).
		 */
		if (msp->ms_sm != NULL) {
			space_map_t *sm = NULL;

			/*
			 * We have to open a new space map here, because
			 * ms_sm's sm_length and sm_alloc may not reflect
			 * what's in the object contents, if we are in between
			 * metaslab_sync() and metaslab_sync_done().
			 *
			 * Note: space_map_open() drops and reacquires the
			 * caller-provided lock.  Therefore we can not provide
			 * any lock that we are using (e.g. ms_lock, svr_lock).
			 */
			VERIFY0(space_map_open(&sm,
			    spa->spa_dsl_pool->dp_meta_objset,
			    msp->ms_sm->sm_object, msp->ms_sm->sm_start,
			    msp->ms_sm->sm_size, msp->ms_sm->sm_shift,
			    &svs->svs_lock));
			space_map_update(sm);
			VERIFY0(space_map_load(sm, allocd_segs, SM_ALLOC));
			space_map_close(sm);
		}
		mutex_exit(&msp->ms_lock);
		mutex_exit(&msp->ms_sync_lock);

		zfs_dbgmsg("Scanning %llu segments for metaslab %llu",
		    avl_numnodes(&allocd_segs->rt_root), msp->ms_id);

		while (!svs->svs_thread_exit &&
		    range_tree_space(allocd_segs) != 0) {
			uint64_t offset, length;
			range_seg_t *rs = avl_first(&allocd_segs->rt_root);

			ASSERT(rs != NULL);
			offset = rs->rs_start;
			length = rs->rs_end - rs->rs_start;

			range_tree_remove(allocd_segs, offset, length);
			mutex_exit(&svs->svs_lock);

			draid_dbg(1, "MS ("U64FMT" at "U64FMT"K) segment: "
			    U64FMT"K + "U64FMT"K\n",
			    msp->ms_id, msp->ms_start >> 10,
			    (offset - msp->ms_start) >> 10, length >> 10);

			if (vd->vdev_ops == &vdev_mirror_ops)
				spa_vdev_scan_rebuild(svs, pio,
				    vd, offset, length);
			else
				spa_vdev_scan_draid_rebuild(svs, pio, vd,
				    svs->svs_vdev, offset, length);

			mutex_enter(&svs->svs_lock);
		}

		mutex_enter(&msp->ms_lock);

		/* HH: wait for rebuild IOs to complete for this metaslab? */
		msp->ms_rebuilding = B_FALSE;

		mutex_exit(&msp->ms_lock);
	}

	mutex_exit(&svs->svs_lock);

	err = zio_wait(pio);
	if (err != 0) /* HH: handle error */
		err = SET_ERROR(err);

	if (svs->svs_thread_exit) {
		mutex_enter(&svs->svs_lock);
		range_tree_vacate(allocd_segs, NULL, NULL);
		mutex_exit(&svs->svs_lock);
	}

	ASSERT0(range_tree_space(allocd_segs));
	range_tree_destroy(allocd_segs);

	mutex_enter(&svs->svs_lock);
	svs->svs_thread = NULL;
	cv_broadcast(&svs->svs_cv);
	mutex_exit(&svs->svs_lock);

	thread_exit();
}

void
spa_vdev_scan_start(spa_t *spa, vdev_t *oldvd, uint64_t msi, uint64_t txg)
{
	dsl_scan_t *scan = spa->spa_dsl_pool->dp_scan;
	spa_vdev_scan_t *svs = kmem_zalloc(sizeof (*svs), KM_SLEEP);

	ASSERT3U(msi, <, oldvd->vdev_top->vdev_ms_count);

	svs->svs_msi = msi;
	svs->svs_vdev = oldvd;
	svs->svs_dtl_max = txg;
	mutex_init(&svs->svs_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&svs->svs_cv, NULL, CV_DEFAULT, NULL);
	ASSERT3P(spa->spa_vdev_scan, ==, NULL);
	spa->spa_vdev_scan = svs;
	svs->svs_thread = thread_create(NULL, 0, spa_vdev_scan_thread, oldvd,
	    0, NULL, TS_RUN, defclsyspri);

	scan->scn_is_sequential = B_TRUE;
	scan->scn_restart_txg = txg;
}

void
spa_vdev_scan_restart(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	vdev_t *pvd = vd->vdev_parent;

	ASSERT(pvd != NULL);
	ASSERT3U(pvd->vdev_children, ==, 2);
	ASSERT3P(pvd->vdev_ops, ==, &vdev_spare_ops);
	ASSERT3P(vd->vdev_ops, ==, &vdev_draid_spare_ops);

	if (spa->spa_vdev_scan != NULL)
		return;

	spa_vdev_scan_start(spa, pvd->vdev_child[0], 0,
	    spa_last_synced_txg(spa) + 1 + TXG_CONCURRENT_STATES);
}

void
spa_vdev_scan_setup_sync(dmu_tx_t *tx)
{
	dsl_scan_t *scn = dmu_tx_pool(tx)->dp_scan;
	spa_t *spa = scn->scn_dp->dp_spa;

	ASSERT(scn->scn_is_sequential);
	ASSERT(scn->scn_phys.scn_state != DSS_SCANNING);
	ASSERT(spa->spa_vdev_scan != NULL);

	bzero(&scn->scn_phys, sizeof (scn->scn_phys));
	scn->scn_phys.scn_func = POOL_SCAN_REBUILD;
	scn->scn_phys.scn_state = DSS_SCANNING;
	scn->scn_phys.scn_min_txg = 0;
	scn->scn_phys.scn_max_txg = tx->tx_txg;
	scn->scn_phys.scn_ddt_class_max = 0;
	scn->scn_phys.scn_start_time = gethrestime_sec();
	scn->scn_phys.scn_errors = 0;
	/* Rebuild only examines blocks on one vdev */
	scn->scn_phys.scn_to_examine =
	    spa->spa_vdev_scan->svs_vdev->vdev_top->vdev_stat.vs_alloc;
	scn->scn_restart_txg = 0;
	scn->scn_done_txg = 0;

	scn->scn_sync_start_time = gethrtime();
	scn->scn_pausing = B_FALSE;
	spa->spa_scrub_active = B_TRUE;
	spa_scan_stat_init(spa);

	spa->spa_scrub_started = B_TRUE;
	spa_event_notify(spa, NULL, ESC_ZFS_REBUILD_START);
}

int
spa_vdev_scan_rebuild_cb(dsl_pool_t *dp,
    const blkptr_t *bp, const zbookmark_phys_t *zb)
{
	/* Rebuild happens in open context and does not use this callback */
	ASSERT0(1);
	return (-ENOTSUP);
}

boolean_t
spa_vdev_scan_enabled(const spa_t *spa)
{
	if (spa_vdev_scan_max_rebuild > 0)
		return (B_TRUE);
	else
		return (B_FALSE);
}

void
spa_vdev_scan_destroy(spa_t *spa)
{
	spa_vdev_scan_t *svs = spa->spa_vdev_scan;

	if (svs == NULL)
		return;

	spa->spa_vdev_scan = NULL;
	mutex_destroy(&svs->svs_lock);
	cv_destroy(&svs->svs_cv);
	kmem_free(svs, sizeof (*svs));
}

void
spa_vdev_scan_sync(spa_t *spa, dmu_tx_t *tx)
{
}

void
spa_vdev_scan_suspend(spa_t *spa)
{
	spa_vdev_scan_t *svs = spa->spa_vdev_scan;

	if (svs == NULL)
		return;

	mutex_enter(&svs->svs_lock);
	svs->svs_thread_exit = B_TRUE;
	while (svs->svs_thread != NULL)
		cv_wait(&svs->svs_cv, &svs->svs_lock);
	mutex_exit(&svs->svs_lock);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(spa_vdev_scan_max_rebuild, int, 0644);
MODULE_PARM_DESC(spa_vdev_scan_max_rebuild, "Max concurrent SPA rebuild I/Os");

module_param(spa_vdev_scan_delay, int, 0644);
MODULE_PARM_DESC(spa_vdev_scan_delay, "Number of ticks to delay SPA rebuild");

module_param(spa_vdev_scan_idle, int, 0644);
MODULE_PARM_DESC(spa_vdev_scan_idle, "Idle window in clock ticks for SPA rebuild");
#endif
