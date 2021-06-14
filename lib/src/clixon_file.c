/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2019 Olof Hagsand and Benny Holmgren
  Copyright (C) 2020-2021 Olof Hagsand and Rubicon Communications, LLC(Netgate)

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****

 *
 */
#ifdef HAVE_CONFIG_H
#include "clixon_config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <regex.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stddef.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clixon_err.h"
#include "clixon_queue.h"
#include "clixon_string.h"
#include "clixon_log.h"
#include "clixon_file.h"

struct clicon_file_list_item 
{
    char * name;
    struct clicon_file_list_item * next;
} clicon_file_list_item;


struct clicon_file_list
{
    struct clicon_file_list_item * first;
} clicon_file_list;


/*! frees the linked list
 */
void clicon_file_list_free(clicon_file_list * list) 
{
    while(list->first != NULL) 
    {
        list->first = current->next;

        free(current->name);
        free(current);
    }

    free(list);
}

/*! Initializes the clicon_file_list
 */
void clicon_file_list_init(clicon_file_list ** list) {
    *list = malloc(siezof(clicon_file_list));
    *list->first = NULL;
}

/*! Adds a new element to the clicon_file_list in alphabetical order
 */
void clicon_file_list_add(clicon_file_list * list, char * name)
{
    clicon_file_list_item * iterator;

    int nameLen = strlen(name);

    clicon_file_list_item * item;
    item = malloc(sizeof(clicon_file_list_item));
    item->next = NULL;
    item->name = malloc(nameLen + 1);
    memcpy(*list->name, name, nameLen + 1);

    if(list->first == NULL) {
        list->first = item;
    } else {
        if(strcoll(name, list->first->name) <= 0) {
            item->next = list->first;
            list->first = item;
        } else {
            iterator = list->first;
            while(iterator->next != NULL && strcoll(name, iterator->next->name) > 0) {
                iterator = iterator->next;
            }
            
            item->next = iterator->next;
            iterator->next = item;
        }
    }
}

/*! qsort "compar" for directory alphabetically sorting, see qsort(3)
 */
static int
clicon_file_dirent_sort(const void* arg1, 
			const void* arg2)
{
    struct dirent *d1 = (struct dirent *)arg1;
    struct dirent *d2 = (struct dirent *)arg2;

    return strcoll(d1->d_name, d2->d_name);
}

/*! Return alphabetically sorted files from a directory matching regexp
 * @param[in]  dir     Directory path 
 * @param[out] ent     Entries pointer, will be filled in with filenames. Free
 *                     after use
 * @param[in]  regexp  Regexp filename matching 
 * @param[in]  type    File type matching, see stat(2) 
 *
 * @retval  n  Number of matching files in directory
 * @retval -1  Error
 *
 * @code
 *   char          *dir = "/root/fs";
 *   struct dirent *dp;
 *   if ((ndp = clicon_file_dirent(dir, &dp, "(.so)$", S_IFREG)) < 0)
 *       return -1;
 *   for (i = 0; i < ndp; i++) 
 *       do something with dp[i];
 *   free(dp);
 * @endcode
*/
int
clicon_file_dirent(const char     *dir,
		   clicon_file_list **ent,
		   const char     *regexp,	
		   mode_t          type)
{
   int            retval = -1;
   DIR           *dirp;
   int            res;
   int            nent;
   regex_t        re;
   char           errbuf[128];
   char           filename[MAXPATHLEN];
   struct stat    st;
   struct dirent *dent;

   clicon_debug(1, "%s", __FUNCTION__);
   *ent = NULL;
   nent = 0;
   if (regexp && (res = regcomp(&re, regexp, REG_EXTENDED)) != 0) {
       regerror(res, &re, errbuf, sizeof(errbuf));
       clicon_err(OE_DB, 0, "regcomp: %s", errbuf);
       return -1;
   }
   if ((dirp = opendir(dir)) == NULL) {
     if (errno == ENOENT) /* Dir does not exist -> return 0 matches */
       retval = 0;
     else
       clicon_err(OE_UNIX, errno, "opendir(%s)", dir);
     goto quit;
   }
   while((dent = readdir(dirp)) != NULL) {
       /* Filename matching */
       if (regexp) {
	   if (regexec(&re, dent->d_name, (size_t) 0, NULL, 0) != 0)
	       continue;
       }
       /* File type matching */
       if (type) {
	   snprintf(filename, MAXPATHLEN-1, "%s/%s", dir, dent->d_name);
	   res = lstat(filename, &st);
	   if (res != 0) {
	       clicon_err(OE_UNIX, errno, "lstat");
	       goto quit;
	   }
	   if ((type & st.st_mode) == 0)
	       continue;
       }

       // Initialize the result list if required
       if(*ent == null) {
            clicon_file_list_init(ent);
       }

       clicon_file_list_add(*ent, dent->d_name);

       nent++;

   } /* while */

   retval = nent;
quit:

    if (dirp)
        closedir(dirp);
    if (regexp)
        regfree(&re);
        
    return retval;
}

/*! Make a copy of file src. Overwrite existing
 * @retval 0   OK
 * @retval -1  Error
 */
int
clicon_file_copy(char *src, 
		 char *target)
{
    int         retval = -1;
    int         inF = 0, ouF = 0;
    int         err = 0;
    char        line[512];
    int         bytes;
    struct stat st;

    if (stat(src, &st) != 0){
	clicon_err(OE_UNIX, errno, "stat");
	return -1;
    }
    if((inF = open(src, O_RDONLY)) == -1) {
	clicon_err(OE_UNIX, errno, "open(%s) for read", src);
	return -1;
    }
    if((ouF = open(target, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode)) == -1) {
	clicon_err(OE_UNIX, errno, "open(%s) for write", target);
	err = errno;
	goto error;
    }
    while((bytes = read(inF, line, sizeof(line))) > 0)
	if (write(ouF, line, bytes) < 0){
	    clicon_err(OE_UNIX, errno, "write(%s)", src);
	    err = errno;
	    goto error;
	}
    retval = 0;
  error:
    close(inF);
    if (ouF)
	close(ouF);
    if (retval < 0)
	errno = err;
    return retval;
}
