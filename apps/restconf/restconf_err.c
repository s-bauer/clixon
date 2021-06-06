/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2019 Olof Hagsand
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
  * Return errors
  * @see RFC 7231 Hypertext Transfer Protocol (HTTP/1.1): Semantics and Content


 * "api-path" is "URI-encoded path expression" definition in RFC8040 3.5.3
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
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <assert.h>
#include <dlfcn.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/wait.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clixon/clixon.h>

#include "restconf_lib.h"
#include "restconf_api.h"
#include "restconf_err.h"

/*! HTTP error 405 Not Allowed
 * @param[in]  req       Generic http handle
 * @param[in]  allow     Which methods are allowed
 * @param[in]  pretty    Pretty-print of reply 
 * @param[in]  media_out Restconf output media
 */
int
restconf_method_notallowed(clicon_handle  h,
			   void          *req,
			   char          *allow,
			   int            pretty,
			   restconf_media media)
{
    int    retval = -1;
    cxobj *xerr = NULL;

    if (netconf_operation_not_supported_xml(&xerr, "protocol", "Method not allowed") < 0)
	goto done;
    /* Assume not-supported mapped to Not Allowed with allow header */
    if (restconf_reply_header(req, "Allow", "%s", allow) < 0)
	goto done;
    if (api_return_err0(h, req, xerr, pretty, YANG_DATA_JSON, 0) < 0)
	goto done;
    retval = 0;
 done:
    if (xerr)
	xml_free(xerr);
    return retval;
}

/*! HTTP error 415 Unsupported media
 * @param[in]  req      Generic http handle
 * RFC8040, section 5.2:
 * If the server does not support the requested input encoding for a request, then it MUST
 * return an error response with a "415 Unsupported Media Type" status-line
 */
int
restconf_unsupported_media(clicon_handle  h,
			   void          *req,
			   int            pretty,
			   restconf_media media)
{
    int    retval = -1;
    cxobj *xerr = NULL;

    if (netconf_operation_not_supported_xml(&xerr, "protocol", "Unsupported Media Type") < 0)
	goto done;
    /* override with 415 netconf->restoconf translation which gives a 405 */
    if (api_return_err0(h, req, xerr, pretty, media, 415) < 0) 
	goto done;
    retval = 0;
 done:
    if (xerr)
	xml_free(xerr);
    return retval;
}

/*! HTTP error 406 Not acceptable
 *
 * @param[in]  req      Generic http handle
 * RFC8040, section 5.2:
 * If the server does not support any of the requested output encodings for a request, then it MUST
 * return an error response with a "406 Not Acceptable" status-line.
 */
int
restconf_not_acceptable(clicon_handle  h,
			void          *req,
			int            pretty,
			restconf_media media)
{
    int    retval = -1;
    cxobj *xerr = NULL;

    if (netconf_operation_not_supported_xml(&xerr, "protocol", "Unacceptable output encoding") < 0)
	goto done;
    /* Override with 415 netconf->restoconf translation which gives a 405 */
    if (api_return_err0(h, req, xerr, pretty, media, 415) < 0) 
	goto done;
    if (restconf_reply_send(req, 415, NULL) < 0)
	goto done;
    retval = 0;
 done:
    return retval;
}

/*! HTTP error 501 Not implemented
 * @param[in]  req    Generic http handle
 */
int
restconf_notimplemented(clicon_handle  h,
			void          *req,
			int            pretty,
			restconf_media media)
{
    int   retval = -1;
    cxobj *xerr = NULL;

    if (netconf_operation_not_supported_xml(&xerr, "protocol", "Not Implemented") < 0)
	goto done;
    /* Override with 501 Not Implemented netconf->restoconf translation which gives a 405 */
    if (api_return_err0(h, req, xerr, pretty, YANG_DATA_JSON, 501) < 0)
	goto done;
    retval = 0;
 done:
    if (xerr)
	xml_free(xerr);
    return retval;
}

/*! Generic restconf error function on get/head request
 * @param[in]  h      Clixon handle
 * @param[in]  req    Generic http handle
 * @param[in]  xerr   XML error message (eg from backend, or from a clixon_netconf_lib function)
 * @param[in]  pretty Set to 1 for pretty-printed xml/json output
 * @param[in]  media  Output media
 * @param[in]  code   If 0 use rfc8040 sec 7 netconf2restconf error-tag mapping
 *                    otherwise use this code
 * xerr should be on the form: <rpc-error>... otherwise an internal error is generated
 */
