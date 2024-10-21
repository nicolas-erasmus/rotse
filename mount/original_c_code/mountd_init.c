/* mountd_init ===============================================================

 * Purpose:    Function to initialize the mountd process.  Hides lots of
 *    gritty details so main procedure is clean.
 *
 * Inputs:
 *     argc    int                    number command-line tokens
 *     argv    char **                array of command-line tokens (see 'main')
 *
 * Outputs:
 *     ipcrec  struct ipc_record *    shared memory and semaphores
 *     sigs    struct rotse_sigset *  signal specifications
 *     cfg     struct mountd_cfg *    config. parameters
 * 
 * Updated: 1997-05-05  Bob Kehoe  --  (from /rotse/skeld/skeld_init.c)
 * Updated: 1998-04-15  Bob Kehoe -- remove config. globals to epochd_cfg
 *                                   struct argument.i
 * Updated: 1998-07-10 Bob Kehoe -- rename epochd to mountd
 * Updated: 1999-08-25 Rick Balsano -- added start of OCAAS's telescoped
 * Updated: 1999-09-14 Rick Balsano -- moved itimer setup after nanosleep()
 * Updated: 1999-09-21 Rick Balsano -- improved start of telescoped
 * Updated: 2002-04-02 E. Rykoff -- works with schier mount. (old mods, new comment!)
 * Updated: 2005-04-10 E. Rykoff -- added baudrate toggle
 ======================================================================== */

/* system includes */

#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <sched.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

/* rotse includes */

#include <rotse.h>
#include <util.h>
#include <io_ctrl.h>
#include <mountd_str.h>
#include "schierd.h"
#include "protos.h"

extern int mtfd;
extern int focfd;

void toggle_baudrate(int fd);

int mountd_init(int argc, char **argv, struct ipc_record *ipcrec,
		struct rotse_sigset *sigs, struct mountd_cfg *cfg)
{
   int sched_policy;		/* posix.1b sched stuff */
   struct sched_param sched_par;
   struct itimerval lwait;	/* polling time */

   extern int loglevel;

   struct termios tio;

   /* temporary variables */
   time_t curtime;

   /* We will use syslog for major errors or for posting
    * important info in addition to the regular log channel. */

   openlog(LOG_ID, LOG_NDELAY, LOG_ROTSE);
   syslog(LOG_ERR, "starting up mountd");

   mtfd = focfd = -1;

   /* read config file */

   if (mountd_conf(argc, argv, cfg, 0) == -1) {
      syslog(LOG_ERR, "mountd_conf failed");
      exit(1);
   }
   /* Become a daemon. Also sets process group id same as rotsed.
    * This must occur before logging starts because we close all
    * open files during the call. */

   if (daemon_init(cfg->pgid) == -1) {
      syslog(LOG_ERR, "%s: %m", "daemon_init failed");
      exit(1);
   }
   /* Start up logging. */

   if ((cfg->logfd = open(cfg->logfile, LOGFLG, LOGMOD)) == -1) {
      syslog(LOG_ERR, "could not open %s: %m", cfg->logfile);
      exit(1);
   }
   curtime = time(NULL);
   loglevel = cfg->loglevel;	/* set-up logging */
   rlog(TERSE, cfg->logfd, "starting schierd: %s\n", ctime(&curtime));
   syslog(LOG_INFO, "starting schierd with lfile %s, confile %s",
	  cfg->logfile, cfg->confile);

   /* lock the process into memory */

