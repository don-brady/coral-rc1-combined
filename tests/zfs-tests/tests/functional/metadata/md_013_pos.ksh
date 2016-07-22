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
#	Checking if allocation_classes feature flag status is active after creating a pool
#	with allocation_classes device.
#

verify_runnable "global"

log_assert "Checking active allocation classes feature flag status successful."
log_onexit cleanup

typeset md_value

for type in "" "mirror" "raidz" "raidz2"
do
	for option in "" "-f"
	do
		for ac_type in "metadata" "smallblks" "log"
		do
			log_must zpool create $TESTPOOL $option -o segregate_${ac_type}=on \
			    $type $ZPOOL_DISKS
			md_value="$(zpool get all -H -o property,value | \
			    egrep allocation_classes | awk '{print $2}')"
			if [ "$md_value" = "active" ]; then
				log_note "feature@allocation_classes is active"
			else
				log_fail "feature@allocation_classes not active, status = $md_value"
			fi
			md_value="$(zpool list -H -o segregate_${ac_type} $TESTPOOL)"
			if [ "$md_value" = "on" ]; then
				log_note "segregate_${ac_type} is on"
			else
				log_fail "segregate_${ac_type} is not on, status = $md_value"
			fi
			log_must zpool destroy -f $TESTPOOL
		done
	done
done

log_pass "Checking active allocation classes feature flag status successful."
