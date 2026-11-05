#include <stdlib.h>
int nano_main(int argc, char **argv);
int main(int argc, char **argv)
{
    /* Exec currently supplies an empty environment on OpenSillicon. */
    if (getenv("TERM") == NULL && setenv("TERM", "xterm", 0) < 0)
        return 1;
    return nano_main(argc, argv);
}
