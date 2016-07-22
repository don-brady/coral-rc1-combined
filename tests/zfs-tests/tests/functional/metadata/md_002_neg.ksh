#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
# Copyright (c) 2016, Intel Corporation.
#

. $STF_SUITE/tests/functional/metadata/md.kshlib

#
# DESCRIPTION:
#	Creating a pool with a metadata device fails with invalid device 
#	type specified.
#

verify_runnable "global"

log_assert "Creating a pool with a metadata device type fails."
log_onexit cleanup

for type in "" "mirror" "raidz" "raidz2"
do
	for option in "" "-f"
	do
		for ac_type in "metadata" "smallblks"
		do
			for mdtype in "" "raidz" "raidz2"
			do
				log_mustnot zpool create $TESTPOOL $option $type $ZPOOL_DISKS \
				    $ac_type $mdtype $MD_DISKS
				log_mustnot display_status $TESTPOOL
				log_mustnot zpool destroy -f $TESTPOOL
			done
		done
	done
done

log_pass "Creating a pool with a metadata device type fails."
