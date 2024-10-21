/* mountd_conf ==============================================================

 * Purpose: read and apply config file information.
 *
 *      Config file is parsed by gettokens() and keyword values are
 * obtained from getkey().  There are no defaults. String valued
 * variables are initialize to NULL. Integer variables are initialized to 
 * -1 and floating point variables are initialized to a large negative number 
 * (below). This is to allow for error checking after the config file is read.
 * The initialization takes place just before reading config file.
 *
 * Inputs:
 *    argc  int                 number of command-line tokens
 *    argv  char **             array of command-line tokens (see 'main')
 *    struct mountd_cfg *cfg    config. parameters
 * 
 * Outputs:
 *
 * Updated: 2001-05-02  Don Smith  --  (from /rotse/torusd/mountd_conf.c)
 * Updated: 2002-04-02  E. Rykoff -- added many more config vars, including pointing
 *                                    models and focus models
 * Updated: 2003-02-05  E. Rykoff -- added focus range
 * Updated: 2004-09-13  E. Rykoff -- added focus_update file
 * Updated: 2004-10-13  E. Rykoff -- added mount_run option
 * Updated: 2005-01-28  E. Rykoff -- added overspeed
 ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <values.h>
#include <syslog.h>

/* project includes */

#include <rotse.h>
#include <util.h>
#include <mountd_str.h>
#include "protos.h"
#include "schierd.h"

#define MAXTOKENS       3	/* max tokens on one line in config file */
#define MAXLINE         1024	/* maximum line length */

