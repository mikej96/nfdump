/*
 *  Copyright (c) 2019-2021, Peter Haag
 *  All rights reserved.
 *  
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions are met:
 *  
 *   * Redistributions of source code must retain the above copyright notice, 
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice, 
 *     this list of conditions and the following disclaimer in the documentation 
 *     and/or other materials provided with the distribution.
 *   * Neither the name of the author nor the names of its contributors may be 
 *     used to endorse or promote products derived from this software without 
 *     specific prior written permission.
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 *  POSSIBILITY OF SUCH DAMAGE.
 *  
 */

#include "config.h"

#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include "util.h"
#include "nfdump.h"
#include "nffile.h"
#include "nfxV3.h"
#include "nbar.h"
#include "maxmind.h"
#include "output_util.h"
#include "output_raw.h"
#include "content_dns.h"

#define IP_STRING_LEN (INET6_ADDRSTRLEN)

// record counter 
static uint32_t recordCount;

static void DumpHex(FILE *stream, const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		fprintf(stream, "%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			fprintf(stream, " ");
			if ((i+1) % 16 == 0) {
				fprintf(stream, "|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					fprintf(stream, " ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					fprintf(stream, "   ");
				}
				fprintf(stream, "|  %s \n", ascii);
			}
		}
	}
}

static void stringEXgenericFlow(FILE *stream, master_record_t *r) {
char datestr1[64], datestr2[64], datestr3[64];

	struct tm *ts;
	time_t when = r->msecFirst / 1000LL;
	if ( when == 0 ) {
		strncpy(datestr1, "<unknown>", 63);
	} else {
		ts = localtime(&when);
		strftime(datestr1, 63, "%Y-%m-%d %H:%M:%S", ts);
	}

	when = r->msecLast / 1000LL;
	if ( when == 0 ) {
		strncpy(datestr2, "<unknown>", 63);
	} else {
		ts = localtime(&when);
		strftime(datestr2, 63, "%Y-%m-%d %H:%M:%S", ts);
	}

	if ( r->msecReceived ) {
		when = r->msecReceived / 1000LL;
		ts = localtime(&when);
		strftime(datestr3, 63, "%Y-%m-%d %H:%M:%S", ts);
	} else {
		datestr3[0] = '0';
		datestr3[1] = '\0';
	}

	int len = fprintf(stream, 
"  first        =     %13llu [%s.%03llu]\n"
"  last         =     %13llu [%s.%03llu]\n"
"  received at  =     %13llu [%s.%03llu]\n"
"  proto        =               %3u %s\n"
"  tcp flags    =              0x%.2x %s\n"
, (long long unsigned)r->msecFirst, datestr1, r->msecFirst % 1000LL
, (long long unsigned)r->msecLast, datestr2, r->msecLast % 1000LL
, (long long unsigned)r->msecReceived, datestr3, (long long unsigned)r->msecReceived % 1000L
, r->proto, ProtoString(r->proto, 0)
, r->proto == IPPROTO_TCP ? r->tcp_flags : 0, FlagsString(r->proto == IPPROTO_TCP ? r->tcp_flags :0));

	if ( r->proto == IPPROTO_ICMP || r->proto == IPPROTO_ICMPV6 ) { // ICMP
		len = fprintf(stream,
"  ICMP         =              %2u.%-2u type.code\n"
	, r->icmp_type, r->icmp_code);
	} else {
		len = fprintf(stream,
"  src port     =             %5u\n"
"  dst port     =             %5u\n"
"  src tos      =               %3u\n"
	, r->srcPort, r->dstPort, r->tos);
	}

	fprintf(stream,
"  in packets   =        %10llu\n"
"  in bytes     =        %10llu\n"
	, (unsigned long long)r->inPackets, (unsigned long long)r->inBytes);

} // End of EXgenericFlowID


