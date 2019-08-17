/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2019 Olof Hagsand

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

/*
 * This program should be run as user www-data 
 *
 * See draft-ietf-netconf-restconf-13.txt [draft]

 * sudo apt-get install libfcgi-dev
 * gcc -o fastcgi fastcgi.c -lfcgi

 * sudo su -c "/www-data/clixon_restconf -D 1 -f /usr/local/etc/example.xml " -s /bin/sh www-data

 * This is the interface:
 * api/data/profile=<name>/metric=<name> PUT data:enable=<flag>
 * api/test
 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <libgen.h>
#include <sys/stat.h> /* chmod */

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clixon/clixon.h>

#include <fcgiapp.h> /* Need to be after clixon_xml.h due to attribute format */

/* restconf */
#include "restconf_lib.h"
#include "restconf_methods.h"
#include "restconf_methods_get.h"
#include "restconf_methods_post.h"
#include "restconf_stream.h"

/* Command line options to be passed to getopt(3) */
#define RESTCONF_OPTS "hD:f:l:p:d:y:a:u:o:"

/* RESTCONF enables deployments to specify where the RESTCONF API is 
   located.  The client discovers this by getting the "/.well-known/host-meta"
   resource 
*/
#define RESTCONF_WELL_KNOWN  "/.well-known/host-meta"

/*! Generic REST method, GET, PUT, DELETE, etc
 * @param[in]  h      CLIXON handle
 * @param[in]  r      Fastcgi request handle
 * @param[in]  api_path According to restconf (Sec 3.5.1.1 in [draft])
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where to start pcvec
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  dvec   Stream input daat
 * @param[in]  pretty Set to 1 for pretty-printed xml/json output
 * @param[in]  media_in  Input media
 * @param[in]  media_out Output media
 */
static int
api_data(clicon_handle h,
	 FCGX_Request *r, 
	 char         *api_path, 
	 cvec         *pcvec, 
	 int           pi,
	 cvec         *qvec, 
	 char         *data,
	 int           pretty,
	 restconf_media media_out)
{
    int     retval = -1;
    char   *request_method;

    clicon_debug(1, "%s", __FUNCTION__);
    request_method = FCGX_GetParam("REQUEST_METHOD", r->envp);
    clicon_debug(1, "%s method:%s", __FUNCTION__, request_method);
    if (strcmp(request_method, "OPTIONS")==0)
	retval = api_data_options(h, r);
    else if (strcmp(request_method, "HEAD")==0)
	retval = api_data_head(h, r, pcvec, pi, qvec, pretty, media_out);
    else if (strcmp(request_method, "GET")==0)
	retval = api_data_get(h, r, pcvec, pi, qvec, pretty, media_out);
    else if (strcmp(request_method, "POST")==0)
	retval = api_data_post(h, r, api_path, pcvec, pi, qvec, data, pretty, media_out);
    else if (strcmp(request_method, "PUT")==0)
	retval = api_data_put(h, r, api_path, pcvec, pi, qvec, data, pretty, media_out);
    else if (strcmp(request_method, "PATCH")==0)
	retval = api_data_patch(h, r, api_path, pcvec, pi, qvec, data, pretty, media_out);
    else if (strcmp(request_method, "DELETE")==0)
	retval = api_data_delete(h, r, api_path, pi, pretty, media_out);
    else
	retval = restconf_notfound(r);
    clicon_debug(1, "%s retval:%d", __FUNCTION__, retval);
    return retval;
}

/*! Operations REST method, POST
 * @param[in]  h      CLIXON handle
 * @param[in]  r      Fastcgi request handle
 * @param[in]  path   According to restconf (Sec 3.5.1.1 in [draft])
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where to start pcvec
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  data   Stream input data
 * @param[in]  media_out Output media
 */
