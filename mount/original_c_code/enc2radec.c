/* enc2radec ==============================================
 *
 * Purpose: Convert encoder counts to encoder ra/dec positions, for
 *          use with the tpoint modeling software.  The output ra/dec
 *          are _not_ the real ra/dec on the sky!
 *
 * int enc2radec(int *encpos, double *ra, double *dec, struct mountd_cfg conf);
 *
 * Created: 2002-04-02 E. Rykoff -- first official version
 * Updated: 2004-09-28 E. Rykoff -- added radec2enc (inverse)
 * ==========================================================*/


#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <mountd_str.h>

#include <slalib.h>
#include <slamac.h>

#include "protos.h"


int enc2radec(int *encpos, double *ra, double *dec, struct mountd_cfg conf)
{
  time_t tnew;
  struct tm *tp;
  double day, mjd, utc, gmst, lmst;
  double ha;
  int j;

  tnew = time(NULL);
  tp = gmtime(&tnew);

  slaCldj(tp->tm_year + 1900, tp->tm_mon + 1, tp->tm_mday, &mjd, &j);
  day = ((double) tp->tm_hour + (double) tp->tm_min / 60.0 +
     (double) tp->tm_sec / 3600.0) / 24.0;
  utc = mjd + day;
  gmst = slaGmst(utc);
  lmst = gmst + slaEqeqx(utc) + conf.longitude * DD2R;
  
  ha = ((double) encpos[0] / conf.deg2enc[0]) * DD2R;
  *dec = (double) encpos[1] / conf.deg2enc[1];

  if (conf.latitude < 0.0) {
    ha *= -1.0;
    *dec *= -1.0;
  }


  *ra = slaDrange(lmst - ha) * DR2D;

  if (*ra < 0.0) { *ra = *ra + 360.0; }
  if (*ra > 360.0) { *ra = *ra - 360.0; }

  //*dec = (double) encpos[1] / conf.deg2enc[1];

  return(0);

}

int radec2enc(double ra, double dec, double mjd, int *encpos, struct mountd_cfg conf)
{
  double utc, gmst, lmst;
  double ha, ddec;

  utc = mjd;
  gmst = slaGmst(utc);
  lmst = gmst + slaEqeqx(utc) + conf.longitude * DD2R;
  
  ha = slaDrange(lmst - ra * DD2R) * DR2D;
  ddec = dec;

  if (conf.latitude < 0.0) {
    if (ddec > 0.0) { 
      ddec = ddec - 360.0;
    }
    ha *= -1.0;
    ddec *= -1.0;
  }

  if (ha < conf.rarange[0]) { ha += 360.0; }
  if (ha > conf.rarange[1]) { ha -= 360.0; }

  encpos[0] = ha * conf.deg2enc[0] + conf.zeropt[0];
  encpos[1] = ddec * conf.deg2enc[1] + conf.zeropt[1];

  return(0);
}
