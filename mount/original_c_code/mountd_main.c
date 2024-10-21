/* Mount Daemon ======================================================== 

 * Purpose: Make the ROTSE mount do our bidding. 
 *
 * Inputs:
 *    argc  int      number of command-line tokens when process is run      
 *    argv  char **  array of strings giving command-line arguments. The
 *                   form of the command line is:
 *
 *                               mountd -f confile -g pgid -k ipc_key
 *
 *                   where 'confile' is the configuration file path, 'pgid' is
 *                   the process group ID, and 'ipc_key' is the identifier for
 *                   the shared memory.  (These are stored in the mountd_cfg
 *                   structure.)
 * 
 * Outputs: NONE
 *
 * Updated: 2001-04-12  Don Smith  --  (from older mountd_main.c)
 * Updated: 2002-04-02  E. Rykoff -- many bug fixes; parallel focus and move;
 *                                   improved error handling, etc.
 * Updated: 2002-09-24  E. Rykoff -- many improvements to error_recover, so
 *                                    it should now work reasonably well.
 * Updated: 2004-07-06  E. Rykoff -- added sigsuspend checking
 * Updated: 2004-09-13  E. Rykoff -- added update_focus calling and 
 *                                    OFFSET_FOCUS mode
 * Updated: 2004-09-28  E. Rykoff -- added ptg_offset reset
 * Updated: 2004-10-13  E. Rykoff -- modified error_recover to quickly recover
 * Updated: 2005-10-06  E. Rykoff -- added limit backing out; fixed mount
 *                                    error logging; fixed homing recovery
 ========================================================================= */

/* system includes */

//#include <syslog.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/times.h>
#include <errno.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stddef.h>
#include <setjmp.h>
#include <termios.h>
#include <values.h>

/* rotse includes */

#include <rotse.h>
#include <util.h>
#include <io_ctrl.h>
#include <mountd_str.h>
#include "schierd.h"
#include "protos.h"

#define SLOW (int) 20
#define MAXCOM 30
#define MAXMAIL 800
#define NROTSE_TOUT 3

/* Globals: */

int loglevel;			/* for rlog.c */
int iofd;			/* file desc. for io box */
sigjmp_buf jmpbuf;		/* for signal handler */
int estab;                      /* flag to determine if mount is initialized */
int mtfd;                       /* for mount serial port */
int focfd;                      /* for focus serial port */
int gstow[2];                   /* Mount stow position */
int gstow_vel[2];               /* mount stow velocity */
int encoder_tol = 0;                /* encoder tolerance for quitting */
int limit_status[2];            /* axes limit stati [global for now] */

/* other */
int init_done;

/* ---------------------------------------------------------------- */
/*                               main                               */
/* ---------------------------------------------------------------- */

