The networking code moves packets between network devices and protocol endpoints.
The current IPv4 profile is statically configured and supports Ethernet, ARP,
ICMP Echo, and UDP. On-link destinations use direct ARP; off-link unicast
destinations use the configured default gateway. The userland resolver sends
bounded IPv4 DNS A queries through UDP and reads its server from
`/etc/resolv.conf`. IPv4 options and fragments, DHCP, DNS-over-TCP, and
production TCP are not supported yet.
