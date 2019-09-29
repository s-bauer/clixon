/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2019 Olof Hagsand and Benny Holmgren

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

 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <pwd.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clixon/clixon.h>

#include "clixon_backend_transaction.h"
#include "backend_plugin.h"
#include "backend_handle.h"
#include "backend_commit.h"
#include "backend_startup.h"

/*! Merge db1 into db2 without commit 
 * @retval   -1       Error
 * @retval    0       Validation failed (with cbret set)
 * @retval    1       Validation OK       
 */
static int
db_merge(clicon_handle h,
 	 const char   *db1,
    	 const char   *db2,
	 cbuf         *cbret)
{
    int    retval = -1;
    cxobj *xt = NULL;
    
    /* Get data as xml from db1 */
    if (xmldb_get0(h, (char*)db1, NULL, NULL, 0, &xt, NULL) < 0)
	goto done;
    /* Merge xml into db2. Without commit */
    retval = xmldb_put(h, (char*)db2, OP_MERGE, xt, clicon_username_get(h), cbret);
 done:
    xmldb_get0_free(h, &xt);
    return retval;
}

/*! Clixon startup startup mode: Commit startup configuration into running state
 * @param[in]  h       Clixon handle
 * @param[in]  db      tmp or startup
 * @param[out] cbret   If status is invalid contains error message
 * @retval    -1       Error
 * @retval     0       Validation failed
 * @retval     1       OK

OK:
                              reset     
running                         |--------+------------> RUNNING
                parse validate OK       / commit 
startup -------+--+-------+------------+          


INVALID (requires manual edit of candidate)
failsafe      ----------------------+
                            reset    \ commit
running                       |-------+---------------> RUNNING FAILSAFE
              parse validate fail 
startup      ---+-------------------------------------> INVALID XML

ERR: (requires repair of startup) NYI
failsafe      ----------------------+
                            reset    \ commit
running                       |-------+---------------> RUNNING FAILSAFE
              parse fail
startup       --+-------------------------------------> BROKEN XML

 * @note: if commit fails, copy factory to running
 */
int
startup_mode_startup(clicon_handle        h,
		     char                *db,
		     cbuf                *cbret)
{
    int         retval = -1;
    int         ret;
    
    if (strcmp(db, "running")==0){
	clicon_err(OE_FATAL, 0, "Invalid startup db: %s", db);
	goto done;
    }
    /* If startup does not exist, create it empty */
    if (xmldb_exists(h, db) != 1){ /* diff */
	if (xmldb_create(h, db) < 0) /* diff */
	    return -1;
    }
    if ((ret = startup_commit(h, db, cbret)) < 0)
	goto done;
    if (ret == 0)
	goto fail;
    retval = 1;
 done:
    return retval;
 fail:
    retval = 0;
    goto done;
}

/*! Merge xml in filename into database
 * @retval   -1       Error
 * @retval    0       Validation failed (with cbret set)
 * @retval    1       Validation OK       
 */
static int
load_extraxml(clicon_handle h,
	      char         *filename,
	      const char   *db,
	      cbuf         *cbret)
{
    int        retval =  -1;
    cxobj     *xt = NULL;
    int        fd = -1;
    yang_stmt *yspec = NULL;

    if (filename == NULL)
	return 1;
    if ((fd = open(filename, O_RDONLY)) < 0){
	clicon_err(OE_UNIX, errno, "open(%s)", filename);
	goto done;
    }
    yspec = clicon_dbspec_yang(h);
    if (xml_parse_file(fd, "</config>", yspec, &xt) < 0)
	goto done;
    /* Replace parent w first child */
    if (xml_rootchild(xt, 0, &xt) < 0)
	goto done;
    /* Merge user reset state */
    retval = xmldb_put(h, (char*)db, OP_MERGE, xt, clicon_username_get(h), cbret);
 done:
    if (fd != -1)
	close(fd);
    if (xt)
	xml_free(xt);
    return retval;
}

/*! Load extra XML via file and/or reset callback, and merge with current
 * An application can add extra XML either via the -c <file> option or
 * via the .ca_reset callback. This XML is "merged" into running, that is,
 * it does not trigger validation calbacks.
 * The function uses an extra "tmp" database, loads the file to it, and calls
 * the reset function on it.
 * @param[in]  h    Clicon handle
 * @param[in]  file (Optional) extra xml file
 * @param[out] status  Startup status
 * @param[out] cbret   If status is invalid contains error message
 * @retval    -1       Error
 * @retval     0       Validation failed
 * @retval     1       OK
                
running -----------------+----+------>
           reset  loadfile   / merge
tmp     |-------+-----+-----+
             reset   extrafile
 */
