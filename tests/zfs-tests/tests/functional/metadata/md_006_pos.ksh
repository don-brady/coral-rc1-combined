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
#	Checking allocation_classes feature flag value after pool is created 
#	(should be enabled) and also after allocation_classes device added 
#	to existing pool (should be active).
#

verify_runnable "global"

log_assert "Values of allocation_classes feature flag correct."
log_onexit cleanup

typeset md_value

for type in "" "mirror" "raidz" "raidz2"
do
	for option in "" "-f"
	do
		for mdtype in "mirror"
		do
			log_must zpool create $TESTPOOL $option $type $ZPOOL_DISKS
			md_value="$(zpool get all -H -o property,value | \
			    egrep allocation_classes  | awk '{print $2}')"
			if [ "$md_value" = "enabled" ]; then
				log_note "feature@allocation_classes is enabled"
			else
				log_fail "feature@allocation_classes not enabled, \
				    status = $md_value"
			fi

			log_must zpool add $TESTPOOL special $mdtype $MD_DISKS
			md_value="$(zpool get all -H -o property,value | \
			    egrep allocation_classes | awk '{print $2}')"
			if [ "$md_value" = "active" ]; then
				log_note "feature@allocation_classes is active"
			else
				log_fail "feature@allocation_classes not active, \
				    status = $md_value"
			fi

			log_must zpool destroy -f $TESTPOOL
		done
	done
done

log_pass "Values of allocation_classes feature flag correct."