   if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
      syslog(LOG_ERR, "mlockall failure: %m");
      rlog(TERSE, cfg->logfd, "error: mem locking failed\n");
      return (-1);
   }
   /* set up our shared memory segment */

   if ((ipcrec->shmid =
	shmget(cfg->ipc_key, sizeof(struct mountd_shm), 0600)) == -1) {
      syslog(LOG_ERR, "shmget failure: %m");
      return (-1);
   }
   if ((int) (ipcrec->shm = shmat(ipcrec->shmid, 0, 0)) == -1) {
      syslog(LOG_ERR, "shmat failure: %m");
      return (-1);
   }
   if (shmctl(ipcrec->shmid, IPC_STAT, &ipcrec->shmds) == -1) {
      syslog(LOG_ERR, "shmctl failure: %m");
      return (-1);
   }
   rlog(DEBUG, cfg->logfd, "mountd shm attached at %s\n",
	ctime(&ipcrec->shmds.shm_atime));

   /* set up our semaphore */

   if ((ipcrec->semid = semget(cfg->ipc_key, 1, 0600)) == -1) {
      syslog(LOG_ERR, "semget failure: %m");
      return (-1);
   }
   /* set up the POSIX.1b scheduling algorithm */

   sched_policy = SCHED_POLICY;
   sched_par.sched_priority = SCHED_PRIORITY;
   if (sched_setscheduler((int) getpid(), sched_policy, &sched_par) == -1) {
      syslog(LOG_ERR, "setscheduler failure: %m");
      rlog(TERSE, cfg->logfd, "error: setscheduler failed\n");
      return (-1);
   }
   /* define our standard signal masks */

   //if (signal_setup_schierd(sigs) == -1) {
   if (signal_setup(sigs, 1) == -1) {
      syslog(LOG_ERR, "signal_setup failed: %m");
      return (-1);
   }
 
   /* install our "normal" signal mask */

   if (sigprocmask(SIG_SETMASK, &sigs->norm, NULL) == -1) {
      syslog(LOG_ERR, "sigprocmask failed: %m");
      return (-1);
   }

   /* Open the io device, set output bits to zero,
    * and verify that we can read the
    * state of the open/closed switches. */

   syslog(LOG_INFO,"setting up serial");
   /* Set up mount serial port */
   if ((mtfd = open(MOUNT_DEV, O_RDWR | O_NOCTTY)) < 0) {
     syslog(LOG_ERR, "Error opening serial port %s, %m",MOUNT_DEV);
     return(-1);
   }
   syslog(LOG_INFO,"mount_dev opened");
   
   bzero(&tio, sizeof(tio));
   
   tio.c_cflag = MOUNT_BAUDRATE | CS8 | CLOCAL | CREAD | CRTSCTS;

   cfsetispeed(&tio, MOUNT_BAUDRATE);
   cfsetospeed(&tio, MOUNT_BAUDRATE);
   
   tio.c_iflag = IGNBRK;
   tio.c_oflag = 0;
   tio.c_lflag = 0;
   tio.c_cc[VTIME] = 0;
   tio.c_cc[VMIN] = 1;
   tio.c_cc[VEOF] = 4;


   tcflush(mtfd, TCIFLUSH);
   tcsetattr(mtfd, TCSANOW, &tio);

   /* flush again */
   tcflush(mtfd, TCIOFLUSH);

   /* toggle the baudrate */
   toggle_baudrate(mtfd);

   /* Set up focus serial port */
   if ((focfd = open(FOCUS_DEV, O_RDWR | O_NOCTTY)) < 0) {
     syslog(LOG_ERR,"Error opening serial port. %m\n");
     return(-1);
   }
   syslog(LOG_INFO,"focusdev opened");
   bzero(&tio,sizeof(tio));

   tio.c_cflag = FOCUS_BAUDRATE | CS8 | CLOCAL | CREAD | CRTSCTS;
   cfsetispeed(&tio, FOCUS_BAUDRATE);
   cfsetospeed(&tio, FOCUS_BAUDRATE);

   tio.c_iflag = IGNBRK;
   tio.c_oflag = NL0 | CR0 | TAB0 | BS0 | VT0 | FF0;
   tio.c_oflag |= OPOST | ONLCR;
   tio.c_lflag = 0;
   tio.c_cc[VTIME] = 5;
   tio.c_cc[VMIN] = 1;

   tcflush(focfd, TCIFLUSH);
   tcsetattr(focfd, TCSANOW, &tio);
   tcflush(focfd, TCIOFLUSH);

   /* toggle the baudrate */
   toggle_baudrate(focfd);

   /* Start up periodic timer.
    * This arranges for SIGALRM to be sent to this process
    * periodically.  We will block the signal except when
    * we reach the bottom of the main loop, where we wait
    * until we receive the next SIGALRM. */

   lwait.it_interval.tv_sec = (int) cfg->poll_time;	/* from config file */
   lwait.it_interval.tv_usec =
       (int) ((cfg->poll_time - (int) cfg->poll_time) * 1000000.0);
   lwait.it_value.tv_sec = (int) cfg->poll_time;
   lwait.it_value.tv_usec = (int) ((cfg->poll_time -
				    (int) cfg->poll_time) * 1000000.0);
   if (setitimer(ITIMER_REAL, &lwait, NULL) == -1) {
      syslog(LOG_ERR, "setitimer failed: %m");
      return (-1);
   }
   syslog(LOG_INFO,"done");

   return (0);
}

void toggle_baudrate(int fd)
{
  /* this function is adapted from the minicom function m_dtrtoggle. */
  /* On intialization the baudrate is dropped to 0 and back to regular */
  /* speed.  I think this might be a key step in clearing a clogged serial */
  /* line, which minicom seems to do better than schierd! */

  struct termios tty, old;

  tcgetattr(fd, &tty);
  tcgetattr(fd, &old);
  cfsetospeed(&tty, B0);
  cfsetispeed(&tty, B0);
  tcsetattr(fd, TCSANOW, &tty);
  sleep(1);
  tcsetattr(fd, TCSANOW, &old);
}