int
startup_extraxml(clicon_handle        h,
		 char                *file,
		 cbuf                *cbret)
{
    int         retval = -1;
    char       *tmp_db = "tmp";
    int         ret;
    cxobj	*xt0 = NULL; 
    cxobj	*xt = NULL; 
    
    /* Clear tmp db */
    if (xmldb_db_reset(h, tmp_db) < 0)
	goto done;
    /* Application may define extra xml in its reset function*/
    if (clixon_plugin_reset(h, tmp_db) < 0)   
	goto done;
    /* Extra XML can also be added via file */
    if (file){
	/* Parse and load file into tmp db */
	if ((ret = load_extraxml(h, file, tmp_db, cbret)) < 0)
	    goto done;
	if (ret == 0)
	    goto fail;
    }
    /* 
     * Check if tmp db is empty.
     * It should be empty if extra-xml is null and reset plugins did nothing
     * then skip validation.
     */
    if (xmldb_get(h, tmp_db, NULL, &xt0) < 0)
	goto done;
    if (xt0==NULL || xml_child_nr(xt0)==0) 
	goto ok;
    xt = NULL;
    /* Validate the tmp db and return possibly upgraded xml in xt
     */
    if ((ret = startup_validate(h, tmp_db, &xt, cbret)) < 0)
	goto done;
    if (ret == 0)
	goto fail;
    if (xt==NULL || xml_child_nr(xt)==0) 
	goto ok;
    /* Merge tmp into running (no commit) */
    if ((ret = db_merge(h, tmp_db, "running", cbret)) < 0)
	goto fail;
    if (ret == 0)
	goto fail;
 ok:
    retval = 1;
 done:
    if (xt0)
	xml_free(xt0);
    xmldb_get0_free(h, &xt);
    if (xmldb_delete(h, tmp_db) != 0 && errno != ENOENT) 
	return -1;
    return retval;
 fail:
    retval = 0;
    goto done;
}

/*! Reset running and start in failsafe mode. If no failsafe then quit.
  Typically done when startup status is not OK so

failsafe      ----------------------+
                            reset    \ commit
running                       |-------+---------------> RUNNING FAILSAFE
 */
int
startup_failsafe(clicon_handle h)
{
    int   retval = -1;
    int   ret;
    char *db = "failsafe";
    cbuf *cbret = NULL;

    if ((cbret = cbuf_new()) == NULL){
	clicon_err(OE_XML, errno, "cbuf_new");
	goto done;
    }
    if ((ret = xmldb_exists(h, db)) < 0)
	goto done;
    if (ret == 0){ /* No it does not exist, fail */
	clicon_err(OE_DB, 0, "Startup failed and no Failsafe database found, exiting");
	goto done;
    }
    /* Copy original running to tmp as backup (restore if error) */
    if (xmldb_copy(h, "running", "tmp") < 0)
	goto done;
    if (xmldb_db_reset(h, "running") < 0)
	goto done;
    ret = candidate_commit(h, db, cbret);
    if (ret != 1)
	if (xmldb_copy(h, "tmp", "running") < 0)
	    goto done;
    if (ret < 0)
	goto done;
    if (ret == 0){
	clicon_err(OE_DB, 0, "Startup failed, Failsafe database validation failed %s", cbuf_get(cbret));
	goto done;
    }
    clicon_log(LOG_NOTICE, "Startup failed, Failsafe database loaded ");
    retval = 0;
 done:
    if (cbret)
	cbuf_free(cbret);
    return retval;
}

/*! Init modules state of the backend (server). To compare with startup XML
 * Set the modules state as setopt to the datastore module.
 * Only if CLICON_XMLDB_MODSTATE is enabled
 * @retval -1 Error
 * @retval  0 OK
 */
int
startup_module_state(clicon_handle h,
		     yang_stmt    *yspec)
{
    int    retval = -1;
    cxobj *x = NULL;
    int    ret;
	
    if (!clicon_option_bool(h, "CLICON_XMLDB_MODSTATE"))
	goto ok;
    /* Set up cache 
     * Now, access brief module cache with clicon_modst_cache_get(h, 1) */
    if ((ret = yang_modules_state_get(h, yspec, NULL, NULL, 1, &x)) < 0)
	goto done;
    if (ret == 0)
	goto fail;
 ok:
    retval = 1;
 done:
    if (x)
	xml_free(x);
    return retval;
 fail:
    retval = 0;
    goto done;
}