int main(int argc, char **argv)
{
   struct ipc_record ipcrec;	/* control struct for ipc stuff */
   struct mountd_shm *shm;	/* pointer to shared memory */
   struct mountd_st *stp;	/* pointer to status portion */
   struct mountd_cd *cdp;	/* pointer to cmd portion */
   struct rotse_sigset sigs;	/* standard rotse signal masks */
   struct mountd_st status;
   struct mountd_cd incomm, last_in;
   float ftmp, ftmp_offset, ftmp_focoffset;
   int uo;
   int nshm, ijmp, bogus, nstack, reset=0, i, mntq, sendstat, terror, oldstate;
   time_t curtime;
   static int setreqlow;
   sigset_t oldmask;
   struct timeval qtime, offsettime, focoffsettime;
   struct mountd_par comstack[MAXCOM]; /* An array of commands */
   struct mountd_par command;          /* A single command */
   struct mountd_par track;            /* A single command to track */
   struct mountd_par focuscmd;         /* A single command to focus */
   struct mountd_par setzeros;         /* A single command to set the zeros */
   struct mountd_par setfoczeros;

   int last_movebit = 0;
   static int got_alert_move = 0;
   int zalarm = 0;

   int freset = 0;

   int ishm = 0;
   int retval;

   bzero(&last_in, sizeof(struct mountd_cd));
   bzero(&command, sizeof(struct mountd_par));
   bzero(&track, sizeof(struct mountd_par));
   bzero(&setzeros, sizeof(struct mountd_par));
   bzero(&setfoczeros, sizeof(struct mountd_par));

   
   setzeros.move_mode = MOUNT_ZEROS;
   setfoczeros.move_mode = FOCUS_ZEROS;
   estab = 0;
   limit_status[0] = limit_status[1] = 0;
   
   /*   DAEMON INITIALIZATION   */
   memset(&status, 0, sizeof(struct mountd_st));
   if (mountd_init(argc, argv, &ipcrec, &sigs, &status.conf) == -1) {
  //    syslog(LOG_ERR, "mountd_init failed");
      mountd_shutdown(1);
   }

   encoder_tol = status.conf.enctol;
   status.conf.zeropt[0] = status.conf.zeropt[1] = 0;
   status.conf.ptg_offset[0] = status.conf.ptg_offset[1] = 0;
   status.conf.zero_mjd = NANVAL;

   shm = ipcrec.shm;
   stp = ipcrec.shm + offsetof(struct mountd_shm, st);	/* status ptr */
   cdp = ipcrec.shm + offsetof(struct mountd_shm, cd);	/* command ptr */

   /* Initialize internal command stack */
   for (i=0; i<MAXCOM; i++) {    
     bzero(&(comstack[i]), sizeof(struct mountd_par));
     comstack[i].active = 0;
     comstack[i].move_mode = MOUNT_IDLE;
   }
   nstack = -1;

   /*actually, the only syncing should be done by astrod/rush!!! */
 
   command.move_mode = MOUNT_INIT;
   if (pushstack(&nstack, comstack, command) != 0) {
   //  syslog(LOG_ERR, "pushstack mount_init failed");
     mountd_shutdown(1);
   }

   status.state = 0;
   setreqlow = 0;

   setbits(status.state, bit(INIT));
   status.data.alarm_type = ALRM_OFF;
   if (gettimeofday(&status.tlast, NULL) != 0) {
   //   syslog(LOG_ERR, "gettimeofday failed");
      mountd_shutdown(1);
   }

   if (shmcpy(stp, &status, sizeof(struct mountd_st),
	      &shm->oreq, NOCHECK, SETHI,
	      ipcrec.semid, &sigs.ipc) == -1) {
   //   syslog(LOG_ERR, "shmcpy to failed");
      mountd_shutdown(1);
   }
   rlog(VERBOSE, status.conf.logfd, "Mount make = %s\n", status.conf.mntman);
   rlog(VERBOSE, status.conf.logfd, "Mount model = %s\n", status.conf.mntmodel);
   rlog(VERBOSE, status.conf.logfd, 
		   "Mount serial number = %d\n", status.conf.mntsn);
   rlog(TERSE, status.conf.logfd, "mountd_init finished\n");
   //syslog(LOG_INFO, "mountd_init finished");
   sigsuspend(&sigs.wait);

   /* SET JUMP POINT to return to after recieving SIG_ROTSE */
   
   if ((ijmp = sigsetjmp(jmpbuf, 1)) != 0) {
     if (ijmp == SIG_ROTSE) {
   //    syslog(LOG_INFO, "got SIG_ROTSE: %d", ijmp);
       rlog(TERSE, status.conf.logfd,
	    "resetting after receiving SIG_ROTSE: %s\n",
	    ctime((time_t *) & status.tlast.tv_sec));
       
       if (!got_alert_move) {   /* Have not heard from astrod yet */
	 rlog(TERSE, status.conf.logfd,
	      "Haven't heard from astrod yet\n");
	   
	 if (getbit(status.state, bit(MOVE)) != 0) {  /* Need to idle the mount */
	   
	   if (sigprocmask(SIG_SETMASK, &sigs.ipc, &oldmask) == -1) {
	//     syslog(LOG_INFO, "sigprocmask() failed after siglongjmp");
	     mountd_shutdown(1);
	   }
	   
	   for (i=0; i<MAXCOM; i++) {
	     bzero(&(comstack[i]), sizeof(struct mountd_par));
	     comstack[i].active = 0;
	     comstack[i].move_mode = MOUNT_IDLE;
	   }
	   nstack = -1;
	   
	   bzero(&command, sizeof(struct mountd_par));
	   command.move_mode = MOUNT_IDLE;
	   command.active = 1;
	   
	   if (pushstack(&nstack, comstack, command) != 0) {
	//     syslog(LOG_ERR, "pushstack failed");
	     mountd_shutdown(1);
	   }
	   if (mount_comm(command.move_mode, &mntq, &command, &status) != 0) {	     
	//     syslog(LOG_ERR, "mount_comm to stop mount failed");
	     mountd_shutdown(1);
	   }
	   
	   if (sigprocmask(SIG_SETMASK, &oldmask, NULL) == -1) {
	//     syslog(LOG_INFO, "sigprocmask() failed after ss__halt()");
	    mountd_shutdown(1);
	   }
	   rlog(TERSE, status.conf.logfd, "Mount idle finished.\n");
	   popstack(&nstack, comstack);
	   zerobits(status.state, bit(MOVE));
	   last_movebit = 0;
	   logtime(1,"Zeroing MOVE bit (after sig)--but not sent");
	   status.data.move_mode = MOUNT_IDLE; 
	   sendstat = 1;
	 }
       } 
       
       got_alert_move = (got_alert_move + 1) % 2;
       
       status.tlast.tv_sec = 0;
       sched_yield();
     } else if (ijmp == SIGHUP) {
       rlog(TERSE, status.conf.logfd, "\nResetting after receiving SIGHUP\n");
       if (mountd_conf(0, NULL, &(status.conf), 1) == -1) {
	// syslog(LOG_ERR,"mountd_conf failed on reread.");
	 mountd_shutdown(1);
       }
     //  syslog(LOG_INFO,"Done resetting configuration.");
       sched_yield();
     }
   }
   
   /* initialize time stamps */
   if (gettimeofday(&status.tlast, NULL) != 0) {
   //  syslog(LOG_ERR, "gettimeofday failed");
     mountd_shutdown(1);
   }
   if (gettimeofday(&qtime, NULL) != 0) {
   //  syslog(LOG_ERR, "gettimeofday failed");
     mountd_shutdown(1);
   }
   if (gettimeofday(&offsettime, NULL) != 0) {
   //  syslog(LOG_ERR, "gettimeofday failed");
     mountd_shutdown(1);
   }
   if (gettimeofday(&focoffsettime, NULL) != 0) {
   //  syslog(LOG_ERR, "gettimeofday failed");
     mountd_shutdown(1);
   }
   last_movebit = 0;  
   sendstat = 1;   /* what about sig? */

   /*   MAIN LOOP   */
   while (1) {
     if (timediff(&qtime, &ftmp) != 0) {
   //    syslog(LOG_ERR, "timediff failed,%m");
       mountd_shutdown(1);
     }
     if (timediff(&offsettime, &ftmp_offset) != 0) {
   //    syslog(LOG_ERR, "timediff failed,%m");
       mountd_shutdown(1);
     }
     if (timediff(&focoffsettime, &ftmp_focoffset) != 0) {
   //    syslog(LOG_ERR,"timediff failed, %m");
       mountd_shutdown(1);
     }

     if (sigprocmask(SIG_SETMASK, &sigs.ipc, &oldmask) == -1) {
   //    syslog(LOG_INFO, "sigprocmask() failed after siglongjmp");
       mountd_shutdown(1);
     }  

     /* If there is an inactive command at the top of the stack, activate it. */
     if (nstack >= 0) {
       sendstat = 1;
       switch (comstack[0].active) {
       case 1:
	 /* if 1, mount is still busy, don't report yet. */
	 sendstat = 0;
	 break;
       case 0:
	 if (mount_comm(comstack[0].move_mode, &bogus, &comstack[0], 
			//&status, &sigs.ipc) != 0) {
			&status) != 0) {
//	   syslog(LOG_ERR, "mount_comm failed");
	   mountd_shutdown(1);
	 }
	 comstack[0].active = 1;
	 if (comstack[0].move_mode == MOUNT_SYNC) estab = 0;

	 /* Tell everyone we're busy & what we plan to be doing */
	 status.data.move_mode = comstack[0].move_mode;
	 break;
       case -1:
	 /* if the top command is complete, delete it. */
	 sendstat = 1;
	 if (comstack[0].nozero == 0) {
	   zerobits(status.state, bit(MOVE));
	 }
	 if (comstack[0].move_mode == MOUNT_INIT) {
	   zerobits(status.state, bit(INIT));
//	   syslog(LOG_INFO,"Zeroing INIT bit");
	 }
	 if ((comstack[0].move_mode == MOUNT_ZEROS) || 
	     (comstack[0].move_mode == MOUNT_RUN)) {
	   zerobits(status.state, bit(ALARM));
	   status.data.alarm_type = ALRM_OFF;
//	   syslog(LOG_INFO,"Zeroing ALARM bit");
	 }
	 popstack(&nstack, comstack);
	 break;
       }
     } else {
       status.data.move_mode = MOUNT_IDLE;
     }

     if (ftmp > status.conf.poll_time) {
       if (mount_comm(MOUNT_QUERY, &mntq, &comstack[0], &status) != 0) {
		      // &sigs.ipc) != 0) {
//	 syslog(LOG_ERR, "query_mount failed,%m");
	 mountd_shutdown(1);
       }
       /* If we gave it a command and it's not moving and there's no error,
	  it must be complete. */
       if ((mntq == MOUNT_IDLE)&&(comstack[0].active == 1)&&
	   ((comstack[0].move_mode > MOUNT_IDLE)&&(comstack[0].move_mode < NMOUNT))) {
	 sendstat = 1;            /* Prepare to report change in status */
	 comstack[0].active = -1; /* tell stack management that task is completed */
	 if ((comstack[0].move_mode == MOUNT_SLEW) || (comstack[0].move_mode == MOUNT_SHIFT)) {
	   /* if a move was successful, and it was after an error reset, all is kosher */
	   if (reset > 0) reset--;
	 }
       }
       if (mntq == MOUNT_ERROR) {
	 //	 estab = 0;
//	 syslog(LOG_INFO,"MOUNT_ERROR: Running error_recover()");
	 setbits(status.state, bit(ALARM));
	 status.data.alarm_type = ALRM_MOUNT;  /* check this */
	 setbits(status.state, bit(MOVE));
	 sendstat = 1;
	 terror = error_recover(&reset, &nstack, comstack, &(status.conf), &zalarm);
	 if (terror == -1) {
//	   syslog(LOG_ERR, "unrecoverable error");
	   mountd_shutdown(1);
	 }       
	 if (zalarm) {  /* this is when it is a simple re-move */
	   zerobits(status.state, bit(ALARM));
	   status.data.alarm_type = 0;
	 }
       } else if (mntq == MOUNT_ERROR_SHUTDOWN) {
//	 syslog(LOG_INFO,"MOUNT_ERROR_SHUTDOWN: Shutting down.");
	 mountd_shutdown(1);
       }

       
       /* Check to see if focus motor is idle */
       if (mount_comm(FOCUS_QUERY, &mntq, &comstack[0], &status) != 0) {
		      //&sigs.ipc) != 0) {
//	 syslog(LOG_ERR, "query_focus failed. Running focus_recover()");
	 terror = focus_recover(&freset, &nstack, comstack, &(status.conf));
	 if (terror == -1) {
//	   syslog(LOG_ERR, "unrecoverable focus error");
	   mountd_shutdown(1);
	 }
	 
	 /*mountd_shutdown(1);*/
       }
       if ((mntq == MOUNT_IDLE)&&(comstack[0].active == 1)&&(comstack[0].move_mode > NMOUNT)) {
	 sendstat = 1;            /* Prepare to report change in status */
	 comstack[0].active = -1; /* tell stack management that task is completed */ 
       }
       if (gettimeofday(&qtime, NULL) != 0) {
//	 syslog(LOG_ERR, "gettimeofday failed");
	 mountd_shutdown(1);
       }
     }
       
     if ((getbit(status.state,bit(MOVE)) != 0) && (last_movebit == 0)) {
       /*logtime(1,"Sending MOVE bit to rotsed");*/
       last_movebit = 1;
     } else if ((getbit(status.state,bit(MOVE)) == 0) && (last_movebit == 1)) {
       logtime(1,"Zeroing MOVE bit for rotsed");
       last_movebit = 0;
     }

       /* copy mountd command structure from shared memory area */
     if ((nshm = shmcpy(&incomm, cdp, sizeof(struct mountd_cd),
			&shm->ireq, CHECKHI, NOSET,
			ipcrec.semid, &sigs.ipc)) == 0) {
       
       /* shm->ireq was low so nothing to do,
	* this is most likely case */
       
     } else if (nshm == sizeof(struct mountd_cd)) {

       /* First check if the command is a repeat */
       /*    if (memcmp(&last_in, &incomm, sizeof(struct mountd_cd)) != 0) {*/
       if (1) {
	 /* It is a new command */
	 memcpy(&last_in, &incomm, sizeof(struct mountd_cd));
	 /* If command makes mount busy, echo back status immediately for astrod */
	 if ((incomm.arg.move_mode == MOUNT_SYNC)||(incomm.arg.move_mode == MOUNT_SLEW)||
	     (incomm.arg.move_mode == MOUNT_STOW)||(incomm.arg.move_mode == MOUNT_PARK)||
	     (incomm.arg.move_mode == MOUNT_SHIFT)||(incomm.arg.move_mode == MOUNT_STANDBY)||
	     (incomm.arg.move_mode == FOCUS_MOVE)||(incomm.arg.move_mode == FOCUS_SYNC)||
	     (incomm.arg.move_mode == MOUNT_IDLE)) {
	   oldstate = status.state;
	   setbits(status.state, bit(MOVE));   /* FOCUS is for focusd, I think */
	   /* syslog(LOG_INFO,"Command In: Setting MOVE bit Hi");*/
	   logtime(1,"Command In: Setting MOVE bit Hi");
	   if ((retval = shmcpy(stp, &status, sizeof(struct mountd_st),
				//	&shm->oreq, NOCHECK, SETHI,
				&shm->oreq, CHECKLO, SETHI,
				ipcrec.semid, &sigs.ipc)) == -1) {
//	     syslog(LOG_ERR, "shmcpy to failed");
	     mountd_shutdown(1);
	   } else if (retval == 0) {
//	     syslog(LOG_INFO,"Warning: Couldn't set bit HI");
	   }
	   status.state = oldstate;
	 }
	 
	 /* received new command  */
	 curtime = time(NULL);
	 rlog(TERSE, status.conf.logfd,
	      "received new command at %s\n", ctime(&curtime));

	 /* Transfer command to internal stack */
	 
	 /* If the top command on the stack is tracking, delete that command 
	    so that the new command will be able to be activated.  */

	 if ((nstack >= 0) && 
	     ((comstack[0].move_mode == MOUNT_TRACK) || (comstack[0].move_mode == MOUNT_TRACK_RA))) {
	   popstack(&nstack, comstack);
	 }
	 /* If the top command is active, deactivate it so that it
	    will be executed later, and log an error. */
	 if (getbit(incomm.arg.mode, bit(ALERT_MOVE)) != 0) {
	   /* Override! */
	   /* Clear the stack...(can add in caveats later) */
	   /* should we put on an idle? -- mount is always stopped first */
//	   syslog(LOG_INFO,"**ALERT_MOVE received...");
	   got_alert_move = (got_alert_move + 1) % 2;
	   for (i=0; i<MAXCOM; i++) {
	     bzero(&(comstack[i]), sizeof(struct mountd_par));
	   }
	   nstack = -1;
	 } else {
	   if (got_alert_move) {
	     /* Assume weather issue */
	     got_alert_move = (got_alert_move + 1) % 2;
	   }
	 }

	 if ((nstack >= 0)&&(comstack[0].active == 1)) {
	   comstack[0].active = 0;
//	   syslog(LOG_ERR, "Command %d received while active.", incomm.arg.move_mode);
	 }
	 /* If the zero points are not set, or the command is a SYNC,
	    push a set zeros command onto the stack first. */
	 if ((status.conf.zeropt[0] == NOZERO)||
	     (incomm.arg.move_mode == MOUNT_SYNC)) {
	   incomm.arg.nozero = 1;
	   if (pushstack(&nstack, comstack, setzeros) != 0) {
//	     syslog(LOG_ERR, "pushstack setzeros failed");
	     mountd_shutdown(1);
	   }
	 }
	 if (incomm.arg.move_mode == FOCUS_SYNC) {
	   incomm.arg.nozero = 1;
	   if (pushstack(&nstack, comstack, setfoczeros) != 0) {
//	     syslog(LOG_ERR,"pushstack setfoczeros failed");
	     mountd_shutdown(1);
	   }
	 }

	 /* Push focus move on stack---and start focus move! */
	
	 /* if command needs tracking, add that first */
	 if (incomm.arg.move_mode == MOUNT_SLEW) {
	   set_track_pars(&track, incomm.arg, status.conf);
	   if (pushstack(&nstack, comstack, track) != 0) {
//	     syslog(LOG_ERR, "pushstack failed");
	     mountd_shutdown(1);
	   }
	   /* Make sure the incoming command doesn't zero the move bit */
	   incomm.arg.nozero = 1;
	   // }

	   /* First thing is to set the focus if necessary */
	   if ((getbit(incomm.arg.mode, bit(AUTO_FOCUS)) != 0) ||
	       (getbit(incomm.arg.mode, bit(USER_FOCUS)) != 0) ||
	       (getbit(incomm.arg.mode, bit(OFFSET_FOCUS)) != 0)) {
	     if (set_focus_pars(&focuscmd, incomm.arg, status.conf) == -1) {
//	       syslog(LOG_ERR,"Error in set_focus_pars()");
	       mountd_shutdown(1);
	     }
	     focuscmd.nozero = 1;  /* Focus command doesn't zero move bit */
	     if (pushstack(&nstack, comstack, focuscmd) != 0) {
//	       syslog(LOG_ERR,"pushstack failed");
	       mountd_shutdown(1);
	     }
	     /* Also just send the focus move command */
	     if (mount_comm(focuscmd.move_mode, &bogus, &focuscmd,
			    //  &status, &sigs.ipc) != 0) {
			    &status) != 0) {
//	       syslog(LOG_ERR,"mount_comm(focus) failed");
	       mountd_shutdown(1);
	     }
	   }
	 }

	 if (pushstack(&nstack, comstack, incomm.arg) != 0) {
//	   syslog(LOG_ERR, "pushstack failed");
	   mountd_shutdown(1);
	 }

	 setreqlow = 1;  /* command processed */
       } 
     } else {
  //     syslog(LOG_ERR, "shmcpy from failed");
       mountd_shutdown(1);
     }
       
     if (timediff(&status.tlast, &ftmp) != 0) {
  //     syslog(LOG_ERR, "timediff failed,%m");
       mountd_shutdown(1);
     }
     if (ftmp > status.conf.sample_time) sendstat = 1;

     if (setreqlow) {
       //       syslog(LOG_INFO,"command processed: ireq->LOW");
       if (shmreqset(&shm->ireq, SETLO,
		     ipcrec.semid, &sigs.ipc) == -1) {
//	 syslog(LOG_ERR,"shmreqset() failed");
	 mountd_shutdown(1);
       }
       setreqlow = 0;
     }

     if (sendstat == 1) {
       //       syslog(LOG_INFO,"Sending status to rotsed");
       /* Log the move_mode to see where it isn't working */
       if ((retval = shmcpy(stp,&status, sizeof(struct mountd_st),
			   &shm->oreq, CHECKLO, SETHI,
			   ipcrec.semid, &sigs.ipc)) == -1) {
//	 syslog(LOG_ERR,"shmcpy to failed");
	 mountd_shutdown(1);
       } else if (retval == 0) {
	 if (ishm > 0) {
//	   syslog(LOG_INFO,"Warning: shmcpy status failed: %d", ishm);
	 }
	 ishm++;
       } else {
	 ishm = 0;
       }
       if (ishm >= NROTSE_TOUT) {
//	 syslog(LOG_ERR,"rotsed not checking schierd status");
	 mountd_shutdown(1);
       }
       

       sendstat = 0;
       if (gettimeofday(&status.tlast, NULL) != 0) {
//	 syslog(LOG_ERR, "gettimeofday failed");
	 mountd_shutdown(1);
       }
     }

     /* Check offset...*/
     if (!isnan(status.conf.zero_mjd) && (ftmp_offset > OFFSET_DELAY)) {
       //       rlog(TERSE,status.conf.logfd,"Running update_offsets()\n");
       if (gettimeofday(&offsettime, NULL) != 0) {
//	 syslog(LOG_ERR, "gettimeofday failed");
	 mountd_shutdown(1);
       }
       if ((uo = update_offsets(&(status.conf))) < 0) {
//	 syslog(LOG_ERR,"Non-fatal error with update_offsets()");
       } else if (uo > 0) {
	 rlog(VERBOSE,status.conf.logfd,"No update from update_offsets()\n");
       }
     }
     /* check focus offset */
     if (ftmp_focoffset > FOCUS_OFFSET_DELAY) {
       if (gettimeofday(&focoffsettime, NULL) != 0) {
//	 syslog(LOG_ERR, "gettimeofday failed");
	 mountd_shutdown(1);
       }       
       /* this can be repeated, not a one-off job */
       if ((uo = update_focus(&(status.conf))) < 0) {
//	 syslog(LOG_ERR,"Non-fatal error with update_focus");
       }
     }
     
     if (sigprocmask(SIG_SETMASK, &oldmask, NULL) == -1) {
//       syslog(LOG_INFO, "sigprocmask() failed after ss__halt()");
       mountd_shutdown(1);
     }

     /* wait for next itimer SIGALRM signal */
     /*sigsuspend(&sigs.wait);*/
     
     if (sigsuspend(&sigs.wait) == -1) {
       if (errno != EINTR) {
//	 syslog(LOG_ERR,"sigsuspend failed: %m");
	 mountd_shutdown(1);
       }
     }
   }
   closelog();
   exit(1);			/* should never get here */
}

