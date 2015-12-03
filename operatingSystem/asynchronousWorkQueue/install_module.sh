#!/bin/sh
set -x
# WARNING: this script doesn't check for errors, so you have to enhance it in case any of the commands
# below fail.
/sbin/lsmod
/sbin/rmmod sys_submitjob
/sbin/insmod sys_submitjob.ko
/sbin/lsmod