static void stringsEXipv4Flow(FILE *stream, master_record_t *r) {
char as[IP_STRING_LEN], ds[IP_STRING_LEN];
char sloc[128], dloc[128];

	uint32_t src = htonl(r->V4.srcaddr);
	uint32_t dst = htonl(r->V4.dstaddr);
	inet_ntop(AF_INET, &src, as, sizeof(as));
	inet_ntop(AF_INET, &dst, ds, sizeof(ds));

	LookupLocation(r->V6.srcaddr, sloc, 128);
	LookupLocation(r->V6.dstaddr, dloc, 128);
	fprintf(stream,
"  src addr     =  %16s: %s\n"
"  dst addr     =  %16s: %s\n"
	, as, sloc, ds, dloc);

} // End of stringsEXipv4Flow

static void stringsEXipv6Flow(FILE *stream, master_record_t *r) {
char as[IP_STRING_LEN], ds[IP_STRING_LEN];
uint64_t src[2], dst[2];
char sloc[128], dloc[128];

	src[0] = htonll(r->V6.srcaddr[0]);
	src[1] = htonll(r->V6.srcaddr[1]);
	dst[0] = htonll(r->V6.dstaddr[0]);
	dst[1] = htonll(r->V6.dstaddr[1]);
	inet_ntop(AF_INET6, &src, as, sizeof(as));
	inet_ntop(AF_INET6, &dst, ds, sizeof(ds));

	LookupLocation(r->V6.srcaddr, sloc, 128);
	LookupLocation(r->V6.dstaddr, dloc, 128);
	fprintf(stream,
"  src addr     =  %16s: %s\n"
"  dst addr     =  %16s: %s\n"
	, as, sloc, ds, dloc);

} // End of stringsEXipv6Flow

static void stringsEXflowMisc(FILE *stream, master_record_t *r) {
char snet[IP_STRING_LEN], dnet[IP_STRING_LEN];

	if ( TestFlag(r->mflags, V3_FLAG_IPV6_ADDR)) {
		// IPv6
		inet6_ntop_mask(r->V6.srcaddr, r->src_mask, snet, sizeof(snet));
 		inet6_ntop_mask(r->V6.dstaddr, r->dst_mask, dnet, sizeof(dnet));
	} else {
		// IPv4
 		inet_ntop_mask(r->V4.srcaddr, r->src_mask, snet, sizeof(snet));
 		inet_ntop_mask(r->V4.dstaddr, r->dst_mask, dnet, sizeof(dnet));
	}

	fprintf(stream,
"  input        =          %8u\n"
"  output       =          %8u\n"
"  src mask     =             %5u %s/%u\n"
"  dst mask     =             %5u %s/%u\n"
"  fwd status   =               %3u\n"
"  dst tos      =               %3u\n"
"  direction    =               %3u\n"
"  biFlow Dir   =              0x%.2x %s\n"
"  end reason   =              0x%.2x %s\n"
	, r->input, r->output, 
	  r->src_mask, snet, r->src_mask, r->dst_mask, dnet, r->dst_mask, 
	  r->fwd_status, r->tos, r->dir, r->biFlowDir, biFlowString(r->biFlowDir),
	  r->flowEndReason, FlowEndString(r->flowEndReason));

} // End of stringsEXflowMisc

static void stringsEXcntFlow(FILE *stream, master_record_t *r) {

	fprintf(stream,
"  out packets  =        %10llu\n"
"  out bytes    =        %10llu\n"
"  aggr flows   =        %10llu\n"
	, (long long unsigned)r->out_pkts, (long long unsigned)r->out_bytes,
	  (long long unsigned)r->aggr_flows);

} // End of stringEXcntFlow

static void stringsEXvLan(FILE *stream, master_record_t *r) {

	fprintf(stream,
"  src vlan     =             %5u\n"
"  dst vlan     =             %5u\n"
, r->src_vlan, r->dst_vlan);

} // End of stringsEXvLan

static void stringsEXasRouting(FILE *stream, master_record_t *r) {

	fprintf(stream,
"  src as       =             %5u\n"
"  dst as       =             %5u\n"
, r->srcas, r->dstas);

} // End of stringsEXasRouting

static void stringsEXbgpNextHopV4(FILE *stream, master_record_t *r) {
char ip[IP_STRING_LEN];

	ip[0] = 0;
	uint32_t i = htonl(r->bgp_nexthop.V4);
	inet_ntop(AF_INET, &i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  bgp next hop =  %16s\n"
, ip);

} // End of stringsEXbgpNextHopV4

