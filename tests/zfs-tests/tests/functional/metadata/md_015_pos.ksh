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


#
# zpool list -CHp
# 1 = Name
# 2 = General Space
# 3 = General Alloc
# 4 = General Using
# 5 = Small Blocks Space
# 6 = Small Blocks Alloc
# 7 = Small Blocks Using
# 8 = Metadata Space
# 9 = Metadata Alloc
# 10 = Metadata Using
#
log_must $ZPOOL create $TESTPOOL -f -o segregate_metadata=on $ZPOOL_DISKS
log_must $ZFS set xattr=sa $TESTPOOL

#With a 32k xattr a file's metadata should take roughly 4 16k blocks.
let "filealloc=65536"
let "xattr=32768"
typeset -i files=0

set -A values $(zpool list -CHp $TESTPOOL | head -n1 | awk '{print $2, $3, $8, $9}')
while [[ ${values[3]} -lt ${values[2]} ]]
do
	#fill the space with 0 data MD files
	let "lfiles=((${values[2]} - ${values[3]}) / ${filealloc})"
	log_must $XATTRTEST -p /$TESTPOOL -k -f $lfiles -s ${xattr} -R
	files=$files+$lfiles
	set -A values $(zpool list -CHp $TESTPOOL | head -n1 | awk '{print $2, $3, $8, $9}')
done

let "filedelete=$files/2"

overage=$(cat /proc/spl/kstat/zfs/alloc_class_stats | grep metadata_highest_overage | awk '{print $3}')

if [[ $overage -lt 1 ]]
then
	log_fail "There is no overage"
fi

for filenum in $(seq $filedelete $files)
do
	rm -f /$TESTPOOL/file-$filenum
done

log_must printf "0" > /proc/spl/kstat/zfs/alloc_class_stats
log_must $XATTRTEST -p /$TESTPOOL -k -f 1 -s ${xattr} -R
overage=$(cat /proc/spl/kstat/zfs/alloc_class_stats | grep metadata_highest_overage | awk '{print $3}')

if [[ $overage -gt 0 ]]
then
	log_fail "There should be no overage"
fi

log_must zpool destroy -f $TESTPOOL

log_pass "Checking active allocation classes feature flag status successful."
