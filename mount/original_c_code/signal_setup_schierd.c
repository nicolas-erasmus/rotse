/* signal_setup_schierd ===============================
 *
 * Purpose: based on real signal_setup utility function, except it allows
 *          the SIG_HUP to get through to reset the schedule.
 * "Created":  2002-04-02: E. Rykoff
 *======================================================*/

/*
 * Our signal handler and signal set mainipulation routines are in here.
 * Created: 1997-03-18  --    Stuart Marshall
 */
#include <signal.h>
#include <syslog.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * rotse includes
 */
#include <rotse.h>

void sig_handler(int);
/*
 * Set up which signals to ignore, block, etc.
 * the arguments are signal sets used for blocking
 * &sigs->wait is used while waiting for itimer at bottom of main loop.
 * &sigs->norm is used almost everywhere.
 * &sigs->ipc is used during semaphore/shared memory access.
 */
int signal_setup_schierd(struct rotse_sigset *sigs)
{
   struct sigaction sa;
   /*
    * These are signals we want to respond to at bottom of main loop.
    */
   if (sigfillset(&sigs->wait) == -1)
      return (-1);
   if (sigdelset(&sigs->wait, SIGILL) == -1 ||
       sigdelset(&sigs->wait, SIGFPE) == -1 ||
       sigdelset(&sigs->wait, SIGSEGV) == -1 ||
       sigdelset(&sigs->wait, SIGTERM) == -1 ||
       sigdelset(&sigs->wait, SIGALRM) == -1 ||
       sigdelset(&sigs->wait, SIGHUP) == -1 ||
       sigdelset(&sigs->wait, SIG_ROTSE) == -1)
      return (-1);
   /*
    * These are signals we will respond to normally.
    */
   if (sigfillset(&sigs->norm) == -1)
      return (-1);
   if (sigdelset(&sigs->norm, SIGILL) == -1 ||
       sigdelset(&sigs->norm, SIGFPE) == -1 ||
       sigdelset(&sigs->norm, SIGSEGV) == -1 ||
       sigdelset(&sigs->norm, SIGTERM) == -1 ||
       sigdelset(&sigs->norm, SIGHUP) == -1 ||
       sigdelset(&sigs->norm, SIG_ROTSE) == -1)
      return (-1);
   /*
    * We block all signals during ipc access.
    */
   if (sigfillset(&sigs->ipc) == -1)
      return (-1);
   /*
    * Next register the name of our signal handler.
    */
   sa.sa_handler = sig_handler;
   /*
    * Block all sigs while in signal handler.
    */
   if (sigfillset(&sa.sa_mask) == -1)	/* block additional sigs in handler */
      return (-1);

   sa.sa_flags = 0;
   /*
    * Register which signals we want to handle.
    */
   if (sigaction(SIGTERM, &sa, NULL) ||
       sigaction(SIGALRM, &sa, NULL) ||
       sigaction(SIGILL, &sa, NULL) ||
       sigaction(SIGFPE, &sa, NULL) ||
       sigaction(SIGSEGV, &sa, NULL) ||
       sigaction(SIGHUP, &sa, NULL) ||
       sigaction(SIG_ROTSE, &sa, NULL)) {
      return (-1);
   }
   return (0);
}
