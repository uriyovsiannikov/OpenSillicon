#include "core/net.h"

_Static_assert(CYCLONE_TCP_MAJOR_VERSION == 2,
	"unexpected CycloneTCP major version");

int
opensillicon_cyclone_config_check(void)
{
	return NET_RTOS_SUPPORT == DISABLED && IPV4_SUPPORT == ENABLED &&
	    TCP_SUPPORT == ENABLED && UDP_SUPPORT == ENABLED;
}
