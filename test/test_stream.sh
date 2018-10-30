#!/bin/bash
# Tests for event streams using notifications
# Assumptions:
# 1. http server setup, such as nginx described in apps/restconf/README.md
#    especially SSE - ngchan setup
# 2. Example stream as Clixon example which needs registration, callback and
#    notification generating code every 5s
#
# Testing of streams is quite complicated.
# Here are some testing dimensions in restconf alone:
# - start/stop subscription
# - start-time/stop-time in subscription
# - stream retention time
# - native vs nchan implementation
# Focussing on 1-3
# 2a) start sub 8s - see 2 notifications
# 2b) start sub 8s - stoptime after 5s - see 1 notifications
# 2c) start sub 8s - replay from start -8s - see 4 notifications
# 2d) start sub 8s - replay from start -8s to stop +4s - see 3 notifications
# 2e) start sub 8s - replay from -90s w retention 60s - see 10 notifications

APPNAME=example
UTIL=../util/clixon_util_stream
DATE=$(date +"%Y-%m-%d")
# include err() and new() functions and creates $dir
. ./lib.sh
cfg=$dir/conf.xml
fyang=$dir/stream.yang
xml=$dir/xml.xml

#  <CLICON_YANG_MODULE_MAIN>example</CLICON_YANG_MODULE_MAIN>
cat <<EOF > $cfg
<config>
  <CLICON_CONFIGFILE>$cfg</CLICON_CONFIGFILE>
  <CLICON_YANG_DIR>/usr/local/share/clixon</CLICON_YANG_DIR>
  <CLICON_RESTCONF_PRETTY>false</CLICON_RESTCONF_PRETTY>
  <CLICON_SOCK>/usr/local/var/$APPNAME/$APPNAME.sock</CLICON_SOCK>
  <CLICON_BACKEND_DIR>/usr/local/lib/$APPNAME/backend</CLICON_BACKEND_DIR>
  <CLICON_BACKEND_REGEXP>example_backend.so$</CLICON_BACKEND_REGEXP>
  <CLICON_BACKEND_PIDFILE>$dir/restconf.pidfile</CLICON_BACKEND_PIDFILE>
  <CLICON_XMLDB_DIR>/usr/local/var/$APPNAME</CLICON_XMLDB_DIR>
  <CLICON_XMLDB_PLUGIN>/usr/local/lib/xmldb/text.so</CLICON_XMLDB_PLUGIN>
  <CLICON_CLISPEC_DIR>/usr/local/lib/$APPNAME/clispec</CLICON_CLISPEC_DIR>
  <CLICON_CLI_DIR>/usr/local/lib/$APPNAME/cli</CLICON_CLI_DIR>
  <CLICON_CLI_MODE>$APPNAME</CLICON_CLI_MODE>
  <CLICON_MODULE_LIBRARY_RFC7895>true</CLICON_MODULE_LIBRARY_RFC7895>
  <CLICON_STREAM_DISCOVERY_RFC5277>true</CLICON_STREAM_DISCOVERY_RFC5277>
  <CLICON_STREAM_DISCOVERY_RFC8040>true</CLICON_STREAM_DISCOVERY_RFC8040>
  <CLICON_STREAM_PATH>streams</CLICON_STREAM_PATH>
  <CLICON_STREAM_URL>https://localhost</CLICON_STREAM_URL>
  <CLICON_STREAM_RETENTION>60</CLICON_STREAM_RETENTION>
</config>
EOF

# RFC5277 NETCONF Event Notifications 
# using reportingEntity (rfc5277) not reporting-entity (rfc8040)
cat <<EOF > $fyang
     module example {
       namespace "http://example.com/event/1.0";
       prefix ex;
       organization "Example, Inc.";
       contact "support at example.com";
       description "Example Notification Data Model Module.";
       revision "2016-07-07" {
         description "Initial version.";
         reference "example.com document 2-9976.";
       }
       notification event {
         description "Example notification event.";
         leaf event-class {
           type string;
           description "Event class identifier.";
         }
         container reportingEntity {
           description "Event specific information.";
           leaf card {
             type string;
             description "Line card identifier.";
           }
         }
         leaf severity {
           type string;
           description "Event severity description.";
         }
       }
       container state {
         config false;
         description "state data for the example application (must be here for example get operation)";
         leaf-list op {
            type string;
         }
       }
   }
EOF

# kill old backend (if any)
new "kill old backend"
sudo clixon_backend -zf $cfg
if [ $? -ne 0 ]; then
    err
fi
new "start backend -s init -f $cfg -y $fyang"
sudo $clixon_backend -s init -f $cfg -y $fyang # -D 1

if [ $? -ne 0 ]; then
    err
fi

new "kill old restconf daemon"
sudo pkill -u www-data clixon_restconf
      
new "start restconf daemon"
sudo start-stop-daemon -S -q -o -b -x /www-data/clixon_restconf -d /www-data -c www-data -- -f $cfg  -y $fyang # -D 1

sleep 2

