/* mountd_sigs ==============================================================

 * Purpose:    Determine what action to take based upon the signal received.
 *
 * Inputs:
 *     signum       int              signal to act on
 *
 * Outputs:
 *
 * Updated: 1997-05-05  Bob Kehoe  --  (from /rotse/skeld/skeld_sigs.c)
 * Updated: 1998-07-10 Bob Kehoe -- rename epochd to mountd
 ======================================================================== */

#include <signal.h>
#include <syslog.h>
#include <setjmp.h>

/* rotse includes */

#include <rotse.h>
#include <mountd_str.h>
#include "protos.h"

extern sigjmp_buf jmpbuf;

void sig_handler(int signum)
{
   if (signum == SIG_ROTSE) {

      /* daemon bad */

      siglongjmp(jmpbuf, signum);
      syslog(LOG_INFO, "got SIG_ROTSE: %d", signum);
   } else if (signum == SIGHUP) {
     syslog(LOG_INFO,"got SIG_HUP: %d", signum);
     siglongjmp(jmpbuf, signum);
   } else if (signum == SIGALRM) {
      ;
   } else {
      syslog(LOG_INFO, "got SIG: %d, calling mountd_shutdown", signum);
      mountd_shutdown(1);
   }
}
