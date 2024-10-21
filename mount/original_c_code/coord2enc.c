/* coord2enc ======================================================== 

 * Purpose: Convert ra/dec coordinates into mount encoder values
 *           Uses output (in the conf struct) from Eli's twostar code
 *
 * Inputs: RA/Dec values and conf structure
 *    
 * Outputs: encpos[2] - Two-axis values for encoder positions.
 *
 * Created: 2001-05-11  Don Smith 
 * Updated: 2001-07-23  E. Rykoff - Works with twostar or tpoint modeling
 * Updated: 2002-04-02  E. Rykoff -- official version, debug stmnts removed
 ========================================================================= */

#include <stdio.h>
#include <time.h>
#include <math.h>
#include <syslog.h>
#include <mountd_str.h>

#include <slalib.h>
#include <slamac.h>

#include "matrix_ops.h"
#include "protos.h"


int coord2enc(double alpha, double delta, struct mountd_cfg *cnf, int *encpos, 
	      double time_offset)
{
  int j;
  time_t tnew;
  struct tm *tp;  
  double now, al, del, ha;
  double mjd, day;
  double utc, gmst, lmst;
  char sys[3];

  /* Find current time */
  tnew = time(NULL);
  tp = gmtime(&tnew);
  now = ((float) tp->tm_year)+1900.+((float)tp->tm_yday+((float)tp->tm_hour+
		((float)tp->tm_min+(float)tp->tm_sec/60.)/60.)/24.)/365.;

  /* Precess coords from J2000.0 */
  al = alpha * DD2R;
  del = delta * DD2R;
  sprintf(sys, "FK5");
  slaPreces(sys, 2000.0, now, &al, &del);

  /* Find Hour angle */
  slaCldj(tp->tm_year + 1900, tp->tm_mon + 1, tp->tm_mday, &mjd, &j);
  day = ((double) tp->tm_hour + (double) tp->tm_min / 60.0 +
     (double) tp->tm_sec / 3600.0) / 24.0;
  utc = mjd + day + time_offset;
  gmst = slaGmst(utc);
  lmst = gmst + slaEqeqx(utc) + cnf->longitude * DD2R;
  ha = slaDrange(lmst - al);

  /* Bifurcate on which model */
  if (cnf->method == MATRIX) {
    if (apply_matrix(ha, del, encpos, cnf) != 0) {
      syslog(LOG_ERR,"Error with apply_matrix");
      return(-1);
    }
  } else if (cnf->method == TPOINT) {
    if (apply_model(ha, del, encpos, cnf) != 0) {
      syslog(LOG_ERR,"Error with apply_model");
      return(-1);
    }
  }

  return(0);
}


int coord2enc_delta(float del_ra, float del_dec, struct mountd_cfg *cnf, int *encpos)
{
  /* This function simply updates encpos by the desired amount */
  
  encpos[0] += (int) (del_ra * cnf->deg2enc[0]);
  encpos[1] += (int) (del_dec * cnf->deg2enc[1]);

  return(0);

}



