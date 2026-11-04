#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/netstack.h"

static void be16(unsigned char *p, unsigned v) { p[0] = v >> 8; p[1] = v; }
static unsigned sum(const unsigned char *p, size_t n)
{
 unsigned s = 0;
 for (size_t i = 0; i < n; i += 2) s += (unsigned)p[i] * 256 + p[i+1];
 while (s >> 16) s = (s & 65535) + (s >> 16);
 return (~s) & 65535;
}
static enum netstack_event receive(struct netstack *s, unsigned char *f, size_t n,
 struct netstack_udp_view *view)
{
 size_t out;
 unsigned char reply[NETSTACK_MAX_FRAME];
 return netstack_process_frame(s, f, n, 0, reply, sizeof(reply), &out, view);
}
int main(void)
{
 struct netstack_config local = {.mac={2,0,0,0,0,1}, .address={10,15,3,1},
  .netmask={255,255,255,0}, .gateway={10,15,3,2}};
 struct netstack_config peer = {.mac={2,0,0,0,0,2}, .address={10,15,3,2},
  .netmask={255,255,255,0}};
 struct netstack s, sender;
 unsigned char f[NETSTACK_MAX_FRAME], payload[512] = {0}, hop[6];
 struct netstack_udp_view view;
 size_t n;
 assert(netstack_init(&s, &local)); assert(netstack_init(&sender, &peer));
 assert(netstack_build_udp_frame(&sender,local.mac,local.address,1234,4321,
  payload,sizeof(payload),f,sizeof(f),&n));
 assert(receive(&s,f,n,&view)==NETSTACK_UDP_DATAGRAM);
 assert(view.payload_length==512);
 /* A valid IPv4/UDP packet may exceed the queue's 512-byte storage. No UDP
  * checksum is required in IPv4, so this exercises the size limit directly. */
 for (size_t length=513; length<=1472; length++) {
  memset(f+42, 0x41, length);
  be16(f+16, 28+length); be16(f+38, 8+length); be16(f+40,0);
  be16(f+24,0); be16(f+24,sum(f+14,20));
  assert(receive(&s,f,42+length,&view)==NETSTACK_IGNORED);
 }
 /* Truncation must never expose transport pointers from an earlier packet. */
 for (size_t length=0; length<42; length++) {
  assert(receive(&s,f,length,&view)==NETSTACK_IGNORED);
  assert(view.payload==NULL);
 }
 const unsigned char invalid[][4]={{10,15,3,255},{10,15,3,0},{127,0,0,1},{0,1,2,3}};
 for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
  assert(!netstack_route_ipv4(&s,invalid[i],hop));
 const unsigned char external[4]={8,8,8,8};
 assert(netstack_route_ipv4(&s,external,hop)); assert(!memcmp(hop,local.gateway,4));
 /* A subnet broadcast address must not become a neighbour cache entry. */
 assert(netstack_build_arp_request(&sender,local.address,f,sizeof(f),&n));
 f[31]=255;
 assert(receive(&s,f,n,&view)==NETSTACK_IGNORED);
 assert(!netstack_arp_lookup(&s,invalid[0],0,hop));
 puts("netstack security regression checks passed");
}