void set_track_pars(struct mountd_par *trck, struct mountd_par cmd, struct mountd_cfg cfg)
{
  /* Follow the ra, dec, and set to MOUNT_TRACK */
  /* Speed is set inside mount_comm */
  trck->ra = cmd.ra; 
  trck->dec = cmd.dec;
  trck->move_mode = MOUNT_TRACK;
}

int set_focus_pars(struct mountd_par *focus, struct mountd_par cmd, struct mountd_cfg cfg)
{
  float autofocus;
  /*focus->foc = cmd.foc;
    focus->move_mode = FOCUS_MOVE;*/

  if (getbit(cmd.mode, bit(AUTO_FOCUS)) != 0) {
    /* Autofocus */
    if (isnan(autofocus = calc_focus(cfg, cmd))) {
//      syslog(LOG_ERR,"Error in calc_focus()");
      return(-1);
    }
    focus->foc = autofocus;
    focus->move_mode = FOCUS_MOVE;
  } else if (getbit(cmd.mode, bit(USER_FOCUS)) != 0) {
    /* Use the given focus */
    if (isnan(cmd.foc)) {
//      syslog(LOG_ERR,"Cannot use specified focus (=nanval)");
      return(-1);
    }
    focus->foc = cmd.foc;
    focus->move_mode = FOCUS_MOVE;
  } else if (getbit(cmd.mode, bit(OFFSET_FOCUS)) != 0) {
    if (isnan(autofocus = calc_focus(cfg, cmd))) {
//      syslog(LOG_ERR,"Error in calc_focus()");
      return(-1);
    }
    focus->foc = autofocus + cmd.foc;
    focus->move_mode = FOCUS_MOVE;
  } else {
//    syslog(LOG_ERR,"Unknown command mode %c", cmd.mode);
    return(-1);
  }
  return(0);
}