static int
api_operations(clicon_handle h,
	       FCGX_Request *r, 
	       char         *path,
	       cvec         *pcvec, 
	       int           pi,
	       cvec         *qvec, 
	       char         *data,
	       int           pretty,
	       restconf_media media_out)
{
    int     retval = -1;
    char   *request_method;

    clicon_debug(1, "%s", __FUNCTION__);
    request_method = FCGX_GetParam("REQUEST_METHOD", r->envp);
    clicon_debug(1, "%s method:%s", __FUNCTION__, request_method);
    if (strcmp(request_method, "GET")==0)
	retval = api_operations_get(h, r, path, pcvec, pi, qvec, data, pretty, media_out);
    else if (strcmp(request_method, "POST")==0)
	retval = api_operations_post(h, r, path, pcvec, pi, qvec, data,
				     pretty, media_out);
    else
	retval = restconf_notfound(r);
    return retval;
}

/*! Determine the root of the RESTCONF API
 * @param[in]  h        Clicon handle
 * @param[in]  r        Fastcgi request handle
 * @note Hardcoded to "/restconf"
 * Return see RFC8040 3.1 and RFC7320
 * In line with the best practices defined by [RFC7320], RESTCONF
 * enables deployments to specify where the RESTCONF API is located.
 */
static int
api_well_known(clicon_handle h,
	       FCGX_Request *r)
{
    clicon_debug(1, "%s", __FUNCTION__);
    FCGX_FPrintF(r->out, "Cache-Control: no-cache\r\n");
    FCGX_FPrintF(r->out, "Content-Type: application/xrd+xml\r\n");
    FCGX_FPrintF(r->out, "\r\n");
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "<XRD xmlns='http://docs.oasis-open.org/ns/xri/xrd-1.0'>\n");
    FCGX_FPrintF(r->out, "   <Link rel='restconf' href='/restconf'/>\n");
    FCGX_FPrintF(r->out, "</XRD>\r\n");

    return 0;
}

/*! Retrieve the Top-Level API Resource
 * @param[in]  h        Clicon handle
 * @param[in]  r        Fastcgi request handle
 * @note Only returns null for operations and data,...
 * See RFC8040 3.3
 */
static int
api_root(clicon_handle  h,
	 FCGX_Request  *r,
	 int            pretty,
	 restconf_media media_out)

{
    int        retval = -1;
    cxobj     *xt = NULL;
    cbuf      *cb = NULL;
    yang_stmt *yspec;
    
    clicon_debug(1, "%s", __FUNCTION__);
    if ((yspec = clicon_dbspec_yang(h)) == NULL){
	clicon_err(OE_FATAL, 0, "No DB_SPEC");
	goto done;
    }
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Cache-Control: no-cache\r\n");

    FCGX_FPrintF(r->out, "Content-Type: %s\r\n", restconf_media_int2str(media_out));
    FCGX_FPrintF(r->out, "\r\n");

    if (xml_parse_string("<restconf xmlns=\"urn:ietf:params:xml:ns:yang:ietf-restconf\"><data/><operations/><yang-library-version>2016-06-21</yang-library-version></restconf>", NULL, &xt) < 0)
	goto done;
    if (xml_apply(xt, CX_ELMNT, xml_spec_populate, yspec) < 0)
	goto done;
    if ((cb = cbuf_new()) == NULL){
	clicon_err(OE_XML, errno, "cbuf_new");
	goto done;
    }
    if (xml_rootchild(xt, 0, &xt) < 0)
	goto done;
    switch (media_out){
    case YANG_DATA_XML:
	if (clicon_xml2cbuf(cb, xt, 0, pretty, -1) < 0)
	    goto done;
	break;
    case YANG_DATA_JSON:
	if (xml2json_cbuf(cb, xt, pretty) < 0)
	    goto done;
	break;
    default:
	break;
    }
    FCGX_FPrintF(r->out, "%s", cb?cbuf_get(cb):"");
    FCGX_FPrintF(r->out, "\r\n\r\n");
    retval = 0;
 done:
    if (cb)
        cbuf_free(cb);
    if (xt)
	xml_free(xt);
    return retval;
}

/*!
 * See https://tools.ietf.org/html/rfc7895
 */
static int
api_yang_library_version(clicon_handle h,
			 FCGX_Request *r,
			 int           pretty,
			 restconf_media media_out)
    