int mountd_conf(int argc, char **argv, struct mountd_cfg *cfg, int reread)
{
   int c,i,j;
   int keyval;
   int ntok;
   char line[MAXLINE];
   char *(tokens[MAXTOKENS]);
   FILE *fp, *mfp;
   char *endptr = NULL;
   int itmp = 0;
   float ftmp = NANVAL;
   int fnd_matfile = 0;
   int fnd_modfile = 0;

   /* Externs for getopt */

   extern char *optarg;
   extern int optind, optopt;
   extern int opterr;

   /* config entry mapping to integers, 0,1,...
    * C_NKEYS must be last. */

   enum {
      C_LOGLEVEL, C_LOGFILE, C_SMPLTIME, C_POLLTIME, C_ERR_TOUT, C_MNTMAN, 
      C_MNTMODEL, C_MNTSN, C_ENCTOL, C_FOCTOL, C_ERMAIL, C_SACC, C_MVEL, 
      C_TVEL, C_STATDIR, C_STATROOT, C_FOCUS_UPDATE,
      C_HVEL, C_STOW, C_STAND, C_ENCDEG, C_MATFILE, C_MODFILE, C_FOCUSMODFILE,
      C_RARANGE, C_DECRANGE, C_FOCRANGE, C_OVERSPEED, C_OBSFILE, C_TESTMODE, 
      C_MOUNT_RUN, C_NKEYS
   };

   /* Keyword table. Each element has config file keyword string
    * as first entry, keyword value as second entry. The table is
    * used by getkey() to get switch value below.  */

   static struct keyword keytable[C_NKEYS] =
   {
      {"loglevel", C_LOGLEVEL},
      {"logfile", C_LOGFILE},
      {"sample_time", C_SMPLTIME},
      {"poll_time", C_POLLTIME},
      {"err_tout", C_ERR_TOUT},
      {"mntman", C_MNTMAN},
      {"mntmodel", C_MNTMODEL},
      {"mntsn", C_MNTSN},
      {"enctol", C_ENCTOL},
      {"foctol", C_FOCTOL},
      {"errormail", C_ERMAIL},
      {"slewacc", C_SACC},
      {"maxvel", C_MVEL},
      {"trackvel", C_TVEL},
      {"homevel", C_HVEL},
      {"stowpos", C_STOW},
      {"standbypos", C_STAND},
      {"deg2enc", C_ENCDEG},
      {"matfile", C_MATFILE},
      {"modfile", C_MODFILE},
      {"focusmodfile", C_FOCUSMODFILE},
      {"rarange", C_RARANGE},
      {"decrange", C_DECRANGE},
      {"focrange", C_FOCRANGE},
      {"overspeed", C_OVERSPEED},
      {"obsfile", C_OBSFILE},
      {"statdir", C_STATDIR},
      {"statroot", C_STATROOT},
      {"focus_update", C_FOCUS_UPDATE},
      {"testmode", C_TESTMODE},
      {"mount_run", C_MOUNT_RUN}
   };
/**** end defining structure of configuration file. *****/

   /* Set required values to allow error checking. */

   if (!reread) {

     cfg->confile[0] = '\0';
     cfg->ipc_key = -1;
     cfg->pgid = -1;
     
     /* Evaluate main's command line */
     
     opterr = 0;			/* prevents getopt from using stdout */
     while ((c = getopt(argc, argv, "f:k:g:")) != -1) {
       switch (c) {
       case 'f':		/* get config file name */
	 if (strncpy(cfg->confile, optarg, MAXFILELEN) == NULL) {
	   syslog(LOG_ERR,
		  "-f option failed, could not copy %s: %m", optarg);
	   return (-1);
	 }
	 break;
       case 'k':		/* get ipc key value */
	 itmp = (int) strtol(optarg, &endptr, 0);
	 if (*endptr != '\0') {
	   syslog(LOG_ERR, "ipc key value %s not valid", optarg);
	   return (-1);
	 }
	 cfg->ipc_key = itmp;
	 itmp = 0;
	 break;
       case 'g':		/* get process group id */
	 itmp = (int) strtol(optarg, &endptr, 0);
	 if (*endptr != '\0') {
	   syslog(LOG_ERR, "ipc key value %s not valid", optarg);
	   return (-1);
	 }
	 cfg->pgid = itmp;
	 itmp = 0;
	 break;
       default:
	 syslog(LOG_ERR, "unrecognized option: %c", c);
	 return (-1);
       }
     }
     /* Check that required params were set. */

     if (cfg->confile[0] == '\0') {
       syslog(LOG_ERR, "error: config file not set on command line");
       return (-1);
     }
     if (cfg->ipc_key == -1) {
       syslog(LOG_ERR, "error: ipc_key not set on command line");
       return (-1);
     }
     if (cfg->pgid == -1) {
       syslog(LOG_ERR, "error: pgid not set on command line");
       return (-1);
     }

   }
   /* Read config file and set parameters, filenames, etc.
    * First set initial values to allow error checking.
    * Then set values from config file.
    * Finally check that values were set properly. */

   cfg->loglevel = -1;
   cfg->logfile[0] = '\0';
   cfg->poll_time = NANVAL;
   cfg->sample_time = NANVAL;
   cfg->err_tout = NANVAL;
   cfg->mntman[0] = '\0';
   cfg->mntmodel[0] = '\0';
   cfg->mntsn = -1;
   cfg->enctol = 0;
   cfg->erroremail[0] = '\0';
   cfg->matfile[0] = '\0';
   cfg->modfile[0] = '\0';
   cfg->focfile[0] = '\0';
   cfg->statdir[0] = '\0';
   cfg->statroot[0] = '\0';
   cfg->focus_update[0] = '\0';
   cfg->rarange[0] = cfg->rarange[1] = NANVAL;
   cfg->decrange[0] = cfg->decrange[1] = NANVAL;
   cfg->focrange[0] = cfg->focrange[1] = NANVAL;
   cfg->overspeed = NANVAL;
   cfg->poleoff = -99.99;
   cfg->longitude = NANVAL;
   cfg->latitude = NANVAL;
   cfg->altitude = NANVAL;
   cfg->foctol = -99.99;
   cfg->testmode = 0;
   cfg->mount_run = 0;
   for (i=0;i<2;i++) {
     cfg->slw_acc[i] = -99.99;
     cfg->max_vel[i] = -99.99;
     cfg->home_vel[i] = -99.99;
     cfg->stowpos[i] = -99.99;
     cfg->standbypos[i] = NANVAL;
     cfg->deg2enc[1] = -99.99;
   }
   for (i=0; i<3; i++)
     for (j=0; j<3; j++)
       cfg->coomat[i][j] = 0.0;
   
   if ((fp = fopen(cfg->confile, "r")) == NULL) {
      syslog(LOG_ERR, "could not open %s: %m", cfg->confile);
      return (-1);
   }
   syslog(LOG_INFO, "using config file %s", cfg->confile);
   while ((ntok = gettokens(fp, line, MAXLINE, tokens, MAXTOKENS)) != 0) {
      switch (keyval = getkey(tokens[0], keytable, C_NKEYS)) {
      case C_LOGLEVEL:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "loglevel entry format error");
	    return (-1);
	 }
	 itmp = (int) strtol(tokens[1], &endptr, 0);
	 if (*endptr != '\0' || itmp < 0 || itmp > LOGMAX) {
	    syslog(LOG_ERR, "loglevel value %s not valid", tokens[1]);
	    return (-1);
	 }
	 cfg->loglevel = itmp;
	 itmp = 0;
	 break;
      case C_LOGFILE:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "logfile entry format error");
	    return (-1);
	 }
	 if (strncpy(cfg->logfile, tokens[1], MAXFILELEN) == NULL) {
	    syslog(LOG_ERR, "copy of %s failed: %m", tokens[1]);
	    return (-1);
	 }
	 break;
      case C_SMPLTIME:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "logfile entry format error");
	    return (-1);
	 }
	 ftmp = (float) strtod(tokens[1], &endptr);
	 if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[1])) {
	    syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[1]);
	    return (-1);
	 }
	 cfg->sample_time = ftmp;
	 ftmp = NANVAL;
	 endptr = NULL;
	 break;
      case C_POLLTIME:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "logfile entry format error");
	    return (-1);
	 }
	 ftmp = (float) strtod(tokens[1], &endptr);
	 if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[1])) {
	    syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[1]);
	    return (-1);
	 }
	 cfg->poll_time = ftmp;
	 ftmp = NANVAL;
	 endptr = NULL;
	 break;
      case C_ERR_TOUT:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "err_tout entry format error");
	    return (-1);
	 }
	 ftmp = (float) strtod(tokens[1], &endptr);
	 if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[1])) {
	    syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[1]);
	    return (-1);
	 }
	 cfg->err_tout = ftmp;
	 ftmp = NANVAL;
	 endptr = NULL;
	 break;
      case C_MNTMAN:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "mntman entry format error");
	    return (-1);
	 }
	 if (strncpy(cfg->mntman, tokens[1], HARDWARE_LEN) == NULL) {
	    syslog(LOG_ERR, "copy of %s failed: %m", tokens[1]);
	    return (-1);
	 }
	 break;
      case C_MNTMODEL:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "mntmodel entry format error");
	    return (-1);
	 }
	 if (strncpy(cfg->mntmodel, tokens[1], HARDWARE_LEN) == NULL) {
	    syslog(LOG_ERR, "copy of %s failed: %m", tokens[1]);
	    return (-1);
	 }
	 break;
      case C_STATDIR:
	if (ntok != 2) {
	  syslog(LOG_ERR,"%s entry format error", tokens[0]);
	  return(-1);
	}
	if (strncpy(cfg->statdir, tokens[1], MAXFILELEN) == NULL) {
	  syslog(LOG_ERR,"Copy of %s failed: %m", tokens[1]);
	  return(-1);
	}
	break;
      case C_STATROOT:
	if (ntok != 2) {
	  syslog(LOG_ERR,"%s entry format error", tokens[0]);
	  return(-1);
	}
	if (strncpy(cfg->statroot, tokens[1], MAXFILELEN) == NULL) {
	  syslog(LOG_ERR,"Copy of %s failed: %m", tokens[1]);
	  return(-1);
	}
	break;
      case C_FOCUS_UPDATE:
	if (ntok != 2) {
	  syslog(LOG_ERR,"%s entry format error", tokens[0]);
	  return(-1);
	}
	if (strncpy(cfg->focus_update, tokens[1], MAXFILELEN) == NULL) {
	  syslog(LOG_ERR,"Copy of %s failed: %m", tokens[1]);
	  return(-1);
	}
	break;
      case C_MNTSN:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "mntsn entry format error");
	    return (-1);
	 }
	 itmp = (int) strtol(tokens[1], &endptr, 0);
	 if (*endptr != '\0' || itmp < 0 || itmp > 100) {
	    syslog(LOG_ERR, "mntsn value %s not valid", tokens[1]);
	    return (-1);
	 }
	 cfg->mntsn = itmp;
	 itmp = 0;
	 break;
      case C_ENCTOL:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "enctol entry format error");
	    return (-1);
	 }
	 itmp = (int) strtol(tokens[1], &endptr, 0);
	 if (*endptr != '\0' || itmp < 0 || itmp > MAXENC) {
	    syslog(LOG_ERR, "enctol value %s not valid", tokens[1]);
	    return (-1);
	 }
	 cfg->enctol = itmp;
	 itmp = 0;
	break;
      case C_FOCTOL:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "foctol entry format error");
	    return (-1);
	 }
	 ftmp = (float) strtod(tokens[1], &endptr);
	 if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[1])) {
	    syslog(LOG_ERR, "foctol value %s not valid", tokens[1]);
	    return (-1);
	 }
	 cfg->foctol = ftmp;
	 ftmp = 0.0;
	break;
      case C_ERMAIL:
	if (ntok != 2) {
	  syslog(LOG_ERR, "erroremail entry format error");
	  return (-1);
	}
	if (strncpy(cfg->erroremail, tokens[1], MAXFILELEN) == NULL) {
	  syslog(LOG_ERR, "copy of %s failed: %m", tokens[1]);
	  return (-1);
	}
	break;
      case C_SACC:
	if (ntok != 3) {
	  syslog(LOG_ERR, "slew acc entry format error");
	  return (-1);
	}
	ftmp = (float) strtod(tokens[1], &endptr);
	if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[1])) {
	  syslog(LOG_ERR, "RA slew acc value %s not valid", tokens[1]);
	  return (-1);
	}
	cfg->slw_acc[0] = ftmp;
	ftmp = (float) strtod(tokens[2], &endptr);
	if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[2])) {
	  syslog(LOG_ERR, "DEC slew acc value %s not valid", tokens[2]);
	  return (-1);
	}
	cfg->slw_acc[1] = ftmp;
	ftmp = 0.0;
	break;
      case C_MVEL:
	if (ntok != 3) {
	  syslog(LOG_ERR, "slew vel entry format error");
	  return (-1);
	}
	ftmp = (float) strtod(tokens[1], &endptr);
	if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[1])) {
	  syslog(LOG_ERR, "RA slew vel value %s not valid", tokens[1]);
	  return (-1);
	}
	cfg->max_vel[0] = ftmp;
	ftmp = (float) strtod(tokens[2], &endptr);
	if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[2])) {
	  syslog(LOG_ERR, "DEC slew vel value %s not valid", tokens[2]);
	  return (-1);
	}
	cfg->max_vel[1] = ftmp;
	ftmp = 0.0;
	break;
     case C_HVEL:
	if (ntok != 3) {
	  syslog(LOG_ERR, "homing vel entry format error");
	  return (-1);
	}
	ftmp = (float) strtod(tokens[1], &endptr);
	if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[1])) {
	  syslog(LOG_ERR, "RA homing vel value %s not valid", tokens[1]);
	  return (-1);
	}
	cfg->home_vel[0] = ftmp;
	ftmp = (float) strtod(tokens[2], &endptr);
	if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[2])) {
	  syslog(LOG_ERR, "DEC homing vel value %s not valid", tokens[2]);
	  return (-1);
	}
	cfg->home_vel[1] = ftmp;
	ftmp = 0.0;
	break;
      case C_ENCDEG:
	if (ntok != 3) {
	  syslog(LOG_ERR, "deg to encoder entry format error");
	  return (-1);
	}
	

	if (isnan(cfg->deg2enc[0] = (double) get_float_val(tokens[1], 0.0, MAXENC))) {
	  syslog(LOG_ERR,"%s value %s invalid", tokens[0], tokens[1]);
	  return(-1);
	}
	if (isnan(cfg->deg2enc[1] = (double) get_float_val(tokens[2], 0.0, MAXENC))) {
	  syslog(LOG_ERR,"%s value %s invalid", tokens[0], tokens[1]);
	  return(-1);
	}
	itmp = 0;
	break;
      case C_STOW:
	 if (ntok != 3) {
	    syslog(LOG_ERR, "stow position entry format error");
	    return (-1);
	 }
	 ftmp = (float) strtod(tokens[1], &endptr);
	 if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[1])) {
	   syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[1]);
	   return (-1);
	 }
	 cfg->stowpos[0] = ftmp;
	 ftmp = NANVAL;
	 endptr = NULL;
	 ftmp = (float) strtod(tokens[2], &endptr);
	 if (*endptr != '\0' || (ftmp == 0.0 && endptr == tokens[1])) {
	   syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[2]);
	   return (-1);
	 }
	 cfg->stowpos[1] = ftmp;
	 ftmp = NANVAL;
	 endptr = NULL;
	 /* for (i=0; i<2; i++) gstow[i] = cfg->stowpos[i];*/
	 break;
      case C_STAND:
	if (ntok != 3) {
	  syslog(LOG_ERR, "standbypos entry format error");
	  return(-1);
	}
	if (isnan(cfg->standbypos[0] = (double) get_float_val(tokens[1], -360.0, 360.0))) {
	  syslog(LOG_ERR,"%s value %s invalid", tokens[0], tokens[1]);
	  return(-1);
	}
	if (isnan(cfg->standbypos[1] = (double) get_float_val(tokens[2], -360.0, 360.0))) {
	  syslog(LOG_ERR,"%s value %s invalid", tokens[0], tokens[1]);
	  return(-1);
	}
	itmp = 0;
	break;
      case C_MATFILE:
	 if (ntok != 2) {
	    syslog(LOG_ERR, "matfile entry format error");
	    return (-1);
	 }
	 if (strncpy(cfg->matfile, tokens[1], MAXFILELEN) == NULL) {
	    syslog(LOG_ERR, "copy of %s failed: %m", tokens[1]);
	    return (-1);
	 }
	 if ((mfp = fopen(cfg->matfile, "r")) == NULL) {
	   syslog(LOG_ERR, "Could not open %s for reading: %m", tokens[1]);
	   return (-1);
	 }
	 fscanf(mfp, "%lf", &(cfg->poleoff));
	 for (j=0; j<3; j++)   /* Need to read in j, i for it to work */
	   for (i=0; i<3; i++)
	     fscanf(mfp, "%lf", &(cfg->coomat[i][j]));
	 fclose(mfp);
	 fnd_matfile = 1;
	 break;
      case C_MODFILE:
	if (ntok != 2) {
	  syslog(LOG_ERR,"modfile entry format error");
	  return(-1);
	}
	if (strncpy(cfg->modfile, tokens[1], MAXFILELEN) == NULL) {
	  syslog(LOG_ERR, "copy of %s failed: %m", tokens[1]);
	  return (-1);
	}
	if (load_model(cfg->modfile, &cfg->model) != 0) {
	  syslog(LOG_ERR, "Error with load_model()");
	  return(-1);
	}
	fnd_modfile = 1;
	break;
      case C_FOCUSMODFILE:
	if (ntok != 2) {
	  syslog(LOG_ERR,"focmodfile entry format error");
	  return(-1);
	}
	if (strncpy(cfg->focfile, tokens[1], MAXFILELEN) == NULL) {
	  syslog(LOG_ERR,"Copy of %s failed: %m", tokens[1]);
	  return(-1);
	}
	if (load_focus_model(cfg->focfile, &cfg->focmod) != 0) {
	  syslog(LOG_ERR,"Error with load_focus_model()");
	  return(-1);
	}
	break;
      case C_RARANGE:
	 if (ntok != 3) {
	    syslog(LOG_ERR, "rarange entry format error");
	    return (-1);
	 }
	 if (isnan(cfg->rarange[0] = get_float_val(tokens[1], -MAXFLOAT, MAXFLOAT))) {
	   syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[1]);
	   return(-1);
	 }
	 if (isnan(cfg->rarange[1] = get_float_val(tokens[2], -MAXFLOAT, MAXFLOAT))) {
	   syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[2]);
	   return(-1);
	 }
	 break;
      case C_DECRANGE:
	if (ntok != 3) {
	  syslog(LOG_ERR, "decrange entry format error");
	  return (-1);
	}
	if (isnan(cfg->decrange[0] = get_float_val(tokens[1], -MAXFLOAT, MAXFLOAT))) {
	  syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[1]);
	  return(-1);
	}
	if (isnan(cfg->decrange[1] = get_float_val(tokens[2], -MAXFLOAT, MAXFLOAT))) {
	  syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[2]);
	  return(-1);
	}
	break;
      case C_FOCRANGE:
	 if (ntok != 3) {
	    syslog(LOG_ERR, "focrange entry format error");
	    return (-1);
	 }
	 if (isnan(cfg->focrange[0] = get_float_val(tokens[1], -MAXFLOAT, MAXFLOAT))) {
	   syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[1]);
	   return(-1);
	 }
	 if (isnan(cfg->focrange[1] = get_float_val(tokens[2], -MAXFLOAT, MAXFLOAT))) {
	   syslog(LOG_ERR, "%s value %s invalid", tokens[0], tokens[2]);
	   return(-1);
	 }
	 break;
      case C_OVERSPEED:
	if (ntok != 2) {
	  syslog(LOG_ERR,"%s entry format error.", tokens[0]);
	  return(-1);
	}
	if (isnan(cfg->overspeed = get_float_val(tokens[1], 1.0, MAXFLOAT))) {
	  syslog(LOG_ERR,"%s value %s invalid", tokens[0], tokens[1]);
	  return(-1);
	}
	break;
      case C_OBSFILE:
	if (ntok != 2) {
	 syslog(LOG_ERR,"obsfile entry format error");
	 return(-1);
	}
	if (strncpy(cfg->obsfile, tokens[1], MAXFILELEN) == NULL) {
	  syslog(LOG_ERR,"copy of %s failed, %m", tokens[1]);
	  return(-1);
	}
	/* Now load it in */
	if (observatory_conf(cfg->obsfile, &(cfg->altitude), &(cfg->latitude), 
			    &(cfg->longitude)) != 0) {
	  syslog(LOG_ERR,"Error with observatory_conf");
	  return(-1);
	}
	break;
      case C_TESTMODE:
	if (ntok != 2) {
	  syslog(LOG_ERR,"testmode entry format error");
	  return(-1);
	}
	itmp = (int) strtol(tokens[1], &endptr, 0);
	if (*endptr != '\0' || itmp < 0 || itmp > 3) {
	    syslog(LOG_ERR, "testmode value %s not valid", tokens[1]);
	    return (-1);
	 }
	 cfg->testmode = itmp;
	 itmp = 0;
	 break;
      case C_MOUNT_RUN:
	if (ntok != 2) {
	  syslog(LOG_ERR,"%s entry format error", tokens[0]);
	  return(-1);
	}
	itmp = (int) strtol(tokens[1], &endptr, 0);
	if (*endptr != '\0' || itmp < 0 || itmp > 1) {
	    syslog(LOG_ERR, "mount_run value %s not valid", tokens[1]);
	    return (-1);
	 }
	 cfg->mount_run = itmp;
	 itmp = 0;
	 break;
      default:
	 syslog(LOG_ERR, "unrecognized keyval: %d, %s", keyval, tokens[0]);
      }
   }
   if (fclose(fp)) {
      syslog(LOG_ERR, "fclose failed on %s: %m", cfg->confile);
      return (-1);
   }
   /* Check that config params are set. If not, return error. */

   if (cfg->logfile[0] == '\0') {
      syslog(LOG_ERR, "error: logfile not set in config file");
      return (-1);
   }
   if (cfg->loglevel == -1) {
      syslog(LOG_ERR, "error: loglevel not set in config file");
      return (-1);
   }
   if (isnan(cfg->sample_time)) {
      syslog(LOG_ERR, "error: sample_time not set in config file");
      return (-1);
   }
   if (isnan(cfg->poll_time)) {
      syslog(LOG_ERR, "error: poll_time not set in config file");
      return (-1);
   }
   if (isnan(cfg->err_tout)) {
      syslog(LOG_ERR, "error: err_tout not set in config file");
      return (-1);
   }
   if (cfg->mntman[0] == '\0') {
      syslog(LOG_ERR, "error: mntman not set in config file");
      return (-1);
   }
   if (cfg->mntmodel[0] == '\0') {
      syslog(LOG_ERR, "error: mntmodel not set in config file");
      return (-1);
   }
   if (cfg->mntsn == -1) {
      syslog(LOG_ERR, "error: mntsn not set in config file");
      return (-1);
   }
   if (cfg->enctol == 0) {
      syslog(LOG_ERR, "error: enctol not set in config file");
      return (-1);
   }
   if (cfg->foctol == -99.99) {
     syslog(LOG_ERR, "error: foctol not set in config file");
     return (-1);
   }
   if (cfg->erroremail[0] == '\0') {
      syslog(LOG_ERR, "error: erroremail not set in config file");
      return (-1);
   }
   if (cfg->statdir[0] == '\0') {
     syslog(LOG_ERR,"error: statdir not set in config file");
     return(-1);
   }
   if (cfg->statroot[0] == '\0') {
     syslog(LOG_ERR,"error: statroot not set in config file");
     return(-1);
   }
   if (cfg->focus_update[0] == '\0') {
     syslog(LOG_ERR,"error: focus_update not set in config file");
     return(-1);
   }
   if (isnan(cfg->rarange[0]) || isnan(cfg->rarange[1])) {
     syslog(LOG_ERR, "error: rarange not set in config file");
     return(-1);
   } 
   if (cfg->rarange[0] > cfg->rarange[1]) {
     syslog(LOG_ERR,"error: rarange must be in min, max form");
     return(-1);
   }
   if (isnan(cfg->decrange[0]) || isnan(cfg->decrange[1])) {
     syslog(LOG_ERR, "error: decrange not set in config file");
     return(-1);
   }
   if (cfg->decrange[0] > cfg->decrange[1]) {
     syslog(LOG_ERR,"error: decrange must be in min, max form");
     return(-1);
   }
   if (isnan(cfg->focrange[0]) || isnan(cfg->focrange[1])) {
     syslog(LOG_ERR,"error: focrange not set in config file");
     return(-1);
   }
   if (cfg->focrange[0] > cfg->focrange[1]) {
     syslog(LOG_ERR,"error: focrange must be in min, max form");
     return(-1);
   }
   if (isnan(cfg->overspeed)) {
     syslog(LOG_INFO,"overspeed not set in config file.  Setting to 1.0");
     cfg->overspeed = 1.0;
   }

   for (i=0; i<2; i++) {
     if (cfg->slw_acc[i] == -99.99) {
       syslog(LOG_ERR, "error: slw_acc[%d] not set in config file",i);
       return (-1);
     }
     if (cfg->max_vel[i] == -99.99) {
       syslog(LOG_ERR, "error: slw_vel[%d] not set in config file",i);
       return (-1);
     }
     if (cfg->home_vel[i] == -99.99) {
       syslog(LOG_ERR, "error: home_vel[%d] not set in config file",i);
       return (-1);
     }
     if (cfg->stowpos[i] == -99.99) {
       syslog(LOG_ERR, "error: stowpos[%d] not set in config file",i);
       return (-1);
     }
     if (isnan(cfg->standbypos[i])) {
       syslog(LOG_ERR, "error: standbypos[%d] not set in config file",i);
       return(-1);
     }
     if (cfg->deg2enc[i] == -99.99) {
       syslog(LOG_ERR, "error: deg2enc[%d] not set in config file",i);
       return (-1);
     }
   }
   if ((cfg->stowpos[0] < cfg->rarange[0]) || (cfg->stowpos[0] > cfg->rarange[1])) {
     syslog(LOG_ERR,"error: stowpos[0] out of range");
     return(-1);
   }
   if ((cfg->stowpos[1] < cfg->decrange[0]) || (cfg->stowpos[1] > cfg->decrange[1])) {
     syslog(LOG_ERR,"error: stowpos[1] out of range");
     return(-1);
   }
   if (cfg->focfile[0] == '\0') {
     syslog(LOG_ERR,"focmodfile not specified in config file");
     return(-1);
   }


   /* See what modeling we're dealing with */
   if (fnd_modfile && fnd_matfile) {
     syslog(LOG_INFO,"Ignoring matrix file %s",cfg->matfile);
     syslog(LOG_INFO,"Modeling based on %s from tpoint",cfg->modfile);
     cfg->method = TPOINT;
   } else if (fnd_modfile && !fnd_matfile) {
     syslog(LOG_INFO,"Modeling based on %s from tpoint",cfg->modfile);
     cfg->method = TPOINT;
   } else if (!fnd_modfile && fnd_matfile) {
     syslog(LOG_INFO,"Modeling based on %s for two-star pointing",cfg->matfile);
     cfg->method = MATRIX;
   } else {
     syslog(LOG_INFO,"No telescope modeling available.  Raw encoder commands only!");
     cfg->method = NONE;
   }




   /* When we get here all required params were set on the command
    * line and in the config file. */
   return (0);
}