void popstack(int *n, struct mountd_par *cmstk) {
  int i;

  /* This will move all elements in the stack down by one index number,
     overwriting the first element in the stack. */

  /* If there are no elements in the stack, there is nothing to delete. */
  if (*n >= 0) {
    for (i=0; i<MAXCOM-1; i++) 
      memcpy(&(cmstk[i]), &(cmstk[i+1]), sizeof(struct mountd_par));
    *n = *n - 1;
  }
}

int pushstack(int *n, struct mountd_par *cmstk, struct mountd_par cmd) {
  int i, err=0;

  /* This will move all existing commands up one index number in
     the stack and then copy the new command into the first element. */

  /* If the number of existing commands is too high, abort and return an error.
     Leave the last element of the array empty for housekeeping purposes. */
  if (*n < MAXCOM-2) { 
    for (i=*n; i>=0; i--)
      memcpy(&(cmstk[i+1]), &(cmstk[i]), sizeof(struct mountd_par));
    memcpy(&(cmstk[0]), &cmd, sizeof(struct mountd_par));
    *n = *n + 1;
  } else err = 1;

  return(err);
}

/* ---------------------------------------------------------------- */
/*                            functions                             */
/* ---------------------------------------------------------------- */

void mountd_shutdown(int severity)
{
/* Park the mount, shut down hardware communications, */
/* and shut down the mount daemon.                    */

   int oops, bog,mntq;
   struct mountd_par empcom;
   struct mountd_st empstat, status;
   //struct rotse_sigset sigs;


   /* Delayed shutdown copied from spotd */
   /*if (signal_setup(&sigs) == -1) {
     syslog(LOG_ERR,"signal_setup failed: %m");
     exit(severity);
     }*/
   /*
   if (sigprocmask(SIG_SETMASK, &sigs.norm, NULL) == -1) {
     syslog(LOG_ERR, "sigprocmask failed: %m");
     exit(severity);
     }*/

//   syslog(LOG_ERR, "begin shutdown, errno: %m");
   if (mtfd != -1) tcflush(mtfd, TCIOFLUSH);
   if (focfd != -1) tcflush(focfd, TCIOFLUSH);
   if (mtfd != -1) clear_mount_comm();
   

   /* if mount is initialized and serial port is open,
      park the mount and quit */

   bzero(&empcom, sizeof(struct mountd_par));
   bzero(&empstat, sizeof(struct mountd_st));
   bzero(&status, sizeof(struct mountd_st));


   //   if (estab) {
   if (estab == 1) {
//     syslog(LOG_INFO,"Sending to Park Position");
     /*  oops = mount_comm(MOUNT_STANDBY, &bog, &empcom, &empstat);*/
     //oops = mount_comm(MOUNT_PARK, &bog, &empcom, &empstat, &sigs.ipc);
     oops = mount_comm(MOUNT_PARK, &bog, &empcom, &empstat);

     mntq = -1;

     /* Kludge a couple things */
     empcom.move_mode = MOUNT_SLEW;
     empcom.encpos[0] = gstow[0];
     empcom.encpos[1] = gstow[1];
     status.conf.enctol = encoder_tol;

     /* wait until idle */
     /*while (mntq != MOUNT_IDLE) {
       if ((oops = mount_comm(MOUNT_QUERY, &mntq, &empcom, &status)) != 0) {
	 syslog(LOG_ERR,"query_mount failed, %m");
	 exit(severity);
       } else if (oops == MOUNT_ERROR) {
	 syslog(LOG_ERR,"query_mount returned Error");
	 exit(severity);
       }
       sigsuspend(&sigs.wait);
       }*/

     if (oops == 0) syslog(LOG_ERR, "normal termination of mountd");
   } /*else {
     syslog(LOG_INFO,"Idling Mount?");
     }*/


   /* Here is where we can halt the mount (when the brake doodads are in) */
   /*oops = mount_comm(MOUNT_HALT, &bog, &empcom, &empstat); */

//   syslog(LOG_INFO,"closing mtfd");
   if (mtfd) close(mtfd);
//   syslog(LOG_INFO,"closing focfd");
   if (focfd) close(focfd);
   exit(severity);		/* just a kludge for now. */
}