{
    int    retval = -1;
    cxobj *xt = NULL;
    cbuf  *cb = NULL;
    char  *ietf_yang_library_revision = "2016-06-21"; /* XXX */

    clicon_debug(1, "%s", __FUNCTION__);
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Cache-Control: no-cache\r\n");
    FCGX_FPrintF(r->out, "Content-Type: %s\r\n", restconf_media_int2str(media_out));
    FCGX_FPrintF(r->out, "\r\n");
    if (xml_parse_va(&xt, NULL, "<yang-library-version>%s</yang-library-version>", ietf_yang_library_revision) < 0)
	goto done;
    if (xml_rootchild(xt, 0, &xt) < 0)
	goto done;
    if ((cb = cbuf_new()) == NULL){
	goto done;
    }
    switch (media_out){
    case YANG_DATA_XML:
	if (clicon_xml2cbuf(cb, xt, 0, pretty, -1) < 0)
	    goto done;
	break;
    case YANG_DATA_JSON:
	if (xml2json_cbuf(cb, xt, pretty) < 0)
	    goto done;
	break;
    default:
	break;
    }
    clicon_debug(1, "%s cb%s", __FUNCTION__, cbuf_get(cb));
    FCGX_FPrintF(r->out, "%s\n", cb?cbuf_get(cb):"");
    FCGX_FPrintF(r->out, "\n\n");
    retval = 0;
 done:
    if (cb)
        cbuf_free(cb);
    if (xt)
	xml_free(xt);
    return retval;
}

/*! Process a FastCGI request
 * @param[in]  r        Fastcgi request handle
 */
static int
api_restconf(clicon_handle h,
	     FCGX_Request *r)
{
    int    retval = -1;
    char  *path;
    char  *query;
    char  *method;
    char **pvec = NULL;
    int    pn;
    cvec  *qvec = NULL;
    cvec  *dvec = NULL;
    cvec  *pcvec = NULL; /* for rest api */
    cbuf  *cb = NULL;
    char  *data;
    int    authenticated = 0;
    char  *media_str = NULL;
    restconf_media media_out = YANG_DATA_JSON;
    int    pretty;
    cbuf  *cbret = NULL;
    cxobj *xret = NULL;
    cxobj *xerr;

    clicon_debug(1, "%s", __FUNCTION__);
    path = restconf_uripath(r);
    query = FCGX_GetParam("QUERY_STRING", r->envp);
    pretty = clicon_option_bool(h, "CLICON_RESTCONF_PRETTY");

    /* Get media for output (proactive negotiation) RFC7231 by using
     * Accept:. This is for methods that have output, such as GET, 
     * operation POST, etc
     * If accept is * default is yang-json
     */
    if ((media_str = FCGX_GetParam("HTTP_ACCEPT", r->envp)) == NULL){
	// retval = restconf_unsupported_media(r);
	// goto done;
    }
    else    if ((media_out = restconf_media_str2int(media_str)) == -1){
	if (strcmp(media_str, "*/*") == 0) /* catch-all */
	    media_out = YANG_DATA_JSON;
	else{
	    retval = restconf_unsupported_media(r);
	    goto done;
	}
    }
    clicon_debug(1, "%s ACCEPT: %s %s", __FUNCTION__, media_str, restconf_media_int2str(media_out));

    if ((pvec = clicon_strsep(path, "/", &pn)) == NULL)
	goto done;
    /* Sanity check of path. Should be /restconf/ */
    if (pn < 2){
	restconf_notfound(r);
	goto ok;
    }
    if (strlen(pvec[0]) != 0){
	retval = restconf_notfound(r);
	goto done;
    }
    if (strcmp(pvec[1], RESTCONF_API)){
	retval = restconf_notfound(r);
	goto done;
    }
    restconf_test(r, 1);

    if (pn == 2){
	retval = api_root(h, r, pretty, media_out);
	goto done;
    }
    if ((method = pvec[2]) == NULL){
	retval = restconf_notfound(r);
	goto done;
    }
    clicon_debug(1, "%s: method=%s", __FUNCTION__, method);
    if (str2cvec(query, '&', '=', &qvec) < 0)
      goto done;
    if (str2cvec(path, '/', '=', &pcvec) < 0) /* rest url eg /album=ricky/foo */
      goto done;
    /* data */
    if ((cb = readdata(r)) == NULL)
	goto done;
    data = cbuf_get(cb);
    clicon_debug(1, "%s DATA=%s", __FUNCTION__, data);

    if (str2cvec(data, '&', '=', &dvec) < 0)
      goto done;
    /* If present, check credentials. See "plugin_credentials" in plugin  
     * See RFC 8040 section 2.5
     */
    if ((authenticated = clixon_plugin_auth(h, r)) < 0)
	goto done;
    clicon_debug(1, "%s auth:%d %s", __FUNCTION__, authenticated, clicon_username_get(h));

    /* If set but no user, we set a dummy user */
    if (authenticated){
	if (clicon_username_get(h) == NULL)
	    clicon_username_set(h, "none");
    }
    else{
	if (netconf_access_denied_xml(&xret, "protocol", "The requested URL was unauthorized") < 0)
	    goto done;
	if ((xerr = xpath_first(xret, "//rpc-error")) != NULL){
	    if (api_return_err(h, r, xerr, pretty, media_out, 0) < 0)
		goto done;
	    goto ok;
	}
	goto ok;
    }
    clicon_debug(1, "%s auth2:%d %s", __FUNCTION__, authenticated, clicon_username_get(h));
    if (strcmp(method, "yang-library-version")==0){
	if (api_yang_library_version(h, r, pretty, media_out) < 0)
	    goto done;
    }
    else if (strcmp(method, "data") == 0){ /* restconf, skip /api/data */
	if (api_data(h, r, path, pcvec, 2, qvec, data,
		     pretty, media_out) < 0)
	    goto done;
    }
    else if (strcmp(method, "operations") == 0){ /* rpc */
	if (api_operations(h, r, path, pcvec, 2, qvec, data,
			   pretty, media_out) < 0)
	    goto done;
    }
    else if (strcmp(method, "test") == 0)
	restconf_test(r, 0);
    else
	restconf_notfound(r);
 ok:
    retval = 0;
 done:
    clicon_debug(1, "%s retval:%d", __FUNCTION__, retval);
    if (pvec)
	free(pvec);
    if (dvec)
	cvec_free(dvec);
    if (qvec)
	cvec_free(qvec);
    if (pcvec)
	cvec_free(pcvec);
    if (cb)
	cbuf_free(cb);
    if (cbret)
	cbuf_free(cbret);
    if (xret)
	xml_free(xret);
    return retval;
}