#
# 1. Netconf RFC5277 stream testing
new "1. Netconf RFC5277 stream testing"
# 1.1 Stream discovery
new "netconf event stream discovery RFC5277 Sec 3.2.5"
expecteof "$clixon_netconf -qf $cfg -y $fyang" 0 '<rpc><get><filter type="xpath" select="netconf/streams" xmlns="urn:ietf:params:xml:ns:netmod:notification"/></get></rpc>]]>]]>' '<rpc-reply><data><netconf><streams><stream><name>EXAMPLE</name><description>Example event stream</description><replay-support>true</replay-support></stream></streams></netconf></data></rpc-reply>]]>]]>'

new "netconf event stream discovery RFC8040 Sec 6.2"
expecteof "$clixon_netconf -qf $cfg -y $fyang" 0 '<rpc><get><filter type="xpath" select="restconf-state/streams" xmlns="urn:ietf:params:xml:ns:netmod:notification"/></get></rpc>]]>]]>' '<rpc-reply><data><restconf-state><streams><stream><name>EXAMPLE</name><description>Example event stream</description><replay-support>true</replay-support><access><encoding>xml</encoding><location>https://localhost/streams/EXAMPLE</location></access></stream></streams></restconf-state></data></rpc-reply>]]>]]>'

#
# 1.2 Netconf stream subscription
new "netconf EXAMPLE subscription"
expectwait "$clixon_netconf -qf $cfg -y $fyang" '<rpc><create-subscription><stream>EXAMPLE</stream></create-subscription></rpc>]]>]]>' '^<rpc-reply><ok/></rpc-reply>]]>]]><notification xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0"><eventTime>20' 5

new "netconf subscription with empty startTime"
expectwait "$clixon_netconf -qf $cfg -y $fyang" '<rpc><create-subscription><stream>EXAMPLE</stream><startTime/></create-subscription></rpc>]]>]]>' '^<rpc-reply><ok/></rpc-reply>]]>]]><notification xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0"><eventTime>20' 5

new "netconf EXAMPLE subscription with simple filter"
expectwait "$clixon_netconf -qf $cfg -y $fyang" "<rpc><create-subscription><stream>EXAMPLE</stream><filter type=\"xpath\" select=\"event\"/></create-subscription></rpc>]]>]]>" '^<rpc-reply><ok/></rpc-reply>]]>]]><notification xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0"><eventTime>20' 5

new "netconf EXAMPLE subscription with filter classifier"
expectwait "$clixon_netconf -qf $cfg -y $fyang" "<rpc><create-subscription><stream>EXAMPLE</stream><filter type=\"xpath\" select=\"event[event-class='fault']\"/></create-subscription></rpc>]]>]]>" '^<rpc-reply><ok/></rpc-reply>]]>]]><notification xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0"><eventTime>20' 5

new "netconf NONEXIST subscription"
expectwait "$clixon_netconf -qf $cfg -y $fyang" '<rpc><create-subscription><stream>NONEXIST</stream></create-subscription></rpc>]]>]]>' '^<rpc-reply><rpc-error><error-tag>invalid-value</error-tag><error-type>application</error-type><error-severity>error</error-severity><error-message>No such stream</error-message></rpc-error></rpc-reply>]]>]]>$' 5

new "netconf EXAMPLE subscription with wrong date"
expectwait "$clixon_netconf -qf $cfg -y $fyang" '<rpc><create-subscription><stream>EXAMPLE</stream><startTime>kallekaka</startTime></create-subscription></rpc>]]>]]>' '^<rpc-reply><rpc-error><error-tag>bad-element</error-tag><error-type>application</error-type><error-info><bad-element>startTime</bad-element></error-info><error-severity>error</error-severity><error-message>Expected timestamp</error-message></rpc-error></rpc-reply>]]>]]>$' 0

#new "netconf EXAMPLE subscription with replay"
#NOW=$(date +"%Y-%m-%dT%H:%M:%S")
#sleep 10
#expectwait "$clixon_netconf -qf $cfg -y $fyang" "<rpc><create-subscription><stream>EXAMPLE</stream><startTime>$NOW</startTime></create-subscription></rpc>]]>]]>" '^<rpc-reply><ok/></rpc-reply>]]>]]><notification xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0"><eventTime>20' 10

#
# 2. Restconf RFC8040 stream testing
new "2. Restconf RFC8040 stream testing"
# 2.1 Stream discovery
new "restconf event stream discovery RFC8040 Sec 6.2"
expectfn "curl -s -X GET http://localhost/restconf/data/ietf-restconf-monitoring:restconf-state/streams" 0 '{"streams": {"stream": \[{"name": "EXAMPLE","description": "Example event stream","replay-support": true,"access": \[{"encoding": "xml","location": "https://localhost/streams/EXAMPLE"}\]}\]}'

new "restconf subscribe RFC8040 Sec 6.3, get location"
expectfn "curl -s -X GET http://localhost/restconf/data/ietf-restconf-monitoring:restconf-state/streams/stream=EXAMPLE/access=xml/location" 0 '{"location": "https://localhost/streams/EXAMPLE"}'