int error_recover(int *rst, int *nstk, struct mountd_par *cmstk, struct mountd_cfg *cfg, 
		  int *zalarm) {
  int perr=0, ax;
  struct mountd_par cmd;
  char almsg[MAXMAIL], line[100];
  int statbits[2];
  int i;
  int retval = 0;
  /*int queries, mntq;*/

  int bogus = 0;
  struct mountd_st stat;
  //struct rotse_sigset sigs;

  int bad_slew = 0;
  struct mountd_par temp_arg, track, lim_shift;

  
  *zalarm = 0;
  almsg[0] = '\0';

  for (ax=0;ax<2;ax++) {
    statbits[ax] = cmstk[0].statbits[ax];
  }

  /* Reset cmd to inactive so it will be sent again, if possible */
  /*  cmstk[0].active = 0;
  if ((cmstk[0].move_mode == MOUNT_TRACK) || (cmstk[0].move_mode == MOUNT_TRACK_RA))
      popstack(nstk, cmstk);
  */
  
  if ((statbits[0] == statbits[1]) && (statbits[0] == 0)) {
    bad_slew = 1;
    sprintf(line,"Pointing outside tolerance.\n");
    strcat(almsg,line);
  //  syslog(LOG_ERR, "Pointing outside tolerance.");
    /* This command failed during a slew, so the current command is okay */
    memcpy(&temp_arg, &(cmstk[0]), sizeof(struct mountd_par));
  } 


  /* Clear the stack */
  for (i=0; i<MAXCOM; i++) {
    bzero(&(cmstk[i]), sizeof(struct mountd_par));
    cmstk[i].active = 0;
    cmstk[i].move_mode = MOUNT_IDLE;
  }
  *nstk = -1;

//  syslog(LOG_INFO,"%d;%d and %d;%d",limit_status[0],limit_status[1],
//	 statbits[0], statbits[1]);

  /* is it a limit problem? */
  if (((limit_status[0] != 0) || (limit_status[1] != 0)) &&
      (statbits[0] == limit_status[0]) &&
      (statbits[1] == limit_status[1])) {  // also want to check bad_slew=0?
    // the limit bit is set, and no other error bits are set
    // we've already cleared the stack, so we need to push out with a shift
    
    if (*rst < MAX_RECOVERY) {
      bzero(&lim_shift, sizeof(struct mountd_par));
      lim_shift.move_mode = MOUNT_SHIFT;
      lim_shift.slew_spd = 10;  // slow retreat
      if (getbit(limit_status[0],bit(NEG_LIM)) != 0) {
	lim_shift.ra = 2.0;
      } else if (getbit(limit_status[0],bit(POS_LIM)) != 0) {
	lim_shift.ra = -2.0;
      }
      if (getbit(limit_status[1],bit(NEG_LIM)) != 0) {
	lim_shift.dec = 2.0;
      } else if (getbit(limit_status[1],bit(POS_LIM)) != 0) {
	lim_shift.dec = -2.0;
      }
      
      // and put it in the stack
      if (pushstack(nstk, cmstk, lim_shift) != 0) {
//	syslog(LOG_ERR,"pushstack failed");
	return(-1);
      }
//      syslog(LOG_INFO,"Attempting to back out of limit...");
      *rst = *rst + 1;
      *zalarm = 1;
    } else {
//      syslog(LOG_ERR,"Too many recovery retries.");
      return(-1);
    }
  } else {

    if (bad_slew) {
      if (*rst < MAX_RECOVERY) {
//	syslog(LOG_ERR,"Preparing to resend move command");
	set_track_pars(&track, temp_arg, *cfg);
	if (pushstack(nstk, cmstk, track) != 0) {
//	  syslog(LOG_ERR,"pushstack failed");
	  return(-1);
	}
	if (pushstack(nstk, cmstk, temp_arg) != 0) {
//	  syslog(LOG_ERR,"pushstack failed");
	  return(-1);
	}
	*rst = *rst + 1;
	/* syslog(LOG_INFO,"nozero = %d, ra: %f, dec: %f", cmstk[0].nozero,
	   cmstk[0].ra, cmstk[0].dec);*/
	*zalarm = 1;
      } else {
	sprintf(line,"Too many recovery tries. Shutting down.\n");
	strcat(almsg,line);
//	syslog(LOG_ERR,"Too many recovery tries.  Re-Move failed.");
	return(-1);
      }
    } else {
      /* First, have mount HALT, and wait a sec */
    
      bzero(&stat, sizeof(stat));
      bzero(&cmd, sizeof(cmd));
      /*  if (signal_setup(&sigs) == -1) {
	  syslog(LOG_ERR,"signal_setup failed: %m");
	  return(-1);
	  }*/

      //mount_comm(MOUNT_HALT, &bogus, &cmd, &stat, &sigs.ipc);
      mount_comm(MOUNT_HALT, &bogus, &cmd, &stat);
      sleep(3);
  
      retval = 0;
      for (ax=0; ax<2; ax++) {
	if (statbits[ax] & 0x004) {
	  /* Drive in e-stop.  Nothing to be done. */
//	  syslog(LOG_ERR, "Drive %d in e-stop.", ax);
	  sprintf(line, "Axis %d in e-stop\n", ax);
	  strcat(almsg, line);
	  retval = -1;
	  perr++;
	} else {
	  if (getbit(statbits[ax], bit(BRAKE_ON)) != 0) {
	    sprintf(line, "Brake on axis %d\n", ax);
	    strcat(almsg, line);
//	    syslog(LOG_ERR,"Brake on axis %d", ax);
	    perr++;
	  }
	  if (getbit(statbits[ax], bit(AMP_DISABLED)) != 0) {
	    sprintf(line, "Drive amp disabled on axis %d\n", ax);
	    strcat(almsg, line);
//	    syslog(LOG_ERR,"Drive amp disabled on axis %d", ax);
	    perr++;
	  }
	  if (getbit(statbits[ax], bit(NEG_LIM)) != 0) {
	    sprintf(line, "Axis %d in neg limit\n", ax);
	    strcat(almsg, line);
//	    syslog(LOG_ERR,"Axis %d in negative limit", ax);
	    perr++;
	  }
	  if (getbit(statbits[ax], bit(POS_LIM)) != 0) {
	    sprintf(line, "Axis %d in pos limit\n", ax);
	    strcat(almsg, line);
//	    syslog(LOG_ERR,"Axis %d in positive limit", ax);
	    perr++;
	  }
	}
      }
    
      /*
      if (perr == 0) {
      sprintf(line,"Pointing outside tolerance\n");
      strcat(almsg, line);
      syslog(LOG_ERR,"Pointing outside tolerance.");
      }
    */
  /* Query the mount until the error (might) go away */
  /*queries = 0;
  mntq = MOUNT_ERROR;
  while ((queries < MAX_ERRORCLEAR) && (mntq == MOUNT_ERROR)) {
    cmd.move_mode = MOUNT_INIT;
    mount_comm(MOUNT_INIT, &bogus, &cmd, &stat, &sigs.ipc);
    mount_comm(MOUNT_QUERY, &mntq, &cmd, &stat, &sigs.ipc);
    queries++;
    }*/



      if (retval == 0) {
	if (*rst < MAX_RECOVERY) {
	  if (cfg->mount_run == 0 || (estab == 0)) {
	    /* we don't have the mount_run command, or we haven't homed yet */
	    cmd.move_mode = MOUNT_ZEROS;
	    cmd.nozero = 0;
	    cmd.active = 0;
	    if (pushstack(nstk, cmstk, cmd) != 0) {
	      syslog(LOG_ERR, "pushstack failed");
	      return(-1);
	    }
	    cmd.move_mode = MOUNT_SYNC;
	    cmd.nozero = 1;
	    if (pushstack(nstk, cmstk, cmd) != 0) {
//	      syslog(LOG_ERR, "pushstack failed");
	      return(-1);
	    }
	  } else {
	    /* we have the mount_run command */
	    cmd.move_mode = MOUNT_RUN;
	    cmd.nozero = 0;
	    cmd.active = 0;
	    if (pushstack(nstk, cmstk, cmd) != 0) {
//	      syslog(LOG_ERR,"pushstack failed");
	      return(-1);
	    }
	    cmd.move_mode = MOUNT_INIT;
	    cmd.nozero = 1;
	    if (pushstack(nstk, cmstk, cmd) != 0) {
//	      syslog(LOG_ERR, "pushstack failed");
	      return(-1);
	    }
	  }
	  sprintf(line, "Attempting to recover.\n");
	  strcat(almsg, line);
//	  syslog(LOG_INFO,"Attempting to recover...");
	  *rst = *rst + 1;
	} else {
	  sprintf(line, "Too many recovery tries. Shutting down.\n");
	  strcat(almsg, line);
//	  syslog(LOG_ERR,"Too many recovery tries. Reset Failed.");
	  return(-1);
	}
      }
    }
  }  
  if (strlen(almsg) > 2) {
    mailalert(almsg, cfg->erroremail);
    rlog(1,cfg->logfd,almsg);
  }

  return(retval);

}