int
api_return_err(clicon_handle h,
	       void         *req,
	       cxobj        *xerr,
	       int           pretty,
	       restconf_media media,
	       int           code0)
{
    int        retval = -1;
    cbuf      *cb = NULL;
    cbuf      *cberr = NULL;
    cxobj     *xtag;
    char      *tagstr;
    int        code;	
    cxobj     *xerr2 = NULL;

    clicon_debug(1, "%s", __FUNCTION__);
    if ((cb = cbuf_new()) == NULL){
	clicon_err(OE_UNIX, errno, "cbuf_new");
	goto done;
    }
    /* A well-formed error message when entering here should look like:
     * <rpc-error>...<error-tag>invalid-value</error-tag>
     * Check this is so, otherwise generate an internal error.
     */
    if (strcmp(xml_name(xerr), "rpc-error") != 0 ||
	(xtag = xpath_first(xerr, NULL, "error-tag")) == NULL){
	if ((cberr = cbuf_new()) == NULL){
	    clicon_err(OE_UNIX, errno, "cbuf_new");
	    goto done;
	}
	cprintf(cberr, "Internal error, system returned invalid error message: ");
	if (netconf_err2cb(xerr, cberr) < 0)
	    goto done;
	if (netconf_operation_failed_xml(&xerr2, "application",
					 cbuf_get(cberr)) < 0)
	    goto done;
	if ((xerr = xpath_first(xerr2, NULL, "rpc-error")) == NULL){
	    clicon_err(OE_XML, 0, "Internal error, shouldnt happen");
	    goto done;
	}
	if ((xtag = xpath_first(xerr, NULL, "error-tag")) == NULL){
	    clicon_err(OE_XML, 0, "Internal error, shouldnt happen");
	    goto done;
	}
    }
#if 1
    if (clicon_debug_get())
	clicon_log_xml(LOG_DEBUG, xerr, "%s Send error:", __FUNCTION__);
#endif
    if (xml_name_set(xerr, "error") < 0)
	goto done;
    tagstr = xml_body(xtag);
    if (code0 != 0)
	code = code0;
    else{
	if ((code = restconf_err2code(tagstr)) < 0)
	    code = 500; /* internal server error */
	if (code == 403){
	    /* Special case: netconf only has "access denied" while restconf 
	     * differentiates between:
	     *   401 Unauthorized If the RESTCONF client is not authenticated (sec 2.5)
	     *   403 Forbidden    If the user is not authorized to access a target resource or invoke 
	     *                    an operation
	     */
	    cxobj *xmsg;
	    char  *mb;
	    if ((xmsg = xpath_first(xerr, NULL, "error-message")) != NULL &&
		(mb = xml_body(xmsg)) != NULL &&
		strcmp(mb, "The requested URL was unauthorized") == 0)
		code = 401;
	}
    }  
    if (restconf_reply_header(req, "Content-Type", "%s", restconf_media_int2str(media)) < 0) // XXX
	goto done;
    switch (media){
    case YANG_DATA_XML:
	clicon_debug(1, "%s code:%d", __FUNCTION__, code);
	if (pretty){
	    cprintf(cb, "    <errors xmlns=\"urn:ietf:params:xml:ns:yang:ietf-restconf\">\n");
	    if (clicon_xml2cbuf(cb, xerr, 2, pretty, -1) < 0)
		goto done;
	    cprintf(cb, "    </errors>\r\n");
	}
	else {
	    cprintf(cb, "<errors xmlns=\"urn:ietf:params:xml:ns:yang:ietf-restconf\">");
	    if (clicon_xml2cbuf(cb, xerr, 2, pretty, -1) < 0)
		goto done;
	    cprintf(cb, "</errors>\r\n");
	}
	break;
    case YANG_DATA_JSON:
	clicon_debug(1, "%s code:%d", __FUNCTION__, code);
	if (pretty){
	    cprintf(cb, "{\n\"ietf-restconf:errors\" : ");
	    if (xml2json_cbuf(cb, xerr, pretty) < 0)
		goto done;
	    cprintf(cb, "\n}\r\n");
	}
	else{
	    cprintf(cb, "{");
	    cprintf(cb, "\"ietf-restconf:errors\":");
	    if (xml2json_cbuf(cb, xerr, pretty) < 0)
		goto done;
	    cprintf(cb, "}\r\n");
	}
	break;
    default:
	clicon_err(OE_YANG, EINVAL, "Invalid media type %d", media);
	goto done;
	break;
    } /* switch media */
    assert(cbuf_len(cb));
    if (restconf_reply_send(req, code, cb) < 0)
	goto done;
    // ok:
    retval = 0;
 done:
    clicon_debug(1, "%s retval:%d", __FUNCTION__, retval);
    if (cb)
        cbuf_free(cb);
    if (cberr)
        cbuf_free(cberr);
    return retval;
}

/*! Generic restconf error function on get/head request
 *
 * Variant of api_return_err on the form <xxx><rpc-error>...
 * This is the form most functions in clixon_netconf_lib returns errors.
 * @param[in]  h      Clixon handle
 * @param[in]  req    Generic http handle
 * @param[in]  xerr   XML error message (eg from backend, or from a clixon_netconf_lib function)
 * @param[in]  pretty Set to 1 for pretty-printed xml/json output
 * @param[in]  media  Output media
 * @param[in]  code   If 0 use rfc8040 sec 7 netconf2restconf error-tag mapping
 *                    otherwise use this code
 * @see api_return_err where top level is expected to be <rpc-error>
 */
int
api_return_err0(clicon_handle h,
		void         *req,
		cxobj        *xerr,
		int           pretty,
		restconf_media media,
		int           code)
{
    int    retval = -1;
    cxobj *xe;

    if ((xe = xpath_first(xerr, NULL, "rpc-error")) == NULL){
	clicon_err(OE_XML, EINVAL, "Expected xml on the form <rpc-error>..");
	goto done;
    }
    if (api_return_err(h, req, xe, pretty, media, code) < 0)
	goto done;
    retval = 0;
 done:
    return retval;
}
