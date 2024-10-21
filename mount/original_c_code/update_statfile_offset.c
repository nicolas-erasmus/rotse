/* update_statfile_offset ==================================================
 *
 * Purpose: This function updates the data fitsfile with details on focus
 *           and pointing updates.  The current mjd is used.  Only updates
 *           the file if it can get a lock.
 *
 * Created: 2002-09-28 E. Rykoff -- first official version
 *
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
#include "fitsio.h"

#include "protos.h"
#include "schierd.h"



int update_stat_fits_elt(fitsfile *fptr, char *colname, int nrow, double val);


int update_statfile_offset(char *statfile, int logfd,
			   float ofocus, float nfocus,
			   double ora, double nra, double odec, double ndec)
{
  fitsfile *fptr;

  struct stat stat_buf;
  long nrow;
  int ncol;
  int hdutype;
  int status = 0;
  time_t now;
  double mjd;

  now = time(NULL);
  mjd = time2mjd(now);

  if (stat(statfile, &stat_buf) != 0) {
    syslog(LOG_INFO,"Statfile %s not found: cannot update offset in statfile", statfile);
    return(-1);
  } 

  if (statfile_lock(statfile) == 0) {
    if (fits_open_file(&fptr, statfile, READWRITE, &status)) {
      rlog(TERSE,logfd, "error in fits_open_file (%s), (%d)\n",statfile, status);
      statfile_unlock(statfile);
      return(-1);
    }
    if (fits_movabs_hdu(fptr, 3, &hdutype, &status)) {
      rlog(TERSE,logfd,"error in fits_movabs_hdu (%d)\n", status);
      statfile_unlock(statfile);
      return(-1);
    }
    
    if (hdutype != BINARY_TBL) {
      rlog(TERSE, logfd, "bad hdu type: %d\n", hdutype);
      status = 0;
      fits_close_file(fptr, &status);
      statfile_unlock(statfile);
      return(-1);
    }

    if (fits_get_num_rows(fptr, &nrow, &status)) {
      rlog(TERSE,logfd,"error in fits_get_num_rows (%d)\n", status);
      statfile_unlock(statfile); 
      return(-1);
    }
    if (fits_get_num_cols(fptr, &ncol, &status)) {
      rlog(TERSE,logfd,"error in fits_get_num_cols (%d)\n", status);
      statfile_unlock(statfile); 
      return(-1);
    }
    
    if (fits_insert_rows(fptr, nrow, 1, &status)) {
      rlog(TERSE, logfd, "error in fits_insert_rows (%d)\n", status);
      statfile_unlock(statfile);
      return(-1);
    }
    
    /* this row is inserted; will need to delete on error */

    nrow++;
    if (update_stat_fits_elt(fptr, "mjd", nrow, mjd)) {
      rlog(TERSE, logfd, "error in update_stat_fits_elt (mjd)\n");
      fits_delete_rows(fptr, nrow, 1, &status);
      fits_close_file(fptr, &status);
      statfile_unlock(statfile);
      return(-1);
    }
    if (update_stat_fits_elt(fptr, "ofocus", nrow, ofocus)) {
      rlog(TERSE, logfd, "error in update_stat_fits_elt (ofocus)\n");
      fits_delete_rows(fptr, nrow, 1, &status);
      fits_close_file(fptr, &status);
      statfile_unlock(statfile);
      return(-1);
    }
    if (update_stat_fits_elt(fptr, "nfocus", nrow, nfocus)) {
      rlog(TERSE, logfd, "error in update_stat_fits_elt (nfocus)\n");
      fits_delete_rows(fptr, nrow, 1, &status);
      fits_close_file(fptr, &status);
      statfile_unlock(statfile);
      return(-1);
    }
    if (update_stat_fits_elt(fptr, "ora", nrow, ora)) {
      rlog(TERSE, logfd, "error in update_stat_fits_elt (ora)\n");
      fits_delete_rows(fptr, nrow, 1, &status);
      fits_close_file(fptr, &status);
      statfile_unlock(statfile);
      return(-1);
    }
    if (update_stat_fits_elt(fptr, "nra", nrow, nra)) {
      rlog(TERSE, logfd, "error in update_stat_fits_elt (nra)\n");
      fits_delete_rows(fptr, nrow, 1, &status);
      fits_close_file(fptr, &status);
      statfile_unlock(statfile);
      return(-1);
    }
    if (update_stat_fits_elt(fptr, "odec", nrow, odec)) {
      rlog(TERSE, logfd, "error in update_stat_fits_elt (odec)\n");
      fits_delete_rows(fptr, nrow, 1, &status);
      fits_close_file(fptr, &status);
      statfile_unlock(statfile);
      return(-1);
    }
    if (update_stat_fits_elt(fptr, "ndec", nrow, ndec)) {
      rlog(TERSE, logfd, "error in update_stat_fits_elt (ndec)\n");
      fits_delete_rows(fptr, nrow, 1, &status);
      fits_close_file(fptr, &status);
      statfile_unlock(statfile);
      return(-1);
    }

    if (fits_close_file(fptr, &status)) {
      rlog(TERSE, logfd, "error in fits_close_file\n");
      statfile_unlock(statfile);
      return(-1);
    }

    statfile_unlock(statfile);
  } else {
    syslog(LOG_INFO,"statfile %s locked: cannot log offset update", statfile);
  }



  return(0);
}

int update_stat_fits_elt(fitsfile *fptr, char *colname, int nrow, double val)
{
  int status = 0;
  int colnum;

  if (fits_get_colnum(fptr, CASEINSEN, colname, &colnum, &status)) {
    return(-1);
  }

  if (fits_write_col(fptr, TDOUBLE, colnum, nrow, 1, 1, &val, &status)) {
    return(-1);
  }

  return(0);
}