void mailalert(char *mailmg, char *targ) {
  FILE *pml;
  char comm[2*MAXFILELEN];

  sprintf(comm, "mailx -s 'Mount error' %s", targ);
  if ((pml = popen(comm, "w")) == NULL) {
    /* problem opening pipe */
  } else {
    fprintf(pml, "%s", mailmg);
    pclose(pml);
  }
}

int focus_recover(int *reset, int *nstack, struct mountd_par *comstack, struct mountd_cfg *cfg)
{
  int i;
  struct mountd_par fcmd;
  struct mountd_st stat;
  int mntq;

  /* clear the stack */
  for (i=0; i<MAXCOM; i++) {
    bzero(&(comstack[i]), sizeof(struct mountd_par));
    comstack[i].active = 0;
    comstack[i].move_mode = MOUNT_IDLE;
  }
  *nstack = -1;

  bzero(&fcmd, sizeof(fcmd));
  bzero(&stat, sizeof(stat));

  if (*reset < MAX_FOCUS_RECOVERY) {
 //   syslog(LOG_ERR,"Preparing to restart focus motor");
    fcmd.move_mode = FOCUS_INIT;
    stat.conf.testmode = cfg->testmode;
    if (mount_comm(fcmd.move_mode, &mntq, &fcmd, &stat) != 0) {
 //     syslog(LOG_ERR,"Could not restart focus motor");
      return(-1);
    }
    (*reset)++;
  } else {
 //   syslog(LOG_ERR,"Too many focus recovery tries.  Shutting down.");
    return(-1);
  }

  return(0);
}		  
