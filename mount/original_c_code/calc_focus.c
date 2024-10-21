/* calc_focus ===================================================
 *
 * Purpose: calculate the focus from a simple bilinear focus model
 *
 *
 * Created: 2001-10-04 Eli Rykoff
 * Updated: 2002-04-02 E. Rykoff -- first official version
 * Updated: 2002-05-09 E. Rykoff -- changed focus modeling to generic terms
 * Updated: 2004-09-13 E. Rykoff -- moved guts to apply_focus_model
 =========================================*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>


#include <rotsed_str.h>
#include <slalib.h>
#include <slamac.h>


#include "protos.h"
#include "schierd.h"

float calc_focus(struct mountd_cfg cfg, struct mountd_par cmd)
{
  int j;
  time_t tnew;
  struct tm *tp;  
  double now, al, del, ha;
  double mjd, day;
  double utc, gmst, lmst;
  char sys[3];

  double az, el;

  //double term,focus;
  float focus;

  //  int i;
  
  /* Find current time */
  tnew = time(NULL);
  tp = gmtime(&tnew);
  now = ((float) tp->tm_year)+1900.+((float)tp->tm_yday+((float)tp->tm_hour+
		((float)tp->tm_min+(float)tp->tm_sec/60.)/60.)/24.)/365.;

  /* Precess coords from J2000.0 */
  al = cmd.ra * DD2R;
  del = cmd.dec * DD2R;
  sprintf(sys, "FK5");
  slaPreces(sys, 2000.0, now, &al, &del);

  /* Find Hour angle */
  slaCldj(tp->tm_year + 1900, tp->tm_mon + 1, tp->tm_mday, &mjd, &j);
  day = ((double) tp->tm_hour + (double) tp->tm_min / 60.0 +
     (double) tp->tm_sec / 3600.0) / 24.0;
  utc = mjd + day;
  gmst = slaGmst(utc);
  lmst = gmst + slaEqeqx(utc) + cfg.longitude * DD2R;
  ha = slaDrange(lmst - al);

  slaDe2h(ha, del, cfg.latitude * DD2R, &az, &el);

  az = az * DR2D;
  el = el * DR2D;

  /* Now apply model */

  focus = apply_focus_model(cfg.focmod, az, el, (double) cmd.temp);

  return(focus);
}

float apply_focus_model(struct focus_model_st focmod, double az, double el, 
			double temp)
{
  double focus = 0.0;
  int i,j;
  double term;

  for (i=0;i<focmod.nterms;i++) {
    term = focmod.term[i].value;
    for (j=0;j<strlen(focmod.term[i].str);j++) {
      switch(focmod.term[i].str[j]) {
      case '1':
	term = term * 1.0;
	break;
      case 't':
	term = term * temp;
	break;
      case 'e':
	term = term * el;
	break;
      case 'a':
	term = term * az;
	break;
      }
    }
    focus = focus + term;
  }

  return((float)focus);
}