/* Need global variable to for signal handler XXX */
static clicon_handle _CLICON_HANDLE = NULL;

/*! Signall terminates process
 */
static void
restconf_sig_term(int arg)
{
    static int i=0;

    if (i++ == 0)
	clicon_log(LOG_NOTICE, "%s: %s: pid: %u Signal %d", 
		   __PROGRAM__, __FUNCTION__, getpid(), arg);
    else
	exit(-1);
    if (_CLICON_HANDLE){
	stream_child_freeall(_CLICON_HANDLE);
	restconf_terminate(_CLICON_HANDLE);
    }
    clicon_exit_set(); /* checked in event_loop() */
    exit(-1);
}

/*! Callback for yang extensions ietf-restconf:yang-data
 * @see ietf-restconf.yang
 * @param[in] h    Clixon handle
 * @param[in] yext Yang node of extension 
 * @param[in] ys   Yang node of (unknown) statement belonging to extension
 * @retval     0   OK, all callbacks executed OK
 * @retval    -1   Error in one callback
 */
static int
restconf_main_extension_cb(clicon_handle h,
			   yang_stmt    *yext,
			   yang_stmt    *ys)
{
    int        retval = -1;
    char      *extname;
    char      *modname;
    yang_stmt *ymod;
    yang_stmt *yc;
    yang_stmt *yn = NULL;
    
    ymod = ys_module(yext);
    modname = yang_argument_get(ymod);
    extname = yang_argument_get(yext);
    if (strcmp(modname, "ietf-restconf") != 0 || strcmp(extname, "yang-data") != 0)
	goto ok;
    clicon_debug(1, "%s Enabled extension:%s:%s", __FUNCTION__, modname, extname);
    if ((yc = yang_find(ys, 0, NULL)) == NULL)
	goto ok;
    if ((yn = ys_dup(yc)) == NULL)
	goto done;
    if (yn_insert(yang_parent_get(ys), yn) < 0)
	goto done;
 ok:
    retval = 0;
 done:
    return retval;
}