static void stringsEXbgpNextHopV6(FILE *stream, master_record_t *r) {
char ip[IP_STRING_LEN];
uint64_t i[2];

	i[0] = htonll(r->bgp_nexthop.V6[0]);
	i[1] = htonll(r->bgp_nexthop.V6[1]);
	inet_ntop(AF_INET6, i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  bgp next hop =  %16s\n"
, ip);

} // End of stringsEXbgpNextHopV6

static void stringsEXipNextHopV4(FILE *stream, master_record_t *r) {
char ip[IP_STRING_LEN];

	ip[0] = 0;
	uint32_t i = htonl(r->ip_nexthop.V4);
	inet_ntop(AF_INET, &i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  ip next hop  =  %16s\n"
, ip);

} // End of stringsEXipNextHopV4

static void stringsEXipNextHopV6(FILE *stream, master_record_t *r) {
char ip[IP_STRING_LEN];
uint64_t i[2];

	i[0] = htonll(r->ip_nexthop.V6[0]);
	i[1] = htonll(r->ip_nexthop.V6[1]);
	inet_ntop(AF_INET6, i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  ip next hop  =  %16s\n"
, ip);

} // End of stringsEXipNextHopV6

static void stringsEXipReceivedV4(FILE *stream, master_record_t *r) {
char ip[IP_STRING_LEN];

	ip[0] = 0;
	uint32_t i = htonl(r->ip_router.V4);
	inet_ntop(AF_INET, &i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  ip exporter  =  %16s\n"
, ip);

} // End of stringsEXipReceivedV4

static void stringsEXipReceivedV6(FILE *stream, master_record_t *r) {
char ip[IP_STRING_LEN];
uint64_t i[2];

	i[0] = htonll(r->ip_router.V6[0]);
	i[1] = htonll(r->ip_router.V6[1]);
	inet_ntop(AF_INET6, i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  ip exporter  =  %16s\n"
, ip);

} // End of stringsEXipReceivedV6

static void stringsEXmplsLabel(FILE *stream, master_record_t *r) {

	for (int i=0; i<10; i++ ) {
		if (r->mpls_label[i] != 0) {
			fprintf(stream,
"  MPLS Lbl %2u  =      %8u-%1u-%1u\n", i+1
, r->mpls_label[i] >> 4 , (r->mpls_label[i] & 0xF ) >> 1, r->mpls_label[i] & 1 );
		}
	}

} // End of stringsEXipReceivedV6

static void stringsEXmacAddr(FILE *stream, master_record_t *r) {
uint8_t mac1[6], mac2[6], mac3[6], mac4[6];

	for ( int i=0; i<6; i++ ) {
		mac1[i] = (r->in_src_mac >> ( i*8 )) & 0xFF;
		mac2[i] = (r->out_dst_mac >> ( i*8 )) & 0xFF;
		mac3[i] = (r->in_dst_mac >> ( i*8 )) & 0xFF;
		mac4[i] = (r->out_src_mac >> ( i*8 )) & 0xFF;
	}

	fprintf(stream,
"  in src mac   = %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n"
"  out dst mac  = %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n"
"  in dst mac   = %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n"
"  out src mac  = %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n"
, mac1[5], mac1[4], mac1[3], mac1[2], mac1[1], mac1[0]
, mac2[5], mac2[4], mac2[3], mac2[2], mac2[1], mac2[0]
, mac3[5], mac3[4], mac3[3], mac3[2], mac3[1], mac3[0]
, mac4[5], mac4[4], mac4[3], mac4[2], mac4[1], mac4[0]);

} // End of stringsEXmacAddr

static void stringsEXasAdjacent(FILE *stream, master_record_t *r) {

	fprintf(stream,
"  bgp next as  =             %5u\n"
"  bgp prev as  =             %5u\n"
, r->bgpNextAdjacentAS, r->bgpPrevAdjacentAS);

} // End of stringsEXasAdjacent

static void stringsEXlatency(FILE *stream, master_record_t *r) {
double f1, f2, f3;

	f1 = (double)r->client_nw_delay_usec / 1000.0;
	f2 = (double)r->server_nw_delay_usec / 1000.0;
	f3 = (double)r->appl_latency_usec / 1000.0;

	fprintf(stream,
"  cli latency  =         %9.3f ms\n"
"  srv latency  =         %9.3f ms\n"
"  app latency  =         %9.3f ms\n"
, f1, f2, f3);

} // End of stringsEXlatency

