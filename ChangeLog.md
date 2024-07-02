# Change Log
All notable changes to pwalk will be documented in this file.

## 2023.09.14
  - ppurge tested on production BeeGFS file system to maintain delete30 tempary file
    system. 

## 2023.08.14
 - Initial work on ppurge for purging files based on mtime age

## 2021.07.14
 - Add Change Owner feature. File owner is changed conditionally. Two arguments
   are required; --chown_from and  -chown_to UID:GID. Check everyfile for
   "chown_from", if True change the owner to "chown_to". 
 - A parallel file traversal is a valuable tool for managing very large file
   POSIX file systems. The implementation of the change-owner feature is the
   first step to make pwalk a general-purpose file operation tool. For each
   file found in the filesystem, pwalk executes the fileProcess function. The
   call to fileProcess is a function pointer that can set to new types of file
   operation functions. Additional file operations are easy to implement via
   the function pointer to "fileProcess." 
 
## 2020.02.18
### Fixes
 - Parent Inode is not correct for Beegfs, change output format for inode

## 2017.05.31 
### Feature 
 - Add feature to exclude directories. command line option --exclude filename.
   filename contains a list of directory names to skip. pwalk can be used with
   relative and absolute paths.  Be sure to use the correct form of the path
   to exclude directories.

## 2016.12.12
### Fixes
 - pwalk escapes properly on filename, but not extension:
 - escaping commas and backslash within quoted fields (required for PostgreSQL)

## 2016.12.09 
### Added
 - setuid to root. pwalk needs to run as root to collect all file system information.
 - move changelog from source code to ChangeLog.md (what you are reading now)

## 2015.03.07 
 - Fix bug with directory file extensions having more than one dot '.' in the file name.

## 2013.08.02 
 - Add GNU license, --version command line arg added
 - becomes a github project

## 2013.10.07
### Added   
 - Output directory depth. Inital directory argument is given depth of zero regardless of its position
   in the file system. Only the inital dirctory (root) has the depth of zero.
 - Output parent inode for every file. Each file has its inode and parent inode. 
   This will allow a topological view of the file system.  The top level dir has a 
   parnet inode of 0. 

### Changes
 -  value of "fileCnt" defaults to -1 if file is not a directory. Zero is valid 
    file count for a directory.  This change will effect SQL queries written for earlier
    versions. Earlier versions were incorrent to asume fileCNT == 0 as regular file. 
 - Change "fileCnt" to numeric. Removing the extra quotes from numeric fileds in the 
      CSV file will save a small amount of file space for the output files.
      (a few megabytes for some of our larger directories)

### Improvements
 - data structures are cleaned up. changes made to improve thread
   performance. Expecthing this version to be 10% to 15% faster than previous.

## 2013.08.01 
 - improve command line argument errors

## 2012.10.09 
### Added 
 - --NoSnap command line arrgument added too ignore directories that are named .snapshot

## 2010.11.29
 - Add mutex for printStat

## 2010.03.24 
### Milstone
 - Recomended that pwalk be run on physical hardware. Slight network latencies 
   introduced by virtualization can add up to a significant amount of time 
   when 10's of billion's of operations are required.  
 - New physical hardware is available to run pwalk.
 - 16 threads are only using about 20% CPU with 10% IO wait. Based on this
   the default thread count will be doubled to 32.

## [pwalk v2] 2010.02.01 
### Fixed
 - pwalk v1 did not detach nor did it join the theads; v2 fixes this short comming;

## [pwalk v1] 2010.01.11
### Major
 - Complete re-write of walkv4 transforming it into pwalk.
   pwalk is a threaded version of walkv4.
   pwalk will call fileDir as a new thread until MAXTHRDS is reached.

## 2010.01.08 
 - Added: file name extension;
    Extension is defined as the last part of the name after a Dot "."
    if no dot is found extension is empty ""
 - Added: accept multible dirctory names as cmd line argument
 - Fix issue with directory detection:
This line of code has been replaced
     if ( f.st_mode & S_IFDIR && (f.st_mode & S_IFMT != S_IFLNK) ) 
With this new line of code:
     if ( S_ISDIR(f.st_mode)  ) 

## 2009.12.30 
 - size for dir should just be dir size; Fix; count returns 0
 - for normal files and count of just the local directory; Previously count
 - return the recursive file count for the whole tree.

## 2009.04.12 [3.1]
 -  replaced constants with "FILENAME_MAX",
 -  Size of directory is size of all files in directory plus itself
 -  Added printStat function
 -  Add file count field. If the file is a directory

## 2009.05.18 
 - check for control charaters and double qutoes in file names
 - escape double quotes and print bad file names to stderr
 - Bad file names contain C0 control codes. Remove C0 from files names for CSV output

## 2008.04.01
 -  Add: CSV output for database use

## 2004.07.06 
 - add flags -a and -k, to match features of du

## 2002.09.06 
 - change output to look exactly like du -a

## 2002.09.04 
 - walk.c; walk the directory and gather stats. This is a more flexible version of du to
   allow system admins to collect stats on different elements of the inode. Example: Create
   histograms of file counts/sizes by uid and or gid etc...

## 1997.03.20 
 - Although this is the first documented date for this file (walk.c) I have versions that date from 1988.
 - Starts as an exercise of how to use UNIX calls; opendir and readdir to simulate ls command.
 - Over the years this basic tool had many uses and file names: dir.c, walk.c, walkv2,3,4, pwalkfs.c
