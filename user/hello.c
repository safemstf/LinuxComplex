/* user/hello.c - Hello World
 *
 * The simplest user program - just prints a message and exits.
 */

#include "start.h"

void main(void)
{
    write("\n");
    write("Hello from user space!\n");
    write("Getting PID...\n");

    int pid = getpid();

    write("Got PID, printing digit...\n");

    /* Print single digit (avoids division) */
    char digit = '0' + (pid & 0xF);  /* Just show lower nibble as hex digit */
    char buf[2] = {digit, '\0'};
    write("PID (low nibble): ");
    write(buf);
    write("\n");

    write("Program completed successfully!\n");
}