#ifdef NSEL
static void stringsEXnselCommon(FILE *stream, master_record_t *r) {
char datestr[64];

	time_t when = r->msecEvent / 1000LL;
	if ( when == 0 ) {
		strncpy(datestr, "<unknown>", 63);
	} else {
		struct tm *ts = localtime(&when);
		strftime(datestr, 63, "%Y-%m-%d %H:%M:%S", ts);
	}
	fprintf(stream,
"  connect ID   =        %10u\n"
"  fw event     =             %5u: %s\n"
"  fw ext event =             %5u: %s\n"
"  Event time   =     %13llu [%s.%03llu]\n"
, r->connID, r->event, r->event_flag == FW_EVENT ? FwEventString(r->event) : EventString(r->event)
, r->fwXevent, EventXString(r->fwXevent)
, (long long unsigned)r->msecEvent, datestr
, (long long unsigned)(r->msecEvent % 1000L));

} // End of stringsEXnselCommon

static void stringsEXnselXlateIPv4(FILE *stream, master_record_t *r) {
char as[IP_STRING_LEN], ds[IP_STRING_LEN];

	uint32_t src = htonl(r->xlate_src_ip.V4);
	uint32_t dst = htonl(r->xlate_dst_ip.V4);
	inet_ntop(AF_INET, &src, as, sizeof(as));
	inet_ntop(AF_INET, &dst, ds, sizeof(ds));

	fprintf(stream,
"  src xlt ip   =  %16s\n"
"  dst xlt ip   =  %16s\n"
	, as,ds);

} // End of stringsEXnselXlateIPv4

static void stringsEXnselXlateIPv6(FILE *stream, master_record_t *r) {
char as[IP_STRING_LEN], ds[IP_STRING_LEN];
uint64_t src[2];
uint64_t dst[2];

	src[0] = htonll(r->xlate_src_ip.V6[0]);
	src[1] = htonll(r->xlate_src_ip.V6[1]);
	dst[0] = htonll(r->xlate_dst_ip.V6[0]);
	dst[1] = htonll(r->xlate_dst_ip.V6[1]);
	inet_ntop(AF_INET6, &src, as, sizeof(as));
	inet_ntop(AF_INET6, &dst, ds, sizeof(ds));

	fprintf(stream,
"  src xlt ip   =  %16s\n"
"  dst xlt ip   =  %16s\n"
	, as,ds);

} // End of stringsEXnselXlateIPv4

static void stringsEXnselXlatePort(FILE *stream, master_record_t *r) {
	fprintf(stream,
"  src xlt port =             %5u\n"
"  dst xlt port =             %5u\n"
, r->xlate_src_port, r->xlate_dst_port );

} // End of stringsEXnselXlatePort

static void stringsEXnselAcl(FILE *stream, master_record_t *r) {
	fprintf(stream,
"  Ingress ACL  =       0x%x/0x%x/0x%x\n"
"  Egress ACL   =       0x%x/0x%x/0x%x\n"
, r->ingressAcl[0], r->ingressAcl[1], r->ingressAcl[2], 
  r->egressAcl[0], r->egressAcl[1], r->egressAcl[2]);

} // End of stringsEXnselAcl

static void stringsEXnselUserID(FILE *stream, master_record_t *r) {
	fprintf(stream,
"  username     =       %s\n"
, r->username);

} // End of stringsEXnselUserID

static void stringsEXnelCommon(FILE *stream, master_record_t *r) {
char datestr[64];

	time_t when = r->msecEvent / 1000LL;
	if ( when == 0 ) {
		strncpy(datestr, "<unknown>", 63);
	} else {
		struct tm *ts = localtime(&when);
		strftime(datestr, 63, "%Y-%m-%d %H:%M:%S", ts);
	}
	fprintf(stream,
"  nat event    =             %5u: %s\n"
"  Event time   =     %13llu [%s.%03llu]\n"
"  ingress VRF  =        %10u\n"
"  egress VRF   =        %10u\n"
, r->event, r->event_flag == FW_EVENT ? FwEventString(r->event) : EventString(r->event)
, (long long unsigned)r->msecEvent, datestr , (long long unsigned)(r->msecEvent % 1000L)
, r->ingressVrf, r->egressVrf);

} // End of stringsEXnelCommon