# Restconf stream subscription RFC8040 Sec 6.3
# Start Subscription w error
new "restconf monitor event nonexist stream"
expectwait 'curl -s -X GET -H "Accept: text/event-stream" -H "Cache-Control: no-cache" -H "Connection: keep-alive" http://localhost/streams/NOTEXIST' 0 '<errors xmlns="urn:ietf:params:xml:ns:yang:ietf-restconf"><error><error-tag>invalid-value</error-tag><error-type>application</error-type><error-severity>error</error-severity><error-message>No such stream</error-message></error></errors>' 2

# 2a) start subscription 8s - see 1-2 notifications
new "2a) start subscriptions 8s - see 2 notifications"
ret=$($UTIL -u http://localhost/streams/EXAMPLE -t 8)
expect="data: <notification xmlns=\"urn:ietf:params:xml:ns:netconf:notification:1.0\"><eventTime>${DATE}T[0-9:.]*</eventTime><event><event-class>fault</event-class><reportingEntity><card>Ethernet0</card></reportingEntity><severity>major</severity></event>"

match=$(echo "$ret" | grep -Eo "$expect")
if [ -z "$match" ]; then
    err "$expect" "$ret"
fi
nr=$(echo "$ret" | grep -c "data:")
if [ $nr != 1 -a $nr != 2 ]; then
    err 2 "$nr"
fi

sleep 2
# 2b) start subscription 8s - stoptime after 5s - see 1-2 notifications
new "2b) start subscriptions 8s - stoptime after 5s - see 1 notifications"
ret=$($UTIL -u http://localhost/streams/EXAMPLE -t 8 -e +10)
expect="data: <notification xmlns=\"urn:ietf:params:xml:ns:netconf:notification:1.0\"><eventTime>${DATE}T[0-9:.]*</eventTime><event><event-class>fault</event-class><reportingEntity><card>Ethernet0</card></reportingEntity><severity>major</severity></event>"
match=$(echo "$ret" | grep -Eo "$expect")
if [ -z "$match" ]; then
    err "$expect" "$ret"
fi
nr=$(echo "$ret" | grep -c "data:")
if [ $nr != 1 -a $nr != 2 ]; then
    err 1 "$nr"
fi

sleep 2
# 2c
new "2c) start sub 8s - replay from start -8s - see 3-4 notifications"
ret=$($UTIL -u http://localhost/streams/EXAMPLE -t 10 -s -8)
expect="data: <notification xmlns=\"urn:ietf:params:xml:ns:netconf:notification:1.0\"><eventTime>${DATE}T[0-9:.]*</eventTime><event><event-class>fault</event-class><reportingEntity><card>Ethernet0</card></reportingEntity><severity>major</severity></event>"
match=$(echo "$ret" | grep -Eo "$expect")
if [ -z "$match" ]; then
    err "$expect" "$ret"
fi
nr=$(echo "$ret" | grep -c "data:")
if [ $nr != 3 -a $nr != 4 ]; then
    err 4 "$nr"
fi
exit
#-----------------

sudo pkill -u www-data clixon_restconf

new "Kill backend"
# kill backend
sudo clixon_backend -zf $cfg
if [ $? -ne 0 ]; then
    err "kill backend"
fi

# Check if still alive
pid=`pgrep clixon_backend`
if [ -n "$pid" ]; then
    sudo kill $pid
fi

rm -rf $dir
exit
#--------------------------------------------------------------------
# Need manual testing
new "restconf monitor streams native NEEDS manual testing"
if false; then
    # url -H "Accept: text/event-stream" http://localhost/streams/EXAMPLE
    # Expect:
    # data: <notification xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0"><eventTime>2018-10-21T19:22:11.381827</eventTime><event><event-class>fault</event-class><reportingEntity><card>Ethernet0</card></reportingEntity><severity>major</severity></event></notification>
    #
    # data: <notification xmlns="urn:ietf:params:xml:ns:netconf:notification:1.0"><eventTime>2018-10-21T19:22:16.387228</eventTime><event><event-class>fault</event-class><reportingEntity><card>Ethernet0</card></reportingEntity><severity>major</severity></event></notification>

new "restconf monitor event ok stream"
expectwait 'curl -s -X GET  -H "Accept: text/event-stream" -H "Cache-Control: no-cache" -H "Connection: keep-alive" http://localhost/streams/EXAMPLE' 0 'foo' 5

new "restconf monitor event starttime"
NOW=$(date +"%Y-%m-%dT%H%%3A%M%%3A%S")
sleep 10
expectwait "curl -s -X GET  -H \"Accept: text/event-stream\" -H \"Cache-Control: no-cache\" -H \"Connection: keep-alive\" http://localhost/streams/EXAMPLE?start-time=$NOW" 0 'foo' 2
fi

# Restconf stream subscription RFC8040 Sec 6.3 - Nginx nchan solution
# Need manual testing
new "restconf monitor streams nchan NEEDS manual testing"
if false; then
    # url -H "Accept: text/event-stream" http://localhost/streams/EXAMPLE
    # Expect:
    echo foo
fi



