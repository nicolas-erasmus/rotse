/* update_offsets =====================================================
 *
 * Purpose: This function reads in online calibration status information
 *          and updates the mount zero-points from successful pointing on
 *          the sky.  This cuts the systematic pointing error (caused by
 *          homing uncertainties) down considerably, so pointing is good within
 *          a couple of arcminutes rather than twenty or so arcminutes.
 *
 *  int update_offsets(struct mountd_cfg *cfg);
 *
 * Created: 2002-04-02 E. Rykoff -- first official version
 * Updated: 2004-09-28 E. Rykoff -- added continous updating; updates fitsfile
 * Updated: 2004-12-15 E. Rykoff -- works with sobj only fitsfile
 *=====================================================================*/




#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <values.h>
#include <syslog.h>


#include <mountd_str.h>
#include <slalib.h>
#include <slamac.h>
#include <fitsio.h>


#include "protos.h"
#include "schierd.h"


#define ISEOL(c)            ((c) == '#' || (c) == '\n' || (c) == '\0')
#define ISSPACE(c)          ((c) == ' ' || (c) == '\t')

#define MAXTOKENS 10
#define MAXLINE 1024

int update_offsets(struct mountd_cfg *cfg) 
{
  /* Returns: 0 on success/update, 1 on file found/no update,
     2 on file not found, -1 on error */ 
  char datestr[10];
  char statfile_dat[MAXFILELEN], statfile_fit[MAXFILELEN];

  int is_statfile_fit, is_statfile_dat;

  struct stat stat_buf;
  FILE *fp;
  char *(tokens[MAXTOKENS]);
  char line[MAXLINE];
  int ntok;

  double mjd, pra, pdec, rra, rdec;
  double encra, encdec, mlim;

  double nmjd;
  time_t now;
  int encpos_orig[2], encpos_new[2];
  double dra,ddec;

  int off_ra_int, off_dec_int;

  /* fits definitions */
  fitsfile *fptr;
  int status = 0;
  int hdutype;
  long nrow;
  int ncol;
  int mjdcol,pracol,pdeccol,rracol,rdeccol;
  int encracol,encdeccol, mlimcol;
  
  mjd = pra = pdec = rra = rdec = mlim = 0.0;
  encra = encdec = 0.0;

  if (isnan(cfg->zero_mjd)) {
    rlog(TERSE, cfg->logfd,"Mount hasn't been synced yet!\n");
    return(2);
  }

  /* we can work with .dat statfiles or .fit statfiles */
  
  /* Figure out name of statfile */
  get_ut_date(datestr);
  
  sprintf(statfile_dat,"%s/%s_%s_run.dat", cfg->statdir, datestr, cfg->statroot);
  sprintf(statfile_fit,"%s/%s_%s_run.fit", cfg->statdir, datestr, cfg->statroot);

  /* determine which statfile, if any, is present */
  is_statfile_dat = is_statfile_fit = 0;
  if (stat(statfile_fit, &stat_buf) == 0) {
    is_statfile_fit = 1;
  } else if (stat(statfile_dat, &stat_buf) == 0) {
    is_statfile_dat = 1;
  }

  if (is_statfile_fit) {
    if (statfile_lock(statfile_fit) == 0) {
      /* we have a new fitfile */
      if (fits_open_file(&fptr, statfile_fit, READONLY, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_open_file (%s), (%d)\n", statfile_fit, status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
    
      if (fits_movabs_hdu(fptr, 2, &hdutype, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_movabs_hdu (%d)\n", status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
    
      if (hdutype != BINARY_TBL) {
	rlog(TERSE,cfg->logfd,"bad hdu type: %d\n", hdutype);
	if (fits_close_file(fptr, &status)) {
	  rlog(TERSE,cfg->logfd,"error in fits_close_file (%d)\n", status);
	  statfile_unlock(statfile_fit); 
	  return(-1);
	}
	statfile_unlock(statfile_fit); 
	return(-1);
      }
    
      if (fits_get_num_rows(fptr, &nrow, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_num_rows (%d)\n", status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_get_num_cols(fptr, &ncol, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_num_cols (%d)\n", status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
    
      if (fits_get_colnum(fptr, CASEINSEN, "mjd", &mjdcol, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_colnum(mjd) (%d)\n", status);
	status = 0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_read_col(fptr, TDOUBLE, mjdcol, nrow, 1, 1, NULL, &mjd, NULL, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_read_col(mjd) (%d)\n", status);
	status=0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit);
	return(-1);
      }

      if (fits_get_colnum(fptr, CASEINSEN, "pra", &pracol, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_colnum(pra) (%d)\n", status);
	status = 0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_read_col(fptr, TDOUBLE, pracol, nrow, 1, 1, NULL, &pra, NULL, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_read_col(pra) (%d)\n", status);
	status=0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }

      if (fits_get_colnum(fptr, CASEINSEN, "pdec", &pdeccol, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_colnum(pdec) (%d)\n", status);
	status = 0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_read_col(fptr, TDOUBLE, pdeccol, nrow, 1, 1, NULL, &pdec, NULL, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_read_col(pdec) (%d)\n", status);
	status=0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }

      if (fits_get_colnum(fptr, CASEINSEN, "rra", &rracol, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_colnum(rra) (%d)\n", status);
	status = 0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_read_col(fptr, TDOUBLE, rracol, nrow, 1, 1, NULL, &rra, NULL, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_read_col(rra) (%d)\n", status);
	status=0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_get_colnum(fptr, CASEINSEN, "rdec", &rdeccol, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_colnum(rdec) (%d)\n", status);
	status = 0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_get_colnum(fptr, CASEINSEN, "encra", &encracol, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_colnum(encradec) (%d)\n", status);
	status = 0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_get_colnum(fptr, CASEINSEN, "encdec", &encdeccol, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_colnum(encdec) (%d)\n", status);
	status = 0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }



      if (fits_read_col(fptr, TDOUBLE, rdeccol, nrow, 1, 1, NULL, &rdec, NULL, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_read_col(rdec) (%d)\n", status);
	status=0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_read_col(fptr, TDOUBLE, encracol, nrow, 1, 1, NULL, &encra, NULL, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_read_col(encra) (%d)\n", status);
	status=0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_read_col(fptr, TDOUBLE, encdeccol, nrow, 1, 1, NULL, &encdec, NULL, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_read_col(encdec) (%d)\n", status);
	status=0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }

      if (fits_get_colnum(fptr, CASEINSEN, "mlim", &mlimcol, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_get_colnum(mlim) (%d)\n", status);
	status = 0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      if (fits_read_col(fptr, TDOUBLE, mlimcol, nrow, 1, 1, NULL, &mlim, NULL, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_read_col(mlim) (%d)\n", status);
	status=0;
	fits_close_file(fptr,&status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }

      if (fits_close_file(fptr, &status)) {
	rlog(TERSE,cfg->logfd,"error in fits_close_file (%d)\n", status);
	statfile_unlock(statfile_fit); 
	return(-1);
      }
      statfile_unlock(statfile_fit);
    }
    } else if (is_statfile_dat) {
    /* we have an old datfile */

    /* Open 'er up */
    if ((fp = fopen(statfile_dat, "r")) == NULL) {
      syslog(LOG_ERR,"Error opening %s: %m", statfile_dat);
      return(-1);
    }

    while (fgets(line, MAXLINE, fp) != NULL);
    /* We are now pointing at the last line */
    fclose(fp);

    if ((ntok = gettokens_nofile(line, MAXLINE, tokens, MAXTOKENS)) < 7) {
      syslog(LOG_ERR,"Error in last line of %s", statfile_dat);
      return(-1);
    }
    mjd = strtod(tokens[1], NULL);
    pra = strtod(tokens[3], NULL);
    pdec = strtod(tokens[4], NULL);
    rra = strtod(tokens[5], NULL);
    rdec = strtod(tokens[6], NULL);
    mlim = 20.0;
  } else {
    rlog(TERSE, cfg->logfd, "Could not find %s or %s\n", statfile_fit, statfile_dat);
    return(2);
  }

  /* Check if we want an update */
  
  if ((mjd <= cfg->zero_mjd) || (mlim < 0.0)) {
    rlog(VERBOSE, cfg->logfd, "No new entries (%13.6f < %13.6f), or not calibrated image (%.2f)\n",
	 mjd, cfg->zero_mjd, mlim);
    return(1);
  }
  
  rlog(VERBOSE, cfg->logfd, "Using: %13.6f, %f,%f -> %f,%f\n",
       mjd, pra, pdec, rra, rdec);

  /* update the statfile (if it is fits type)*/
  if (is_statfile_fit) {
    
    if (update_statfile_offset(statfile_fit, cfg->logfd,
			       -1.0, -1.0,
			       pra, rra, pdec, rdec) != 0) {
      syslog(LOG_ERR,"error with update_statfile_offset (pointing)");
    }
  }

  now = time(NULL);
  nmjd = time2mjd(now);

  if (radec2enc(encra,encdec,mjd,encpos_orig,*cfg) != 0) {
    syslog(LOG_ERR,"Error with radec2enc");
    return(-1);
  }

  if (coord2enc(rra,rdec,cfg,encpos_new,mjd-nmjd) != 0) {
    syslog(LOG_ERR,"Error with coord2enc (pointing)");
    return(-1);
  }

  off_ra_int = encpos_orig[0] - encpos_new[0];
  off_dec_int = encpos_orig[1] - encpos_new[1];

  /* Sanity checks */
  dra = (rra - pra) * cos(rdec * DD2R);
  ddec = (rdec - pdec);

  /*  if ((abs(off_ra_int) > fabs(dra) * cfg->deg2enc[0] * 3.0) ||
      (abs(off_ra_int) > MAX_OFFSET * cfg->deg2enc[0])) {*/
  if (abs(off_ra_int) > MAX_OFFSET * cfg->deg2enc[0]) {
    syslog(LOG_INFO,"RA Offset too large (%.3f).  Cancelling.", 
	   (float) off_ra_int / cfg->deg2enc[0]);
    rlog(VERBOSE,cfg->logfd,"RA Offset too large (%.3f).  Cancelling.\n", 
	   (float) off_ra_int / cfg->deg2enc[0]);
    off_ra_int = 0;
  }
  /*  if ((abs(off_dec_int) > fabs(ddec) * cfg->deg2enc[1] * 3.0) ||
      (abs(off_dec_int) > MAX_OFFSET * cfg->deg2enc[1])) {*/
  if (abs(off_dec_int) > MAX_OFFSET * cfg->deg2enc[1]) {
    syslog(LOG_INFO,"Dec Offset too large (%.3f).  Cancelling.",
	   (float) off_dec_int / cfg->deg2enc[1]);
    rlog(VERBOSE,cfg->logfd,"Dec Offset too large (%.3f).  Cancelling.\n",
	   (float) off_dec_int / cfg->deg2enc[1]);
    off_dec_int = 0;
  }

  rlog(TERSE, cfg->logfd, "Adding %d, %d to pointing offset\n", off_ra_int, off_dec_int);

  cfg->ptg_offset[0] += off_ra_int;
  cfg->ptg_offset[1] += off_dec_int;


  /*  cfg->zero_mjd = NANVAL; */  /* no longer reset-- allow multiple updates */
  /* set this to now. */
  cfg->zero_mjd = nmjd;


  return(0);
}


int gettokens_nofile(char *line,
		     int line_size,
		     char **tokenlist,
		     int maxtokens)
{
   char *cp;
   int eol;
   int ntok;
   int quoted = 0;

   if (maxtokens == 0)
      return (0);
   /*
    * Now separate out the tokens
    */
   cp = line;
   while (ISSPACE(*cp))
     cp++;
   
   eol = 0;
   ntok = 0;
   while (!eol) {
      quoted ^= (*cp == '"');
      if (quoted)
         cp++;
      tokenlist[ntok++] = cp;
      while (!ISEOL(*cp) && (!ISSPACE(*cp) || quoted))
         quoted ^= (*cp++ == '"');

      if (*(cp - 1) == '"')
         cp--;
      if (ISEOL(*cp)) {
         *cp = '\0';
         eol = 1;
     } else {                  /* must be space */
         *cp++ = '\0';
         while (ISSPACE(*cp))
            cp++;
         if (ISEOL(*cp))
            eol = 1;
      }
      if (ntok == maxtokens)
         eol = 1;
   }
   return ntok;
}


double time2mjd(time_t time)
{
  struct tm gmt;
  int k;
  double djm;
  float days;
  double retval;

  gmt = *gmtime(&time);

  slaCldj((gmt.tm_year > 70 ? gmt.tm_year + 1900 : gmt.tm_year
           + 2000), 1 + gmt.tm_mon, gmt.tm_mday, &djm, &k);
  slaCtf2d(gmt.tm_hour, gmt.tm_min, gmt.tm_sec, &days, &k);

  retval = djm + (double) days;

  return(retval);
}