static void stringsEXnelXlatePort(FILE *stream, master_record_t *r) {

	fprintf(stream,
"  pblock start =             %5u\n"
"  pblock end   =             %5u\n"
"  pblock step  =             %5u\n"
"  pblock size  =             %5u\n"
, r->block_start, r->block_end, r->block_step, r->block_size );

} // End of stringsEXnelXlatePort

#endif
static void stringsEXnbarApp(FILE *stream, master_record_t *r) {
union {
        uint8_t     val8[4];
        uint32_t    val32;
}conv;

	char *name = GetNbarInfo(r->nbarAppID, 4);
	if ( name == NULL) {
		printf("No nbar app name\n");
		name = "<no info>";
	} else {
		printf("Found nbar app name\n");
	}

	conv.val8[0] = 0;
	conv.val8[1] = r->nbarAppID[1];
	conv.val8[2] = r->nbarAppID[2];
	conv.val8[3] = r->nbarAppID[3];

	fprintf(stream,
"  app ID       =             %2u..%u: %s\n"
, r->nbarAppID[0], ntohl(conv.val32), name);

} // End of stringsEXnbarAppID

static void stringsEXinPayload(FILE *stream, master_record_t *r) {

	fprintf(stream,
"  in payload   =        %10u\n"
, r->inPayloadLength);
	DumpHex(stream, r->inPayload, r->inPayloadLength > 512 ? 512 : r->inPayloadLength);
	if ( r->srcPort == 53 ||  r->dstPort == 53) {
		content_decode_dns((uint8_t *)r->inPayload, r->inPayloadLength);
	}
} // End of stringsEXinPayload

static void stringsEXoutPayload(FILE *stream, master_record_t *r) {

	fprintf(stream,
"  out payload  =        %10u\n"
, r->outPayloadLength);
	DumpHex(stream, r->outPayload, r->outPayloadLength > 512 ? 512 : r->outPayloadLength);
	if ( r->srcPort == 53 ||  r->dstPort == 53) {
		content_decode_dns((uint8_t *)r->outPayload, r->outPayloadLength);
	}

} // End of stringsEXoutPayload

static void stringsEXdnsInfo(FILE *stream, master_record_t *r) {
/*
	char *rrCode = "undef";
	switch (r->RRsection) {
		case 0:
			rrCode = "query section";
			break;
		case 1:
			rrCode = "answer section";
			break;
		case 2:
			rrCode = "name server section";
			break;
		case 3:
			rrCode = "additional section";
			break;
	}
	fprintf(stream,
"  dns type     =               %3u %s\n"
"  dns query    =        %10u\n"
"  dns resouce  =               %3u %s\n"
"  dns auth     =        %10u\n"
"  dns ID       =        %10u\n"
"  dns TTL      =        %10u\n"
"  dns Qname    =               %3u %s\n"
, r->QueryResponse, r->QueryResponse == 0 ? "query" : "response", r->QueryType
, r->RRsection, rrCode, r->Authoritative, r->ID, r->TTL
, r->QnameLength, r->QnameLength ? r->Qname : "<empty>");
*/
} // End of stringsEXdnsInfo


void raw_prolog(void) {
	recordCount = 0;
} // End of pipe_prolog

void raw_epilog(void) {
	// empty
} // End of pipe_epilog