static void
restconf_sig_child(int arg)
{
    int status;
    int pid;

    if ((pid = waitpid(-1, &status, 0)) != -1 && WIFEXITED(status))
	stream_child_free(_CLICON_HANDLE, pid);
}

/*! Usage help routine
 * @param[in]  argv0  command line
 * @param[in]  h      Clicon handle
 */
static void
usage(clicon_handle h,
      char         *argv0)

{
    fprintf(stderr, "usage:%s [options]\n"
	    "where options are\n"
            "\t-h \t\tHelp\n"
	    "\t-D <level>\tDebug level\n"
    	    "\t-f <file>\tConfiguration file (mandatory)\n"
	    "\t-l <s|f<file>> \tLog on (s)yslog, (f)ile (syslog is default)\n"
	    "\t-p <dir>\tYang directory path (see CLICON_YANG_DIR)\n"
	    "\t-d <dir>\tSpecify restconf plugin directory dir (default: %s)\n"
	    "\t-y <file>\tLoad yang spec file (override yang main module)\n"
    	    "\t-a UNIX|IPv4|IPv6\tInternal backend socket family\n"
    	    "\t-u <path|addr>\tInternal socket domain path or IP addr (see -a)\n"
	    "\t-o \"<option>=<value>\"\tGive configuration option overriding config file (see clixon-config.yang)\n",
	    argv0,
	    clicon_restconf_dir(h)
	    );
    exit(0);
}

/*! Main routine for fastcgi API
 */
