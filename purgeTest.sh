#!/bin/bash

# purge files older than Period P.
#
# testing methodoligy
# Start with a tar file from the production file system. Do not test with live data. 
# Not all file systems are the same. Run this test on the same file system platform that
# will be used in production.
# Test data should have many levels of directories, a varity of file ages and owners.
# Create the tar file with --atime-preserve
# 
# 
# tar --atime-preserve -cf src.tar src
#
# There should be at least one file per directory that needs to be purged. (makes the math easier)
# Purged files are held in .ppurge directory for P days, even if they are much older than P days old.
# This script needs to be run daily for the duration of P.
#
#  Beining with the Epoch timestamps increase in value. 
#  t0 is Jan 1, 1970 Midnight
#
#  |----------------------------|--------|---------|----------------------------->
#  t0       past              Now-(p*2) Now-P      P                  future

# Unwind the tar file within the production file system. Create an environment
# variable TARGET, to be the full path to the test directory tree.

target=$TARGET
source_tar_file=/home/jfdey/src.tar
P=30

today=`date +"%F-%T"`
pMinus=`date -d "$(( $P - 1 )) days ago" +"%F-%H:%M"`
p=`date -d "${P} days ago" +"%F-%H:%M"`
pPlus=`date -d "$(( $P + 1 )) days ago" +"%F-%H:%M"`

pList='pMinus
p
pPlus'

echo Testing ppurge - Purging files older than: ${P}
echo Using these time stamps for testing
printf "%12s : %s\n" "Today" $today
for ts in $pList; do
    printf "%12s : %s\n" ${ts}: ${!ts}
done

echo 
if [[ -d ${target} ]]; then
    echo === Clean-up Older test env
    rm -rf ${target}
fi
echo === Create file tree for testing 
(cd `dirname ${target}` && tar -xf $source_tar_file )

echo 
file_count=`find ${target} -type f | wc -l`
echo === Count all regular files in tree: $file_count
pMinus_file_count=`find ${target} -type f -mtime +${P} | wc -l`
echo === Count all regular files in tree over $P days old: $pMinus_file_count
newer_thanP=`find ${target} -type f -mtime -$P | wc -l`
echo === Count regular files in the tree under $P days old: $newer_thanP 
dir_count=`find ${target} -type d  | wc -l`
echo === Count all Directories in the tree: $dir_count

echo
echo === Initial ppruge run
./ppurge --purgeDays 30 ${target} >output_a.csv 2>debug.a

moved_to_purge=0
for pdir in `find ${target} -name .ppurge`; do
    count=`find ${pdir} -type f | wc -l`
    moved_to_purge=$(( $moved_to_purge + $count ))
done
echo
echo === Number of files moved to purge dir: $moved_to_purge 
echo === Should equal the cound of files over $P days old: $pMinus_file_count
purge_records=`grep ^P output_a.csv | wc -l`
echo === This should also match the number of Purge records from the .ppurge log file: $purge_records

purge_dir_count=`find $target -name .ppurge | wc -l`
echo
echo === Number of ppurge directoris created: $purge_dir_count
echo === Should match the number of directories: $dir_count

over_2x=0
Px2=$(( $P * 2 ))
for pdir in `find ${target} -name .ppurge`; do
    count=`find ${pdir} -type f -a -mtime +${Px2} | wc -l`
    over_2x=$(( $over_2x + $count ))
done
echo
echo === Number of files that are over P*2 days old: $over_2x

echo
echo === Move the Atime of .ppurge directories P+1 into the past. This will cause all files over P*2 to be removed
pPlus1=`date -d "$(( $P + 1 )) days ago" +"%Y%m%d%H%M"`
sudo find $target -name .ppurge -a -exec touch -a -t $pPlus1 {} \;

read -p "Take a pause to inspect the file sysetm. Check for .ppurge directories. Press (y/n) to continue." choice
case "$choice" in
  n|N ) echo "Stopping."; 
        exit;;
  * ) echo "continue tesing";;
esac 

echo
echo === ReRun ppurge
./ppurge --purgeDays $P ${target} >output_b.csv 2>debug.b

ppurge_fcount=0
for pdir in `find ${target} -name .ppurge`; do
    count=`find ${pdir} -type f | wc -l`
    ppurge_fcount=$(( $ppurge_fcount + $count ))
done

echo
echo === Number of files in .ppurge directories: $ppurge_fcount
echo === Number of files in .ppurge directories could be zero if test data does not have with dates between NOW and $P days old.
rm_records=`grep ^R output_b.csv | wc -l`
echo
echo === The number of purged files should match the number of removed files after the second ppurge run.
if [ $purge_records == $rm_records ]; then
    msg=" And they match!";
else
    msg=" hmmm, they do not match"
fi
echo === Purged: $purge_records  Removed: $rm_records $msg
 
newer_thanP_b=`find ${target} -type f -mtime -$P | wc -l`
echo === The number of files that are newer that $P should be the same. "$newer_thanP == $newer_thanP_b"