void flow_record_to_raw(FILE *stream, void *record, int tag) {
master_record_t *r = (master_record_t *)record;
char elementString[MAXELEMENTS * 5];

	elementString[0] = '\0';
	for (int i=0; i<r->numElements; i++) {
		snprintf(elementString + strlen(elementString), sizeof(elementString) - strlen(elementString), "%u ", r->exElementList[i]);
	}

	char *type;
	char version[8];
	if ( TestFlag(r->flags, V3_FLAG_EVENT)) {
		type = "EVENT";
		version[0] = '\0';
	} else {
		if ( r->nfversion != 0 ) {
			snprintf(version, 8, " v%u", r->nfversion & 0x0F);
			if ( r->nfversion & 0x80 ) {
				type = "SFLOW";
			} else if ( r->nfversion & 0x40 ) {
				type = "PCAP";
			} else {
				type = "NETFLOW";
			}
		} else {
			// compat with previous versions
			type = "FLOW";
			version[0] = '\0';
		}
	}

	fprintf(stream, "\n"
"Flow Record: \n"
"  Flags        =              0x%.2x %s%s%s, %s\n"
"  Elements     =             %5u: %s\n"
"  size         =             %5u\n"
"  engine type  =             %5u\n"
"  engine ID    =             %5u\n"
"  export sysid =             %5u\n"
,	r->flags, type, version,
	TestFlag(r->flags, V3_FLAG_ANON) ? " Anonymized" : "", 
	TestFlag(r->flags, V3_FLAG_SAMPLED) ? "Sampled" : "Unsampled", 
	r->numElements, elementString, r->size, r->engine_type, r->engine_id, r->exporter_sysid);

	if ( r->label ) {
	fprintf(stream, 
"  Label        =  %16s\n"
, r->label);
	}

	int i = 0;
	while (r->exElementList[i]) {
		switch (r->exElementList[i]) {
			case EXnull:
				fprintf(stderr, "Found unexpected NULL extension \n");
				break;
			case EXgenericFlowID: 
				stringEXgenericFlow(stream, r);
				break;
			case EXipv4FlowID:
				stringsEXipv4Flow(stream, r);
				break;
			case EXipv6FlowID:
				stringsEXipv6Flow(stream, r);
				break;
			case EXflowMiscID:
				stringsEXflowMisc(stream, r);
				break;
			case EXcntFlowID:
				stringsEXcntFlow(stream, r);
				break;
			case EXvLanID:
				stringsEXvLan(stream, r);
				break;
			case EXasRoutingID:
				stringsEXasRouting(stream, r);
				break;
			case EXbgpNextHopV4ID:
				stringsEXbgpNextHopV4(stream, r);
				break;
			case EXbgpNextHopV6ID:
				stringsEXbgpNextHopV6(stream, r);
				break;
			case EXipNextHopV4ID:
				stringsEXipNextHopV4(stream, r);
				break;
			case EXipNextHopV6ID:
				stringsEXipNextHopV6(stream, r);
				break;
			case EXipReceivedV4ID:
				stringsEXipReceivedV4(stream, r);
				break;
			case EXipReceivedV6ID:
				stringsEXipReceivedV6(stream, r);
				break;
			case EXmplsLabelID:
				stringsEXmplsLabel(stream, r);
				break;
			case EXmacAddrID:
				stringsEXmacAddr(stream, r);
				break;
			case EXasAdjacentID:
				stringsEXasAdjacent(stream, r);
				break;
			case EXlatencyID:
				stringsEXlatency(stream, r);
				break;
#ifdef NSEL
			case EXnselCommonID:
				stringsEXnselCommon(stream, r);
				break;
			case EXnselXlateIPv4ID:
				stringsEXnselXlateIPv4(stream, r);
				break;
			case EXnselXlateIPv6ID:
				stringsEXnselXlateIPv6(stream, r);
				break;
			case EXnselXlatePortID:
				stringsEXnselXlatePort(stream, r);
				break;
			case EXnselAclID:
				stringsEXnselAcl(stream, r);
				break;
			case EXnselUserID:
				stringsEXnselUserID(stream, r);
				break;
			case EXnelCommonID:
				stringsEXnelCommon(stream, r);
				break;
			case EXnelXlatePortID:
				stringsEXnelXlatePort(stream, r);
				break;
#endif
			case EXnbarAppID:
				stringsEXnbarApp(stream, r);
				break;
			case EXinPayloadID:
				stringsEXinPayload(stream, r);
				break;
			case EXoutPayloadID:
				stringsEXoutPayload(stream, r);
				break;
			default:
				dbg_printf("Extension %i not yet implemented\n", r->exElementList[i]);
		}
		i++;
	}

} // flow_record_to_raw


