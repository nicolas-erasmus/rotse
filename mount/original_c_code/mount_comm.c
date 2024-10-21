/* mount_comm ======================================================== 

 * Purpose: Passes a command to the mount and returns a response if called for
 *
 * Inputs: command type, command structure (may be empty), conf struct
 *    
 * Outputs: integer status result (may not be used)
 *
 * Created: 2001-05-11  Don Smith
 * Updated: 2002-04-02  E. Rykoff -- many mods, bug fixes, clean-ups.
 *                                   and improved error handling (not perfect)
 * Updated: 2003-02-03  E. Rykoff -- added focus range checking
 * Updated: 2004-07-16  E. Rykoff -- fixed up serial reading;
 *                                   changed usleep to rnanosleep
 * Updated: 2004-09-28  E. Rykoff -- added ptg_offset reset
 * Updated: 2004-10-13  E. Rykoff -- added MOUNT_RUN
 * Updated: 2004-11-30  E. Rykoff -- fixed mount error bit checking
 * Updated: 2004-12-16  E. Rykoff -- added check after STOP commands
 * Updated: 2005-01-28  E. Rykoff -- added overspeed config
 * Updated: 2005-04-14  E. Rykoff -- added ptg_offset to limit check
 * Updated: 2005-10-06  E. Rykoff -- fixed error bits; added limit_status
 ========================================================================= */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <syslog.h>
#include <values.h>

#include <rotsed_str.h>
#include <slalib.h>
#include <slamac.h>


#include "protos.h"
#include "schierd.h"
#include "commands.h"

#define RESP_LEN 1000

extern int mtfd;
extern int limit_status[2];

