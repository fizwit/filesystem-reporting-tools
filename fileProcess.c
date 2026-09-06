/*
 *  fileProcess.c

Copyright (C) (2013-2016) John F Dey

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 */

/*

For each file found by Pwalk, process the file.
File processing functions go in this file.  File process routines must
keep the same arguments as defined by the prototype fileProcess()

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include "pwalk.h"

/* rewrite control characters */
static const unsigned char escape_code[32] = {
    [7]  = 'a',  // bell
    [8]  = 'b',  // backspace
    [9]  = 't',  // tab
    [10] = 'n',  // line feed
    [11] = 'v',  // vertical tab
    [12] = 'f',  // form feed
    [13] = 'r'   // carriage return
};


/* Escape CSV delimeters, replace control characters */
void
csv_escape(char *in, char *out)
{
   char *orig;

   orig = in;
   while ( *in ) {
      if ( *in == '"' ) {
          *out++ = '"';
          *out++ = *in++;
      } else if ( (unsigned char)*in < 32 ) {
          if ( escape_code[(int)*in] ) {
              *out++ = '\\';
              *out++ = escape_code[(int)*in];
          }
          in++;
      } else
          *out++ = *in++;
   }
   *out = '\0';
}
