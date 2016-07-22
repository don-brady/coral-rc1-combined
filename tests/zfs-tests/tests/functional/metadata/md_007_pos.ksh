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
#	Attaching and detaching a metadata device to an existing metadata mirror;
#	creates 3-way metadata mirror. Applies method to each disk in the mirror.
#

verify_runnable "global"

log_assert "Attaching/detaching metadata device to metadata mirror successful."
log_onexit cleanup

typeset md_value

for type in "" "mirror" "raidz" "raidz2"
do
	for option in "" "-f"
	do
		for ac_type in "metadata" "smallblks"
		do
			for mdtype in "mirror"
			do
				log_must zpool create $TESTPOOL $option $type $ZPOOL_DISKS \
				    $ac_type $mdtype $MD_DISKS
				log_must zpool attach $TESTPOOL $MD_DISK1 $MD_EXTRA1
				log_must zpool detach $TESTPOOL $MD_EXTRA1
				log_must zpool attach $TESTPOOL $MD_DISK1 $MD_EXTRA1
				log_must zpool detach $TESTPOOL $MD_DISK1
				log_must zpool attach $TESTPOOL $MD_DISK0 $MD_DISK1
				log_must zpool detach $TESTPOOL $MD_DISK0
				log_must zpool destroy -f $TESTPOOL
			done
		done
	done
done

log_pass "Attaching and detaching metadata device to metadata mirror successful."