int mount_comm(int COMM_TYPE, int *result, struct mountd_par *commst, struct mountd_st *st)
{
  int error = 0, junk, i;
  int apos[2][2], cpos[2][2];
  int nowpos[2],  limitpos[2], futurepos[2];
  float fvel[2];
  static float trkvel[2];
  char outcom[100], mntresp[RESP_LEN];
  double deltim;
  double sidrate=360.0/86636.55; /* Degrees per second in siderial rate */
  float focpos[2], focresp;
  double old_ra = 0.0, old_dec = 0.0;
  float ftmp;
  char logstr[100];
  float volt[2];
  int limitproblem = 0;

  struct timespec focsleep;

  time_t now;
  struct tm lt;
  
  static float lastvolt[2];

  char vtmpfile[100], line[100];
  static int tfd = -1;

  *result = 0;

  /*commst->active = 0;*/  /* Check this */

  switch (COMM_TYPE) {

  case MOUNT_QUERY:
    /* This option is meant to determine if the mount is moving, or idle.  We
       must poll the status twice and see if the command value changes, and if
       not, if the actual value is close to the command value.
    */

    /* Note: eventually, we will likely need a more sophisticated
       version of this that looks for damped oscillations after stopping. */

    /* Have to do this in both RA and Dec.  Alternate to give axis time to change */
    for (i=0; i<2; i++) {
      if (get1stat(i, &(apos[0][i]), &(cpos[0][i]), st->conf.testmode) != 0) {
	syslog(LOG_ERR,"Error in get1stat");
	return(-1);
      }
    }
 
    /* Check for safe holds, etc. */
    for (i=0; i<2; i++) {
      if (get2stat(i, commst->statbits, st->conf.testmode) != 0) {
	syslog(LOG_ERR,"Error in get2stat");
	return(-1);
      }
    }

    /* monitor voltage */
    for (i=0; i<2; i++) {
      if (get3stat(i, &(volt[i]), &ftmp, st->conf.testmode) != 0) {
	syslog(LOG_ERR,"Error in get3stat");
	return(-1);
      }
    }

    /* Checking position a second time */
    for (i=0; i<2; i++) {
      if (get1stat(i, &(apos[1][i]), &(cpos[1][i]), st->conf.testmode) != 0) {
	syslog(LOG_ERR,"Error in get1stat");
	return(-1);
      }
    }


    /* Determine if mount is moving, idle, or locked up */
    if (getbit(st->conf.testmode,bit(NO_MOUNT)) == 0) {
      //      *result = evalstat(apos, cpos, commst, st->conf.enctol);
      *result = evalstat(apos, cpos, commst, &st->conf);
    } else {
      *result = 0;
    }

    if (*result == MOUNT_ERROR) {
      /* log last voltage on error */
      syslog(LOG_INFO,"Last voltage: RA = %.1f, Dec = %.1f", lastvolt[0], lastvolt[1]);
    }

    st->data.encpos[0] = apos[1][0] - st->conf.zeropt[0];// - st->conf.ptg_offset[0];
    st->data.encpos[1] = apos[1][1] - st->conf.zeropt[1];// - st->conf.ptg_offset[1];
    enc2radec(st->data.encpos, &st->data.enc_ra, &st->data.enc_dec, st->conf);
    st->data.v_ra = volt[0];
    st->data.v_dec = volt[1];
    lastvolt[0] = volt[0];
    lastvolt[1] = volt[1];
    if (*result == MOUNT_ERROR) {
      sprintf(logstr,"Mount error at %d, %d\n",st->data.encpos[0],st->data.encpos[1]);
      rlog(1,st->conf.logfd,logstr);
    }

    /* and write out the voltages if we're recording them */
    if (tfd != -1) {
      sprintf(line,"%12d%6.2f%12d%6.2f\n",
	      st->data.encpos[0], st->data.v_ra,
	      st->data.encpos[1], st->data.v_dec);

      write(tfd, line, strlen(line));
      /* check if we're done with the move */
      if (*result == MOUNT_IDLE) {
	close(tfd);
	tfd = -1;
      }
    }
  
    break;
  case MOUNT_INIT:
    syslog(LOG_INFO,"In MOUNT_INIT part");
  
    /* set the velocity to 0 */
    sprintf(outcom, "$%s, %d", comkeys[VelRa], 0);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s, %d", comkeys[VelDec], 0);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    /* set the acceleration to the proper value */
    sprintf(outcom, "$%s, %d", comkeys[AccelRa], (int) (st->conf.slw_acc[0]*st->conf.deg2enc[0]));
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s, %d", comkeys[AccelDec], (int) (st->conf.slw_acc[1]*st->conf.deg2enc[1]));
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    
    /* set the maximum velocity to the proper value */
    sprintf(outcom,"$%s, %d", comkeys[MaxVelRA], (int) (st->conf.overspeed * st->conf.max_vel[0]*st->conf.deg2enc[0]));
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom,"$%s, %d", comkeys[MaxVelDec], (int) (st->conf.overspeed * st->conf.max_vel[1]*st->conf.deg2enc[1]));
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    /* Halt the mount if not already halted */
    sprintf(outcom, "$%s", comkeys[HaltRA]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s", comkeys[HaltDec]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    /* Stop to turn the amplifiers on */
    for (i=0;i<2;i++) {
      if (stop_axis(i, st->conf.testmode) != 0) {
	syslog(LOG_ERR,"Error with stop_axis(%d)", i);
	return(-1);
      }
    }

    /*
    sprintf(outcom, "$%s", comkeys[StopRA]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s", comkeys[StopDec]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    */
    if (comm2focus(0, "1MO", &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command 1MO responded %f", focresp);
      return(-1);
    } 
    st->data.slew_spd = 0;
    st->data.trk_spd = 0.0;

    break;
  case MOUNT_SYNC:
    setbits(st->state, bit(MOVE));
    syslog(LOG_INFO,"In MOUNT_SYNC part");
    /* Erase current zero point values */
    st->conf.zeropt[0] = 0;
    st->conf.zeropt[1] = 0;
    st->conf.ptg_offset[0] = st->conf.ptg_offset[1] = 0;
    
    st->conf.zero_mjd = NANVAL;
    
    /* Set to home speed */
    sprintf(outcom, "$%s, %d", comkeys[VelRa], (int) (st->conf.home_vel[0]*st->conf.deg2enc[0]));
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s, %d", comkeys[VelDec], (int) (st->conf.home_vel[1]*st->conf.deg2enc[1]));
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }


    /* Stop the mount before homing */
    for (i=0;i<2;i++) {
      if (stop_axis(i, st->conf.testmode) != 0) {
	syslog(LOG_ERR,"Error with stop_axis(%d)", i);
	return(-1);
      }
    }


    /*
    sprintf(outcom, "$%s", comkeys[StopRA]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s", comkeys[StopDec]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    */
    /* Tell the mount to go home */
    sprintf(outcom, "$%s", comkeys[HomeRA]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    sprintf(outcom, "$%s", comkeys[HomeDec]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    st->data.slew_spd = (int) (st->conf.home_vel[0]*st->conf.deg2enc[0]);
    st->data.trk_spd = 0.0;
    break;


    /* new */
  case MOUNT_RUN:
    setbits(st->state, bit(MOVE));
    syslog(LOG_INFO,"In MOUNT_RUN part");
    /* Set to 0 speed */
    sprintf(outcom, "$%s, %d", comkeys[VelRa], 0);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s, %d", comkeys[VelDec], 0);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    /* Stop the mount before running */
    for (i=0;i<2;i++) {
      if (stop_axis(i, st->conf.testmode) != 0) {
	syslog(LOG_ERR,"Error with stop_axis(%d)", i);
	return(-1);
      }
    }



    /*
    sprintf(outcom, "$%s", comkeys[StopRA]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s", comkeys[StopDec]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    */
    /* Tell the mount to run */
    sprintf(outcom, "$%s", comkeys[RunRA]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    sprintf(outcom, "$%s", comkeys[RunDec]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    st->data.slew_spd = 0.0;
    st->data.trk_spd = 0.0;
    break;


  case MOUNT_HALT:
    sprintf(outcom, "$%s", comkeys[HaltRA]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    sprintf(outcom, "$%s", comkeys[HaltDec]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    st->data.trk_spd = 0.0;
    st->data.slew_spd = 0;
    break;
  case MOUNT_STANDBY:
    syslog(LOG_INFO,"In MOUNT_STANDBY");
    /* This is actually a move command to the park position at 50% slew rate */
    setbits(st->state, bit(MOVE));
    for (i=0;i<2;i++) {
      fvel[i] = (st->conf.max_vel[i] * st->conf.deg2enc[i] * STANDBY_SPEED) / 100.0;
      commst->encpos[i] = (int) (st->conf.standbypos[i] * st->conf.deg2enc[i]) +
	st->conf.zeropt[i];
    }
    st->data.ra = 0;
    st->data.dec = 0;
    st->data.slew_spd = (int) STANDBY_SPEED;
    st->data.trk_spd = 0.0;

    if ((error = mount_move(commst->encpos, fvel, &(st->conf), 1)) != 0) {
      syslog(LOG_ERR,"Error with mount_move");
      return(-1);
    }
    break;

  case MOUNT_IDLE:
    syslog(LOG_INFO,"In MOUNT_IDLE");
    sprintf(outcom, "$%s, 0", comkeys[VelRa]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    sprintf(outcom, "$%s, 0", comkeys[VelDec]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    for (i=0; i<2; i++) {
      if (get1stat(i, &junk, &nowpos[i], st->conf.testmode)) {
	syslog(LOG_ERR,"Error in get1stat()");
	return(-1);
      }
    }

    sprintf(outcom, "$%s, %d", comkeys[PosRA], nowpos[0]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    
    sprintf(outcom,"$%s, %d", comkeys[PosDec], nowpos[1]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    st->data.move_mode = 0;
    setbits(st->data.move_mode, bit(MOUNT_IDLE));
    st->data.trk_spd = 0.0; 
    st->data.slew_spd = 0; 
    break;
  case MOUNT_TRACK:
    /* Since tracking can be interrupted, zero out the state bit */
    zerobits(st->state, bit(MOVE));
  
    for (i=0; i<2; i++) {
      fvel[i] = fabs(trkvel[i]);
    }
    if (trkvel[0] > 0) {
      commst->encpos[0] = (int) (st->conf.rarange[1] * st->conf.deg2enc[0]) + st->conf.zeropt[0] + st->conf.ptg_offset[0];
    } else {
      commst->encpos[0] = (int) (st->conf.rarange[0] * st->conf.deg2enc[0]) + st->conf.zeropt[0] + st->conf.ptg_offset[0];
    }
    if (trkvel[1] > 0) {
      commst->encpos[1] = (int) (st->conf.decrange[1] * st->conf.deg2enc[1]) + st->conf.zeropt[1] + st->conf.ptg_offset[1];
    } else {
      commst->encpos[1] = (int) (st->conf.decrange[0] * st->conf.deg2enc[1]) + st->conf.zeropt[1] + st->conf.ptg_offset[1];
    }

    st->data.slew_spd = 0;
    st->data.trk_spd = sqrt((fvel[0] * fvel[0]) + (fvel[1] * fvel[1]));

  case MOUNT_SLEW:
 
    //old_ra = old_dec = 0;  // this shouldn't be there, should it?
    if (COMM_TYPE == MOUNT_SLEW) {
      setbits(st->state, bit(MOVE));
      for (i=0;i<2;i++) {
	if (get1stat(i, &junk, &nowpos[i], st->conf.testmode) != 0) {
	  syslog(LOG_ERR,"Error with get1stat()");
	  return(-1);
	}
      }
  
      if (coord2enc(commst->ra, commst->dec, &(st->conf), commst->encpos, 0.0) != 0) {
	syslog(LOG_ERR, "Coordinates not observable.");
	return(-1);
      }

      /* Put in here future calculation and track speed calculation! */
      deltim = 60.0;
      if (coord2enc(commst->ra, commst->dec, &(st->conf), futurepos, deltim/(60.*60.*24.)) != 0) {
	syslog(LOG_ERR, "Coordinates not observable.");
	return(-1);
      }
      /* check if we're going to track over the limit */
      limitproblem = 0;
      if ((abs(futurepos[0] - commst->encpos[0]) > (10 * st->conf.deg2enc[0])) ||
	  (abs(futurepos[1] - commst->encpos[1]) > (10 * st->conf.deg2enc[1]))) {
	/*syslog(LOG_ERR,"%d, %d: Tracking over limit!",abs(futurepos[0]-commst->encpos[0]),
	  abs(futurepos[1] - commst->encpos[1]));*/
	syslog(LOG_ERR,"Tracking over limit: sending to standby");
	for (i=0; i<2; i++) {
	  commst->encpos[i] = futurepos[i] = st->conf.standbypos[i];
	}
	/*	return(-1);*/
	limitproblem = 1;
      }

      for (i=0; i<2; i++) {
	trkvel[i] = (float)(futurepos[i]-commst->encpos[i])/deltim;
      }

      /* and add on the extra dec tracking -- optional!*/
      trkvel[1] = trkvel[1] + commst->dectrack * st->conf.deg2enc[1];
 
      /* Back to normal */

      for (i=0; i<2; i++) {
	fvel[i] = (st->conf.max_vel[i] * st->conf.deg2enc[i] * (float)commst->slew_spd) / 100.0;
      }
      /* Where are we now? */
      deltim = ftmp = 0;
      for (i=0; i<2; i++) {
	if (get1stat(i, &junk, &nowpos[i], st->conf.testmode) != 0) {
	  syslog(LOG_ERR,"Error with get1stat()");
	  return(-1);
	}
	ftmp = fabs((float)(nowpos[i] - commst->encpos[i])) / fvel[i];
	if (ftmp > deltim) deltim = ftmp;
      }
      /* Recalculate encoder position that far in the future */
      if (!limitproblem) {
	if ((error = coord2enc(commst->ra, commst->dec, &(st->conf), commst->encpos,
			       deltim / (60. * 60. * 24.)) != 0)) {
	  syslog(LOG_ERR, "Coordinates not observable.");
	  return(-1);
	}
      }

      old_ra = st->data.ra;
      old_dec = st->data.dec;
      st->data.ra = commst->ra;
      st->data.dec = commst->dec;
      st->data.slew_spd = (int) fvel[0];  /* just ra for now */
      st->data.trk_spd = 0.0;
    }
    if ((error = mount_move(commst->encpos, fvel, &(st->conf), 1)) != 0) {
      syslog(LOG_ERR,"Error with mount_move");
      if (error == -2) {
	cancel_move(commst, st, old_ra, old_dec);
	/*
	syslog(LOG_INFO,"Illegal position (%.2f, %.2f). Not Moving.",
	       commst->ra, commst->dec);
	zerobits(st->state, bit(MOVE));
	st->data.ra = old_ra;
	st->data.dec = old_dec;
	for (i=0; i<2;i++) {
	  if (get1stat(i, &junk, &(commst->encpos[i]), st->conf.testmode)) {
	    syslog(LOG_ERR,"Error in get1stat()");
	    return(-1);
	  }
	  }*/
	fvel[0] = fvel[1] = 0;
	if ((error = mount_move(commst->encpos, fvel, &(st->conf), 1)) != 0) {
	  syslog(LOG_INFO,"error cancelling move.");
	}
	error = 0;
      } else {
	return(-1);
      }
    }
    break;

  case MOUNT_SHIFT:
    /* Shift the mount delta-ra and delta-dec in encoder steps */
    setbits(st->state, bit(MOVE));

    for (i=0; i<2; i++) {
      if (get1stat(i, &junk, &(commst->encpos[i]), st->conf.testmode) != 0) {
	syslog(LOG_ERR,"Error with get1stat");
	return(-1);
      }
    }
    if (coord2enc_delta(commst->ra, commst->dec, &(st->conf), commst->encpos) != 0) {
      syslog(LOG_ERR,"Odd...error with coord2enc_delta?");
      return(-1);
    }

    for (i=0; i<2; i++) {
      fvel[i] = (st->conf.max_vel[i] * st->conf.deg2enc[i] * (float)commst->slew_spd) / 100.0;
    }
    
    if ((error = mount_move(commst->encpos, fvel, &(st->conf), 1)) != 0) {
      syslog(LOG_ERR,"Error with mount_move");
      if (error == -2) {
	cancel_move(commst, st, old_ra, old_dec);
	fvel[0] = fvel[1] = 0;
	if ((error = mount_move(commst->encpos, fvel, &(st->conf), 1)) != 0) {
	  syslog(LOG_INFO,"error cancelling move.");
	}
	error = 0;
      } else {
	return(-1);
      }
    }

    st->data.slew_spd = (int) fvel[0];
    st->data.trk_spd = 0.0;

    /* and start recording voltage if necessary */
    if (getbit(commst->mode, bit(RECORD_VOLTAGE)) != 0) {
      now = time(NULL);
      lt = *localtime(&now);
      sprintf(vtmpfile,"%s/voltage-%02d-%02d-%02d-XXXXXX",
	      P_tmpdir, lt.tm_hour, lt.tm_min, lt.tm_sec);
      if ((tfd = mkstemp(vtmpfile)) == -1) {
	syslog(LOG_ERR,"mkstemp failed in mount_comm(): no voltage record");
      }
      chmod(vtmpfile, 0644);
      syslog(LOG_INFO,"Voltage file: %s", vtmpfile);
    }

    break;
  case MOUNT_TRACK_RA:
    zerobits(st->state, bit(MOVE));

    fvel[0] = sidrate * st->conf.deg2enc[0];
    if (st->conf.latitude < 0.0) { fvel[0] *= -1.0; }
    fvel[1] = 0.0;
    limitpos[0] = (int) (st->conf.rarange[1] * st->conf.deg2enc[0]) + st->conf.zeropt[0] + st->conf.ptg_offset[0];
    limitpos[1] = (int) (st->conf.decrange[1] * st->conf.deg2enc[1]) + st->conf.zeropt[1] + st->conf.ptg_offset[1];

    if ((error = mount_move(limitpos, fvel, &(st->conf), 1)) != 0) {
      syslog(LOG_ERR,"Error with mount_move");
      if (error == -2) {
	cancel_move(commst, st, old_ra, old_dec);
	/*
	syslog(LOG_INFO,"Illegal position...not the end of the world");
	zerobits(st->state, bit(MOVE));
	for (i=0; i<2;i++) {
	  if (get1stat(i, &junk, &(commst->encpos[i]), st->conf.testmode)) {
	    syslog(LOG_ERR,"Error in get1stat()");
	    return(-1);
	  }
	  }*/
	fvel[0] = fvel[1] = 0;
	if ((error = mount_move(commst->encpos, fvel, &(st->conf), 1)) != 0) {
	  syslog(LOG_INFO,"error cancelling move.");
	}
	error =0;
      } else {
	return(-1);
      }
    }

    st->data.trk_spd = (int) (sidrate * st->conf.deg2enc[0]);
    st->data.slew_spd = 0;

    break;
  case MOUNT_STOW:
  case MOUNT_PARK:
    setbits(st->state, bit(MOVE));

    syslog(LOG_INFO,"in STOW/PARK");

    /* Set to home speed */
    sprintf(outcom, "$%s, %d", comkeys[VelRa], gstow_vel[0]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s, %d", comkeys[VelDec], gstow_vel[1]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    sprintf(outcom, "$%s, %d", comkeys[PosRA], gstow[0]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }

    sprintf(outcom, "$%s, %d", comkeys[PosDec], gstow[1]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    break;

  case MOUNT_ZEROS:
    /* Set the zero points to the current position */
    syslog(LOG_INFO,"Running Mount Zeros");
    for (i=0; i<2; i++) 
      if (get1stat(i, &junk, &(st->conf.zeropt[i]), st->conf.testmode) != 0) {
	syslog(LOG_ERR, "Error in get1stat");
	return(-1);
      }
    
    syslog(LOG_INFO,"Zero point: %d, %d", st->conf.zeropt[0], st->conf.zeropt[1]);
    /* Set the zero mjd */
    now = time(NULL);
    st->conf.zero_mjd = time2mjd(now);
    
    if (commst->nozero == 0) {
      /*zerobits(st->state, bit(MOVE));*/
      /* kludgy at moment */
      zerobits(st->state, bit(ALARM));
      st->data.alarm_type = ALRM_OFF;
    }
    /* Set up globals */
    estab = 1;
    for (i=0; i<2; i++) {
      gstow[i] = (int) (st->conf.stowpos[i] * st->conf.deg2enc[i]) 
	+ st->conf.zeropt[i];
      gstow_vel[i] = (int) (st->conf.home_vel[i] * st->conf.deg2enc[i]);
    }
    syslog(LOG_INFO,"gstow values = %d, %d", gstow[0], gstow[1]);
    syslog(LOG_INFO,"gstow velocities = %d, %d", gstow_vel[0], gstow_vel[1]);
    break;
  case FOCUS_ON:
    if (comm2focus(0, "1MO", &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command 1MO responded %f", focresp);
      return(-1);
    }
    break;
  case FOCUS_OFF:
    if (comm2focus(0, "1MF", &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command 1MF responded %f", focresp);
      return(-1);
    }
    break;
  case FOCUS_SYNC:
    syslog(LOG_INFO,"In focus sync");
    if (comm2focus(0, "1PA-100", &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command 1PA-100 responded %f", focresp);
      return(-1);
    }
    setbits(st->state, bit(MOVE));
    break;
  case FOCUS_MOVE:
    if (commst->foc > st->conf.focrange[1]) {
      syslog(LOG_ERR,"Focus command too large.  Setting to %f", st->conf.focrange[1]);
      commst->foc = st->conf.focrange[1];
    } else if (commst->foc < st->conf.focrange[0]) {
      syslog(LOG_ERR,"Focus command too small.  Setting to %f", st->conf.focrange[0]);
      commst->foc = st->conf.focrange[0];
    }
    rlog(TERSE, st->conf.logfd, "Moving focus to %f\n", commst->foc);
    sprintf(outcom,"1PA%f", commst->foc);
    if (comm2focus(0, outcom, &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %f", outcom, focresp);
      return(-1);
    }
    setbits(st->state, bit(MOVE));
    break;
  case FOCUS_QUERY:
    /* First make sure the mount is on */
    if (comm2focus(1, "1MO?", &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command 1MO? responded %f", focresp);
      return(-1);
    }
    if (getbit(st->conf.testmode, bit(NO_FOCUS)) == 0) {
      if (focresp == 0.0) {
	syslog(LOG_ERR,"Error: focus motor is off.");
	return(-1);
      }
    }
    /* Now get encoder position twice */
    if (comm2focus(1, "1TP?", &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command 1TP? responded %f", focresp);
      return(-1);
    }
    focpos[0] = focresp;
    /* Insert pause here? */
    //usleep(50000);
    focsleep.tv_sec = 0;
    focsleep.tv_nsec = 50000000;
    rnanosleep(&focsleep);

    if (comm2focus(1, "1TP?", &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command 1TP? responded %f", focresp);
      return(-1);
    }
    focpos[1] = focresp;

    if (getbit(st->conf.testmode,bit(NO_FOCUS)) == 0) {
      *result = evalfocus(focpos, commst, st->conf.foctol);
    } else {
      *result = MOUNT_IDLE;
    }

    /* change = focpos[1] - focpos[0];
    if ((change > st->conf.foctol)||(change < -st->conf.foctol)) {
      *result = FOCUS_MOVE;
    } else {
      *result = MOUNT_IDLE;
      }*/
    st->data.foc = (double) focpos[1];
    break;
  case FOCUS_ZEROS:
    syslog(LOG_INFO,"Zeroing Focus");
    if (comm2focus(0, "1DH", &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command 1DH responded %f", focresp);
      return(-1);
    }
    break;
  case FOCUS_INIT:
    if (comm2focus(0, "1MO", &focresp, st->conf.testmode) != 0) {
      syslog(LOG_ERR,"Error: Command 1MO responded %f", focresp);
      return(-1);
    }
    break;
  default:
    error = -1;
  }
  if ((COMM_TYPE != MOUNT_QUERY) && (COMM_TYPE != FOCUS_QUERY) && (error == 0)) 
    commst->active = 1;

  return(error);
}

int stop_axis(int axis, int testmode)
{
  char outcom[100],mntresp[RESP_LEN];
  int statbits;

  /* first the easy part, sending the stop command */
  if (axis == 0) {
    sprintf(outcom,"$%s", comkeys[StopRA]);
  } else {
    sprintf(outcom,"$%s", comkeys[StopDec]);
  }
  appendCRC(outcom);

  if (comm2mount(outcom, mntresp, 0, testmode) != 0) {
    syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
    return(-1);
  }

  /* now, we have to check that it stopped */
  statbits = 0;
  if (get2stat(axis, &statbits, testmode) != 0) {
    syslog(LOG_ERR,"Error in get2stat(%d)",axis);
    return(-1);
  }
  
  /* and check the bits */
  if (getbit(statbits, bit(E_STOP)) != 0) {
    syslog(LOG_ERR,"E-stop bit set. Unable to Stop Axis %d", axis);
    return(-1);
  }
  if (getbit(statbits, bit(AMP_DISABLED)) != 0) {
    syslog(LOG_ERR,"Amplifier disabled bit set.  Unable to Stop Axis %d", axis);
    return(-1);
  }
  if (getbit(statbits, bit(BRAKE_ON)) != 0) {
    syslog(LOG_ERR, "Brake On bit set.  Unable to Stop Axis %d", axis);
    return(-1);
  }

  return(0);
}


int comm2focus(int getresp, char *com, float *fresp, int testmode) {
  fd_set readfds, writefds;
  int chk, maxfd;
  struct timeval tv;
  char cr, *endptr=NULL;
  char resp[RESP_LEN];
  float ftmp;
  /* int i; */

  if (getbit(testmode, bit(NO_FOCUS)) != 0) {
    *fresp = 0.0;
    return(0);
  }

  cr = 0x0d;

  *fresp = 0.0;

  /* Set timeout value */
  tv.tv_sec = 1;
  tv.tv_usec = 0;

  maxfd = focfd + 1;
  FD_ZERO(&writefds);
  FD_SET(focfd, &writefds);
  FD_ZERO(&readfds);

  chk = select(maxfd, NULL, &writefds, NULL, &tv);

  /* Need to check if timed out! */  
  if (chk > 0) {
    chk = write(focfd, com, strlen(com));
    chk = write(focfd, &cr, 1);
  } else {
    syslog(LOG_ERR,"Unable to write to serial port!");
    strcpy(resp, "");

    return(-1);
  }
  
  /* It only returns something if asked to */
  if (getresp == 1) {
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (mount_serial_read(focfd, &tv, resp, '\r', RESP_LEN, 1) == -1) {
      syslog(LOG_ERR,"Error reading from focus");
      return(-1);
    }
    
    ftmp = (float) strtod(resp, &endptr);
    if (*endptr != '\0' || (ftmp == 0.0 && endptr == resp)) {
      syslog(LOG_ERR, "1TP? return value %s invalid", resp);
      return (-1);
    }
    *fresp = ftmp;

    /*
    strcpy(resp,"");
    chk = 0;
    FD_ZERO(&writefds);
    FD_ZERO(&readfds);
    FD_SET(focfd, &readfds);
    chk = select(maxfd, &readfds, NULL, NULL, &tv);
    if (chk > 0) {
      strcpy(resp, "");
      i = 0;
      do {
	chk = read(focfd, &(resp[i]), 1);
	i++;
      } while (resp[i-1] != '\r');
      resp[i-1] = '\0'; 
  
      ftmp = (float) strtod(resp, &endptr);
      if (*endptr != '\0' || (ftmp == 0.0 && endptr == resp)) {
	syslog(LOG_ERR, "1TP? return value %s invalid", resp);
	return (-1);
      }
      *fresp = ftmp;
    } else {
      syslog(LOG_ERR, "No response from focus controller!");
      strcpy(resp,"");
    
      return(-1);
    }
    */
  }
  return(0);
}

int comm2mount(char *com, char *resp, int ntry, int testmode) {
  fd_set readfds, writefds;
  int chk;
  int maxfd;
  struct timeval tv;

  /* int i; */

  if (getbit(testmode, bit(NO_MOUNT)) != 0) {
    /* test mode-- return 0 */
    resp[0] = '\0';
    return(0);
  }

  /* Set timeout value */
  tv.tv_sec = 1;
  tv.tv_usec = 0;

  maxfd = mtfd + 1;
  FD_ZERO(&writefds);
  FD_SET(mtfd, &writefds);
  FD_ZERO(&readfds);

  chk = select(maxfd, NULL, &writefds, NULL, &tv);

  /* Need to check if timed out! */  
  if (chk > 0) {
    chk = write(mtfd, com, strlen(com));
  } else {
    syslog(LOG_ERR,"Unable to write to serial port!");
    strcpy(resp, "");

    return(-1);
  }

  tv.tv_sec = 1;
  tv.tv_usec = 0;
  if (mount_serial_read(mtfd, &tv, resp, '\r', RESP_LEN, 0) == -1) {
    syslog(LOG_ERR,"Error reading from mount.");
    if (ntry < MAXTRY) {
      syslog(LOG_ERR,"Error on response to: %s", com);
      if (comm2mount(com, resp, ntry+1, testmode) != 0) {
	syslog(LOG_ERR,"No response on resend.  Not good.");
	resp[0] = '\0';
	return(-1);
      }
    } else {
      syslog(LOG_ERR,"Too many no responses.  Check mount computer!");
      resp[0] = '\0';
      return(-1);
    }
  }
  /*
  strcpy(resp,"");
  chk = 0;
  FD_ZERO(&writefds);
  FD_ZERO(&readfds);
  FD_SET(mtfd, &readfds);
  chk = select(maxfd, &readfds, NULL, NULL, &tv);
  if (chk > 0) {
    strcpy(resp, "");
    i = 0;
    do {
      chk = read(mtfd, &(resp[i]), 1);
      i++;
    } while (resp[i-1] != '\r');
    resp[i] = '\0';
  } else {  
    if (ntry < MAXTRY) {
      syslog(LOG_ERR, "No response to: %s", com);
      if (comm2mount(com, resp, ntry + 1, testmode) != 0) {
	syslog(LOG_ERR,"No response on resend.  Not good.");
	strcpy(resp,"");
	return(-1);
      }
    } else {
      syslog(LOG_ERR,"Too many no responses.  Check mount computer!");
      strcpy(resp,"");
      return(-1);
    }
  }
  */
  chk = check_resp(com, resp);

  if (chk == -1) {
    if (ntry < MAXTRY) {
      syslog(LOG_ERR,"Clearing and Resending last command");
      clear_mount_comm();
      if ((chk = comm2mount(com,resp,ntry+1,testmode)) != 0) {
	syslog(LOG_ERR,"Problem on resend.  Giving up.");
	resp[0] = '\0';
	return(-1);
      }
    } else {
      syslog(LOG_ERR,"Too many tries.");
      resp[0] = '\0';
      return(-1);
    }
  }

  return(chk);
}

int get_last_fault(char *fault)
{
  fd_set readfds, writefds;
  int chk;
  int maxfd;
  struct timeval tv;
  char dummy;

  char command[100];

  /* int i; */

  /* make command string */
  sprintf(command,"$%s", comkeys[RecentFaults]);
  appendCRC(command);

  /* Set timeout value */
  tv.tv_sec = 3;
  tv.tv_usec = 0;

  maxfd = mtfd + 1;
  FD_ZERO(&writefds);
  FD_SET(mtfd, &writefds);
  FD_ZERO(&readfds);

  chk = select(maxfd, NULL, &writefds, NULL, &tv);

  if (chk > 0) {
    chk = write(mtfd, command, strlen(command));
  } else {
    syslog(LOG_ERR,"Unable to write to serial port!");
    strcpy(fault, "");
    return(-1);
  }

  tv.tv_sec = 3;
  tv.tv_usec = 0;
  if (mount_serial_read(mtfd, &tv, fault, ';', RESP_LEN, 0) == -1) {
    syslog(LOG_ERR,"Error reading fault from mount.");
    return(-1);
  }

  /* Now, clear the rest of the read buffer */
  FD_ZERO(&readfds);
  FD_SET(mtfd, &readfds);
  while(select(mtfd + 1, &readfds, NULL, NULL, &tv)) {
    read(mtfd, &dummy, 1);
  }
  

  /*
  strcpy(fault,"");
  chk = 0;
  FD_ZERO(&writefds);
  FD_ZERO(&readfds);
  FD_SET(mtfd, &readfds);
  chk = select(maxfd, &readfds, NULL, NULL, &tv);  
  if (chk > 0) {
    strcpy(fault, "");

    i = 0;
    do {
      chk = read(mtfd, &(fault[i]), 1);
      i++;
      } while (fault[i-1] != ';');*/
    /* Clear the rest of the read buffer */
  /*    while(select(maxfd, &readfds, NULL, NULL, &tv)) {
      read(mtfd, &dummy, 1);
    }
    fault[i] = '\0';
  } else {
    syslog(LOG_ERR,"No response from mount!");
    return(-1);
  }
  */
  return(0);
}

/* Check that the response is kosher */
int check_resp(char *command, char *response)
{
  /*  char *errorloc;*/
  int errorval;
  int len;
  unsigned short nCRC, inCRC_s;
  char inCRC[5], cut_resp[RESP_LEN];

  /* First check if the response CRC was bad */
  len = strlen(response);
  strncpy(cut_resp, response, len - 5);
  cut_resp[len - 5] = 0;

  nCRC = CalcCRC(cut_resp);

  strncpy(inCRC, &(response[len-5]), 4);
  inCRC[4] = 0;
  inCRC_s = (unsigned short) strtol(inCRC, NULL, 16);

  if (nCRC != inCRC_s) {
    syslog(LOG_ERR,"Bad CRC on Response: [%s], [%s], %d", response, inCRC, inCRC_s);
    return(-1);
  }
  errorval = 0;
  if (strstr(command,"RA") != NULL) {
    if (strstr(response,"RA") == NULL) {
      syslog(LOG_ERR,"shite: %s responded %s", command, response);
      errorval = -1;
    }
  } else if (strstr(command,"Dec") != NULL) {
    if (strstr(response,"Dec") == NULL) {
      syslog(LOG_ERR,"shite: %s responded %s", command, response);
      errorval = -1;
    }
  }
  /*
  if ((errorloc = strstr(response, "Error")) == NULL) {
    errorval = 0;
  } else {
    errorval = -2;
    }*/

  return(errorval);

}

int get1stat(int axis, int *apos, int *cpos, int testmode) {
  char outcom[100], comres[RESP_LEN];
  /*int prob=0;*/

  if (axis == 0) 
    sprintf(outcom, "$%s", comkeys[Status1RA]);
  else 
    sprintf(outcom, "$%s", comkeys[Status1Dec]);
  appendCRC(outcom);

  if (getbit(testmode, bit(NO_MOUNT)) != 0) {
    /* simulate a response */
    sprintf(comres, "@Status1RA, 0000, 0000, 0000\r");
  } else {
    if (comm2mount(outcom, comres, 0, testmode) == -1) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, comres);
      return(-1);
    }
  }

  /*syslog(LOG_INFO,"about to parse a response1");*/
  return parsestat1(axis, comres, apos, cpos);
  /*  syslog(LOG_INFO,"parsed a response");*/

  /*  return(prob);*/
}


int parsestat1(int axis, char *resp, int *a, int *c)
{
  /* this function should strip out the target and current 
     encoder positions from the mount response string */
  int n,i;
  char stat1[20], stat2[20], *endp=NULL;

  n = 11+axis;
  i = 0;

  while ((resp[n+i] != ',') && (i<20)) i++;

  if (i < 20) {
    strncpy(stat1, resp+n, i);
    stat1[i] = '\0';
    *c = (int) strtol(stat1, &endp, 10);
  } else {
    return(-1);
  }


  n += i + 2;
  i = 0;
  
  while ((resp[n+i] != ' ') && (i<20)) i++;

  if (i < 20) {
    strncpy(stat2, resp+n, i);
    stat2[i] = '\0';
    *a = (int) strtol(stat2, &endp, 10);
  } else {
    return(-1);
  }
  return(0);
}

int get2stat(int axis, int *statres, int testmode) {
  char outcom[100], comres[RESP_LEN];
  /*  int prob=0;*/

  if (axis == 0) 
    sprintf(outcom, "$%s", comkeys[Status2RA]);
  else 
    sprintf(outcom, "$%s", comkeys[Status2Dec]);
  appendCRC(outcom);

  if (getbit(testmode, bit(NO_MOUNT)) != 0) {
    /* simulate a response */
    sprintf(comres,"@Status2RA, 0000, 0000\r");
  } else {
    if (comm2mount(outcom, comres, 0, testmode) == -1) {
      syslog(LOG_ERR,"Error: command %s responded %s", outcom, comres);
      return(-1);
    }
  }

  /* syslog(LOG_INFO,"about to parse a response2");*/
  return parsestat2(axis, comres, &statres[axis]);
  /*syslog(LOG_INFO,"parsed a response2");*/

  /*  return(prob);*/
}

int parsestat2(int axis, char *resp, int *bitn)
{
  /* this function should strip out the axis
     status from the mount response string */

  int n,i;
  char stat2[20], *endp=NULL;
  int word1, word2;

  n = 0;
  i = 0;

  while ((resp[n+i] != ',') && (i<20)) i++;
  n += i + 2;
  i = 0;
  while ((resp[n+i] != ',') && (i<20)) i++;


  if (i < 20) {
    strncpy(stat2, resp+n-6, 4);
    stat2[4] = '\0';
    word1 = (int) strtol(stat2, &endp, 16);

    strncpy(stat2, resp+n, i);
    stat2[i] = '\0';
    //    *bitn = (int) strtol(stat2, &endp, 16);
    word2 = (int) strtol(stat2, &endp, 16);

    /* set the local bit information from the two words */
    *bitn = 0;
    if (getbit(word1,bit(SCHIER_ESTOP)) != 0) setbits(*bitn, bit(E_STOP));
    if (getbit(word1,bit(SCHIER_NLIM)) != 0) setbits(*bitn, bit(NEG_LIM));
    if (getbit(word1,bit(SCHIER_PLIM)) != 0) setbits(*bitn, bit(POS_LIM));
    if (getbit(word2,bit(SCHIER_BRAKE)) != 0) setbits(*bitn, bit(BRAKE_ON));
    if (getbit(word2,bit(SCHIER_AMPDIS)) != 0) setbits(*bitn, bit(AMP_DISABLED));

    if (*bitn > 0) {
      //      syslog(LOG_INFO,"Non-0 when resp = %s", resp);
      syslog(LOG_INFO,"Error bit = %d when resp = %s", *bitn, resp);
    }
  } else {
    return(-1);
  }
  return(0);
}

int get3stat(int axis, float *voltage, float *integrator, int testmode)
{
  char outcom[100], comres[RESP_LEN];
  
  if (axis == 0) {
    sprintf(outcom, "$%s", comkeys[Status3RA]);
  } else {
    sprintf(outcom, "$%s", comkeys[Status3Dec]);
  }
  appendCRC(outcom);

  if (getbit(testmode, bit(NO_MOUNT)) != 0) {
    if (axis == 0) {
      sprintf(comres, "@Status3RA, 0000, 0000 \r");
    } else {
      sprintf(comres, "@Status3Dec, 0000, 0000 \r");
    }
  } else {
    if (comm2mount(outcom, comres, 0, testmode) == -1) {
      syslog(LOG_ERR,"Error: command %s responded %s", outcom, comres);
      return(-1);
    }
  }
  //syslog(LOG_INFO,"%s", comres);
  return parsestat3(axis, comres, voltage, integrator);

  /*  return(0);*/
}

int parsestat3(int axis, char *resp, float *vo, float *in)
{
  int n,i;
  char stat1[20], stat2[20], *endp=NULL;

  n=11+axis;
  i=0;

  while ((resp[n+i] != ',') && (i<20)) i++;

  if (i<20) {
    strncpy(stat1,resp+n,i);
    stat1[i] = '\0';
    *vo = (float) strtod(stat1, &endp);
  } else {
    syslog(LOG_ERR,"No comma: %d (%s)", i, resp);
    return(-1);
  }

  n += i + 2;
  i = 0;

  while ((resp[n+i] != ' ') && (i<20)) i++;

  if (i<20) {
    strncpy(stat2, resp+n, i);
    stat2[i] = ' ';
    *in = (float) strtod(stat2, &endp);
  } else {
    syslog(LOG_ERR,"No term: %d (%s)", i, resp);
    return(-1);
  }
  return(0);
}


int evalstat(int ap[2][2], int cp[2][2], struct mountd_par *commst, struct mountd_cfg *cfg)
{
  int mntst=MOUNT_IDLE, ax;
  char fault[RESP_LEN];
  char *aname[] = AXIS_NAMES;
  static int stop_ctr[2] = {0,0};
  int okay = 1;
  int tol;

  tol = cfg->enctol;

  /* loop through both axes */
  for (ax=0; ax<2; ax++) {
    limit_status[ax] = 0;
    if ((mntst != MOUNT_ERROR) && (mntst != MOUNT_ERROR_SHUTDOWN)) {
      if (cp[1][ax] != cp[0][ax]) {
	/* axis is moving */
	if (mntst >=0) mntst = commst->move_mode;
	stop_ctr[ax]=0;
      } else {
	if (abs(ap[1][ax] - ap[0][ax]) < tol) {
	  /* Check if target was acquired in a slew */
	  if ((commst->active) &&
	      ((commst->move_mode == MOUNT_SLEW) || (commst->move_mode == MOUNT_SHIFT))) {
	    if (abs(ap[1][ax] - commst->encpos[ax]) > tol) {
	      /* We might be off-target.  If we're closing in, merely increment stop counter */
	      stop_ctr[ax]++;
	      okay = 1;
	      if ((stop_ctr[ax] >= MAX_STOP_COUNT) || !okay) {
		syslog(LOG_ERR,"Mount off-target on axis %d: %s Axis", ax, aname[ax]);
		/* extra stuff */
		syslog(LOG_INFO,"ap[0][ax] = %d, ap[1][ax] = %d", ap[0][ax], ap[1][ax]);
		syslog(LOG_INFO,"cp[0][ax] = %d, cp[1][ax] = %d", cp[0][ax], cp[1][ax]);
		syslog(LOG_INFO,"encpos: %d", commst->encpos[ax]);
		mntst = MOUNT_ERROR; /* If mount is stopped off-target, report error */
		stop_ctr[ax] = 0;  /* reset stop counter */
	      }
	    } else {
	      stop_ctr[ax] = 0;
	    }
	  }
	} //else {
	  /* ap[1] - ap[0] > tol */
	  /* this means it is still moving */
	//  if (mntst >= 0) {
	//    mntst = commst->move_mode;
	//    syslog(LOG_INFO,"still moving...");
	//  }
	  //	  stop_ctr[ax] = 0;
	//	}
      }
      /* Check if any error bits are set */
      if (commst->statbits[ax] > 0) {
	if (ax == 0) 
	  syslog(LOG_ERR,"Error bit %d set on RA axis (%d)", commst->statbits[ax], ax);
	else syslog(LOG_ERR,"Error bit %d set on Dec axis (%d)", commst->statbits[ax], ax);
	if (getbit(commst->statbits[ax], bit(POS_LIM)) != 0) {
	  syslog(LOG_ERR,"Positive Limit bit set.");
	  setbits(limit_status[ax], bit(POS_LIM));
	  if (commst->move_mode != MOUNT_SYNC) {
	    mntst = MOUNT_ERROR;
	  }
	}
	if (getbit(commst->statbits[ax], bit(NEG_LIM)) != 0) {
	  syslog(LOG_ERR,"Negative Limit bit set.");
	  setbits(limit_status[ax], bit(NEG_LIM));
	  if (commst->move_mode != MOUNT_SYNC) {
	    mntst = MOUNT_ERROR;
	  }
	}
	if (getbit(commst->statbits[ax], bit(E_STOP)) != 0) {
	  syslog(LOG_ERR,"E-stop bit set.  Unrecoverable.");
	  mntst = MOUNT_ERROR_SHUTDOWN;
	}
	if (getbit(commst->statbits[ax], bit(AMP_DISABLED)) != 0) {
	  syslog(LOG_ERR,"Amplifier disabled bit set.");
	  mntst = MOUNT_ERROR;
	}
	if (getbit(commst->statbits[ax], bit(BRAKE_ON)) != 0) {
	  syslog(LOG_ERR,"Brake on bit set.");
	  mntst = MOUNT_ERROR;
	}

	if (get_last_fault(fault) == 0) {
	  if (strstr(fault,"Axis 1") != NULL) {
	    syslog(LOG_ERR,"Fault reported on RA Axis:");
	  } else if (strstr(fault, "Axis 2") != NULL) {
	    syslog(LOG_ERR,"Fault reported on Dec Axis:");
	  }
	  syslog(LOG_ERR,"%s", fault);

	  if (strstr(fault, "High Output I^2") != NULL) {
	    syslog(LOG_ERR,"Unrecoverable Error.");
	    mntst = MOUNT_ERROR_SHUTDOWN;
	  }
	}
      }
      /*
      if (commst->statbits[ax] > 0) {
	mntst = MOUNT_ERROR;
	syslog(LOG_ERR,"error bit %d set on axis %d: %s Axis", 
	       commst->statbits[ax], ax, aname[ax]);
	if (get_last_fault(fault) == 0) {
	  if (strstr(fault, "Axis 1") != NULL) {
	    syslog(LOG_ERR,"Error reported on RA Axis:");
	  } else if (strstr(fault, "Axis 2") != NULL) {
	    syslog(LOG_ERR,"Error reported on Dec Axis:");
	  }
	  syslog(LOG_ERR,"%s",fault);

	  if (strstr(fault, "High Output I^2") != NULL) {
	    syslog(LOG_ERR,"Unrecoverable Error.");
	    mntst = MOUNT_ERROR_SHUTDOWN;
	  }
	}
      }
      */
    }
  }
  /*  if (logit) syslog(LOG_INFO,"mntst = %d",mntst);*/
  return(mntst);
}

int mount_move(int *pos, float *fvel, struct mountd_cfg *cnf, int stopmount)
{
  int err = 0;
  char outcom[100], mntresp[RESP_LEN];

  /* Stop the mount if requested. */
  if (stopmount) {
    sprintf(outcom, "$%s, 0", comkeys[VelRa]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, cnf->testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s, 0", comkeys[VelDec]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, cnf->testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
  }

  /* Check if the position is kosher */
  if ((pos[0] > ((int) (cnf->rarange[1]*cnf->deg2enc[0]) + cnf->zeropt[0] + cnf->ptg_offset[0])) ||
      (pos[0] < ((int) (cnf->rarange[0]*cnf->deg2enc[0]) + cnf->zeropt[0] + cnf->ptg_offset[0]))) {
    syslog(LOG_ERR,"RA position out of range!");
    err = -2;
  }
  if ((pos[1] > ((int) (cnf->decrange[1]*cnf->deg2enc[1]) + cnf->zeropt[1] + cnf->ptg_offset[1])) ||
      (pos[1] < ((int) (cnf->decrange[0]*cnf->deg2enc[1]) + cnf->zeropt[1] + cnf->ptg_offset[1]))) {
    syslog(LOG_ERR,"Dec position out of range!");
    err = -2;
  }
  if (err) return(err);
  /* syslog(LOG_INFO,"Sending mount to %d, %d", pos[0], pos[1]);*/
  rlog(VERBOSE, cnf->logfd,"Sending mount to %d, %d", pos[0], pos[1]);
  /*gettimeofday(&now_tv, NULL);
    rlog(TERSE, cnf->logfd,"%s + %d: Sending mount to %d, %d\n", ctime(&now_tv.tv_sec), now_tv.tv_usec, pos[0], pos[1]);*/
  /*logtime(1,"Sending mount off...");*/
  /*if (getbit(cnf->testmode, bit(NO_MOUNT)) == 0) {*/
    /*  if (!cnf->testmode) {*/
    sprintf(outcom, "$%s, %d", comkeys[PosRA], pos[0]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, cnf->testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s, %d", comkeys[PosDec], pos[1]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, cnf->testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    /* } else {
    syslog(LOG_INFO,"In mount testmode");
    }*/
  /*syslog(LOG_INFO,"Setting speed to %f, %f", fvel[0], fvel[1]);*/
    /* DANGER */
    /*fvel[0] = fvel[0] * 2.0;
      fvel[1] = fvel[1] * 2.0;*/
    rlog(TERSE, cnf->logfd, "Setting speed to %f, %f\n", fvel[0], fvel[1]);
  /*if (getbit(cnf->testmode, bit(NO_MOUNT)) == 0) {*/
  /*if (!cnf->testmode) {*/
    sprintf(outcom, "$%s, %f", comkeys[VelRa], fvel[0]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, cnf->testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    sprintf(outcom, "$%s, %f", comkeys[VelDec], fvel[1]);
    appendCRC(outcom);
    if (comm2mount(outcom, mntresp, 0, cnf->testmode) != 0) {
      syslog(LOG_ERR,"Error: Command %s responded %s", outcom, mntresp);
      return(-1);
    }
    /*} else {
    syslog(LOG_INFO,"In mount testmode");
    }*/
  return(0);
}


int clear_mount_comm()
{
  fd_set writefds;
  struct timeval tv;
  int chk = 0;
  char dummy;
  char resp[RESP_LEN];

  tv.tv_sec = 1;
  tv.tv_usec = 0;

  if (mount_serial_read(mtfd, &tv, resp, '\r', RESP_LEN, 0) == -1) {
    syslog(LOG_ERR,"Error on read: non-fatal.");
  }


  /*
  maxfd = mtfd + 1;
  FD_ZERO(&readfds);
  FD_SET(mtfd, &readfds);

  syslog(LOG_INFO,"Clearing mount command...");

  chk = select(maxfd, &readfds, NULL, NULL, &tv);
  if (chk > 0) {
    syslog(LOG_INFO,"Found something to read!");
    do {
      chk = read(mtfd, &dummy, 1);
    } while (dummy != '\r');
  }
  */

  chk = 0;

  FD_ZERO(&writefds);
  FD_SET(mtfd, &writefds);

  chk = select(mtfd+1, NULL, &writefds, NULL, &tv);
  if (chk > 0) {
    dummy = '\r';
    syslog(LOG_INFO,"Sending carriage return");
    chk = write(mtfd, &dummy, 1);
  }


  tv.tv_sec = 2;
  tv.tv_usec = 0;

  if (mount_serial_read(mtfd, &tv, resp, '\r', RESP_LEN, 0) == -1) {
    syslog(LOG_ERR,"Error on read: non-fatal.");
  }

  /*
  chk = 0;

  FD_ZERO(&readfds);
  FD_SET(mtfd, &readfds);

  chk = select(maxfd, &readfds, NULL, NULL, &tv);
  if (chk > 0) {
    syslog(LOG_INFO,"Something to read (after return)");
    do {
      chk = read(mtfd, &dummy, 1);
    } while (dummy != '\r');
  }
  */

  return(0);

}

int evalfocus(float fp[2], struct mountd_par *commst, float tol)
{
  float delta;
  int retval = MOUNT_IDLE;
  static int stop_ctr = 0;
  static int start_ctr = 0;
  static int min_start = 1;

  delta = fabs(fp[1] - fp[0]);

  if ((delta > tol) || (start_ctr < min_start)) {
    /* focus is moving--or give it a small chance to start moving */
    start_ctr++;
    retval = commst->move_mode;
  } else if (start_ctr >= min_start) {
    /* focus is not moving...*/
    if (commst->move_mode == FOCUS_MOVE) {
      /* can't do this check for FOCUS_SYNC...always off target! (-100) */
      if (fabs(fp[1] - commst->foc) > tol) {
	/* Not moving, but off target */
	stop_ctr++;
	if (stop_ctr >= MAX_STOP_COUNT) {
	  syslog(LOG_ERR,"Focus stopped at %.3f, target was %.3f", fp[1], commst->foc);
	  min_start = 10;  /* next move will take a while to start */
	  stop_ctr = start_ctr = 0;
	  retval = MOUNT_IDLE;
	} else {
	  /* still has a chance of getting there */
	  retval = commst->move_mode;
	}
      } else {
	/* on target */
	start_ctr = stop_ctr = 0;
	min_start = 1;
	retval = MOUNT_IDLE;
      }
    } else if (commst->move_mode == FOCUS_SYNC) {
      start_ctr = stop_ctr = 0;
      min_start = 1;
      retval = MOUNT_IDLE;
    } else {
      retval = MOUNT_IDLE;
    }
  }

  return(retval);
}

int cancel_move(struct mountd_par *commst, struct mountd_st *st, double old_ra, double old_dec)
{
  int i, junk;

  syslog(LOG_INFO, "Illegal RA/Dec (%.2f, %.2f).  Cancelling Move.",
	 commst->ra, commst->dec);

  zerobits(st->state, bit(MOVE));   /* NO???? */
  st->data.ra = old_ra;
  st->data.dec = old_dec;
  /* set the target position to the current position */
  for (i=0; i<2; i++) {
    if (get1stat(i, &junk, &(commst->encpos[i]), st->conf.testmode)) {
      syslog(LOG_ERR,"Error in get1stat()");
      return(-1);
    }
  }
  return(0);
}

int mount_serial_read(int fd, struct timeval *tv, char *resp, char endch, 
		      int maxlen, int chomp)
{
  int i = 0;
  int chk;
  fd_set readfds;

  resp[0] = '\0';

  do {
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    chk = select(fd + 1, &readfds, NULL, NULL, tv);
    if (chk > 0) {
      read(fd, &(resp[i]), 1);
      i++;
    } else {
      syslog(LOG_ERR,"Timeout on serial read (read [%s])",resp);
      resp[0] = '\0';
      return(-1);
    }
  } while ((resp[i-1] != endch) && (i < maxlen));

  if (i == maxlen) {
    syslog(LOG_ERR,"No termination character in response (read [%s])", resp);
    resp[0] = '\0';
    return(-1);
  }

  if (chomp) {
    resp[i-1] = '\0';/* get rid of the \r */
  } else {
    resp[i] = '\0';
  }

  return(0);
}