int 
main(int    argc, 
     char **argv) 
{
    int            retval = -1;
    int            sock;
    char	  *argv0 = argv[0];
    FCGX_Request   request;
    FCGX_Request  *r = &request;
    int            c;
    char          *sockpath;
    char          *path;
    clicon_handle  h;
    char          *dir;
    int            logdst = CLICON_LOG_SYSLOG;
    yang_stmt     *yspec = NULL;
    yang_stmt     *yspecfg = NULL; /* For config XXX clixon bug */
    char          *stream_path;
    int            finish;
    char          *str;
    clixon_plugin *cp = NULL;
    
    /* In the startup, logs to stderr & debug flag set later */
    clicon_log_init(__PROGRAM__, LOG_INFO, logdst); 

    /* Create handle */
    if ((h = clicon_handle_init()) == NULL)
	goto done;

    _CLICON_HANDLE = h; /* for termination handling */
    while ((c = getopt(argc, argv, RESTCONF_OPTS)) != -1)
	switch (c) {
	case 'h':
	    usage(h, argv[0]);
	    break;
	case 'D' : /* debug */
	    if (sscanf(optarg, "%d", &debug) != 1)
		usage(h, argv[0]);
	    break;
	 case 'f': /* override config file */
	    if (!strlen(optarg))
		usage(h, argv[0]);
	    clicon_option_str_set(h, "CLICON_CONFIGFILE", optarg);
	    break;
	 case 'l': /* Log destination: s|e|o */
	     if ((logdst = clicon_log_opt(optarg[0])) < 0)
		usage(h, argv[0]);
	    if (logdst == CLICON_LOG_FILE &&
		strlen(optarg)>1 &&
		clicon_log_file(optarg+1) < 0)
		goto done;
	   break;
	} /* switch getopt */
    /* 
     * Logs, error and debug to stderr or syslog, set debug level
     */
    clicon_log_init(__PROGRAM__, debug?LOG_DEBUG:LOG_INFO, logdst); 

    clicon_debug_init(debug, NULL); 
    clicon_log(LOG_NOTICE, "%s: %u Started", __PROGRAM__, getpid());
    if (set_signal(SIGTERM, restconf_sig_term, NULL) < 0){
	clicon_err(OE_DEMON, errno, "Setting signal");
	goto done;
    }
    if (set_signal(SIGINT, restconf_sig_term, NULL) < 0){
	clicon_err(OE_DEMON, errno, "Setting signal");
	goto done;
    }
    if (set_signal(SIGCHLD, restconf_sig_child, NULL) < 0){
	clicon_err(OE_DEMON, errno, "Setting signal");
	goto done;
    }

    /* Create configure yang-spec */
    if ((yspecfg = yspec_new()) == NULL)
	goto done;
    /* Find and read configfile */
    if (clicon_options_main(h, yspecfg) < 0)
	goto done;
    clicon_config_yang_set(h, yspecfg);
    stream_path = clicon_option_str(h, "CLICON_STREAM_PATH");
    /* Now rest of options, some overwrite option file */
    optind = 1;
    opterr = 0;
    while ((c = getopt(argc, argv, RESTCONF_OPTS)) != -1)
	switch (c) {
	case 'h' : /* help */
	case 'D' : /* debug */
	case 'f':  /* config file */
	case 'l':  /* log  */
	    break; /* see above */
	case 'p' : /* yang dir path */
	    if (clicon_option_add(h, "CLICON_YANG_DIR", optarg) < 0)
		goto done;
	    break;
	case 'd':  /* Plugin directory */
	    if (!strlen(optarg))
		usage(h, argv[0]);
	    clicon_option_str_set(h, "CLICON_RESTCONF_DIR", optarg);
	    break;
	case 'y' : /* Load yang spec file (override yang main module) */
	    clicon_option_str_set(h, "CLICON_YANG_MAIN_FILE", optarg);
	    break;
	case 'a': /* internal backend socket address family */
	    clicon_option_str_set(h, "CLICON_SOCK_FAMILY", optarg);
	    break;
	case 'u': /* internal backend socket unix domain path or ip host */
	    if (!strlen(optarg))
		usage(h, argv[0]);
	    clicon_option_str_set(h, "CLICON_SOCK", optarg);
	    break;
	case 'o':{ /* Configuration option */
	    char          *val;
	    if ((val = index(optarg, '=')) == NULL)
		usage(h, argv0);
	    *val++ = '\0';
	    if (clicon_option_add(h, optarg, val) < 0)
		goto done;
	    break;
	}
        default:
            usage(h, argv[0]);
            break;
	}
    argc -= optind;
    argv += optind;

    /* Access the remaining argv/argc options (after --) w clicon-argv_get() */
    clicon_argv_set(h, argv0, argc, argv);
    
    /* Add (hardcoded) netconf features in case ietf-netconf loaded here
     * Otherwise it is loaded in netconf_module_load below
     */
    if (netconf_module_features(h) < 0)
	goto done;

    /* Create top-level yang spec and store as option */
    if ((yspec = yspec_new()) == NULL)
	goto done;
    clicon_dbspec_yang_set(h, yspec);
    
    /* Load restconf plugins before yangs are loaded (eg extension callbacks) */
    if ((dir = clicon_restconf_dir(h)) != NULL)
	if (clixon_plugins_load(h, CLIXON_PLUGIN_INIT, dir, NULL) < 0)
	    return -1;
    /* Create a pseudo-plugin to create extension callback to set the ietf-routing
     * yang-data extension for api-root top-level restconf function.
     */
    if (clixon_pseudo_plugin(h, "pseudo restconf", &cp) < 0)
	goto done;
    cp->cp_api.ca_extension = restconf_main_extension_cb;

    /* Load Yang modules
     * 1. Load a yang module as a specific absolute filename */
    if ((str = clicon_yang_main_file(h)) != NULL){
	if (yang_spec_parse_file(h, str, yspec) < 0)
	    goto done;
    }
    /* 2. Load a (single) main module */
    if ((str = clicon_yang_module_main(h)) != NULL){
	if (yang_spec_parse_module(h, str, clicon_yang_module_revision(h),
				   yspec) < 0)
	    goto done;
    }
    /* 3. Load all modules in a directory */
    if ((str = clicon_yang_main_dir(h)) != NULL){
	if (yang_spec_load_dir(h, str, yspec) < 0)
	    goto done;
    }
    /* Load clixon lib yang module */
    if (yang_spec_parse_module(h, "clixon-lib", NULL, yspec) < 0)
	goto done;
     /* Load yang module library, RFC7895 */
    if (yang_modules_init(h) < 0)
	goto done;

    /* Load yang restconf module */
    if (yang_spec_parse_module(h, "ietf-restconf", NULL, yspec)< 0)
	goto done;
    
    /* Add netconf yang spec, used as internal protocol */
    if (netconf_module_load(h) < 0)
	goto done;
    
    /* Add system modules */
     if (clicon_option_bool(h, "CLICON_STREAM_DISCOVERY_RFC8040") &&
	 yang_spec_parse_module(h, "ietf-restconf-monitoring", NULL, yspec)< 0)
	 goto done;
     if (clicon_option_bool(h, "CLICON_STREAM_DISCOVERY_RFC5277") &&
	 yang_spec_parse_module(h, "clixon-rfc5277", NULL, yspec)< 0)
	 goto done;

     /* Dump configuration options on debug */
    if (debug)      
	clicon_option_dump(h, debug);

    /* Call start function in all plugins before we go interactive 
     */
     if (clixon_plugin_start(h) < 0)
	 goto done;

    if ((sockpath = clicon_option_str(h, "CLICON_RESTCONF_PATH")) == NULL){
	clicon_err(OE_CFG, errno, "No CLICON_RESTCONF_PATH in clixon configure file");
	goto done;
    }
    if (FCGX_Init() != 0){ /* How to cleanup memory after this? */
	clicon_err(OE_CFG, errno, "FCGX_Init");
	goto done;
    }
    clicon_debug(1, "restconf_main: Opening FCGX socket: %s", sockpath);
    if ((sock = FCGX_OpenSocket(sockpath, 10)) < 0){
	clicon_err(OE_CFG, errno, "FCGX_OpenSocket");
	goto done;
    }
    if (clicon_socket_set(h, sock) < 0)
	goto done;
    /* umask settings may interfer: we want group to write: this is 774 */
    if (chmod(sockpath, S_IRWXU|S_IRWXG|S_IROTH) < 0){
	clicon_err(OE_UNIX, errno, "chmod");
	goto done;
    }
    if (FCGX_InitRequest(r, sock, 0) != 0){
	clicon_err(OE_CFG, errno, "FCGX_InitRequest");
	goto done;
    }
    while (1) {
	finish = 1; /* If zero, dont finish request, initiate new */

	if (FCGX_Accept_r(r) < 0) {
	    clicon_err(OE_CFG, errno, "FCGX_Accept_r");
	    goto done;
	}
	clicon_debug(1, "------------");
	if ((path = FCGX_GetParam("REQUEST_URI", r->envp)) != NULL){
	    clicon_debug(1, "path: %s", path);
	    if (strncmp(path, "/" RESTCONF_API, strlen("/" RESTCONF_API)) == 0)
		api_restconf(h, r); /* This is the function */
	    else if (strncmp(path+1, stream_path, strlen(stream_path)) == 0) {
		api_stream(h, r, stream_path, &finish); 
	    }
	    else if (strncmp(path, RESTCONF_WELL_KNOWN, strlen(RESTCONF_WELL_KNOWN)) == 0) {
		api_well_known(h, r); /*  */
	    }
	    else{
		clicon_debug(1, "top-level %s not found", path);
		restconf_notfound(r);
	    }
	}
	else
	    clicon_debug(1, "NULL URI");
	if (finish)
	    FCGX_Finish_r(r);
	else{ /* A handler is forked so we initiate a new request after instead 
		 of finnishing the old */
	    if (FCGX_InitRequest(r, sock, 0) != 0){
		clicon_err(OE_CFG, errno, "FCGX_InitRequest");
		goto done;
	    }
	}
    }
    retval = 0;
 done:
    stream_child_freeall(h);
    restconf_terminate(h);
    return retval;
}
