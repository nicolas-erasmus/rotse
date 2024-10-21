/* Apply_model ================================================
 *
 * Purpose: This function applies a Tpoint (tm) pointing model for
 *          more sophisticated pointing.  So far, the pointing model can
 *          consist of the tpoint constants IH, ID, NP, CH, ME, MA, FO
 *
 * int apply_model(double ha, double dec, int *encpos, struct mountd_cfg *cfg);
 *
 * Created: 2002-04-02 E. Rykoff -- first official version
 * Updated: 2003-03-17 E. Rykoff -- new version works in the south
 * Updated: 2004-09-28 E. Rykoff -- added ptg_offset
 * Updated: 2005-10-28 E. Rykoff -- moved ptg_offset to IH/ID
 *=============================================================*/


#include <stdio.h>
#include <time.h>
#include <math.h>
#include <syslog.h>

#include <mountd_str.h>

#include <slamac.h>

#include "protos.h"

#define sec(A)    1./cos(A)

int apply_model(double ha, double dec, int *encpos, struct mountd_cfg *cfg)
{
  float sign;
  
  int i,j;
  int numterms, type;
  int reverse;
  double delta_dec, delta_ha;
  double s2d = 1/(60.0*60.0);
  double lat;

  double dha = 0, ddec = 0;

  int good_sol = 0;
  int flip = 1;
  int ntry = 0;

  float ramin,ramax;

  lat = cfg->latitude * DD2R;

  if (lat < 0.0) {
    ramax = -1.0 * cfg->rarange[0];
    ramin = -1.0 * cfg->rarange[1];
  } else {
    ramin = cfg->rarange[0];
    ramax = cfg->rarange[1];
  }


  i=0;
  while (cfg->model.term[i].type != -1) {
    i++;
  }
  numterms = i-1;
  
  /* For now, we still should only use S->T direction! */
  if (cfg->model.method == 'S') {
    reverse = 1;
    sign = -1.0;
  } else {
    reverse = 0;
    sign = 1.0;
  }
  
  while ((good_sol == 0) && (ntry < 2)) {
    ntry ++;
    dha= ha * DR2D;
    ddec = dec * DR2D;

    if (flip) {
      dha = dha + 180.0;
      ddec = 180.0 - ddec;
    }
  
    delta_ha = delta_dec = 0.0;
    for (i=0;i<=numterms;i++) {
      if (reverse) {j=numterms - i;}
      else {j = i;}

      type = cfg->model.term[j].type;
  
      switch(type) {
      case IH:
	/* delta h = + IH */
	delta_ha = cfg->model.term[j].value * s2d +
	  cfg->ptg_offset[0]/cfg->deg2enc[0];   /* enc -> deg */
	dha = dha + sign*delta_ha;
	break;
      case ID:
	/* delta d = + ID */
	delta_dec = cfg->model.term[j].value * s2d +
	  cfg->ptg_offset[1]/cfg->deg2enc[1];   /* enc -> deg */
	ddec = ddec + sign*delta_dec;
	break;
      case NP:
	/* delta h = + NP tan d */
	delta_ha = cfg->model.term[j].value * s2d * tan(ddec*DD2R);
	dha = dha + sign*delta_ha;
	break;
      case CH:
	/* delta h = + CH sec d */
	delta_ha = (cfg->model.term[j].value * s2d) / cos(ddec*DD2R);
	dha = dha + sign*delta_ha;
	break;
      case ME:
	/* delta h = + ME sin h tan d */
	/* delta d = + ME cos h */
	delta_ha = (cfg->model.term[j].value * s2d) * sin(dha*DD2R) * tan(ddec*DD2R);
	delta_dec = (cfg->model.term[j].value * s2d) * cos(dha*DD2R);
	dha = dha + sign*delta_ha;
	ddec = ddec + sign*delta_dec;
	break;
      case MA:
	/* delta h = - MA cos h tan d */
	/* delta d = + MA sin h */
	delta_ha = -1.0 * (cfg->model.term[j].value * s2d) * cos(dha*DD2R) * tan(ddec*DD2R);
	delta_dec = (cfg->model.term[j].value * s2d) * sin(dha*DD2R);
	dha = dha + sign*delta_ha;
	ddec = ddec + sign*delta_dec;
	break;
      case FO:
	/* delta d = + FO cos h */
	delta_dec = (cfg->model.term[j].value * s2d) * cos(dha*DD2R);
	ddec = ddec + sign*delta_dec;
	break;
      case TF:
	/* delta h = + TF cos l sin h sec d */
	/* delta d = + TF (cos l cos h sin d - sin l cos d) */
	delta_ha = (cfg->model.term[j].value * s2d) * cos(lat) * sin(dha*DD2R) * sec(ddec*DD2R);
	delta_dec = (cfg->model.term[j].value * s2d) *
	  (cos(lat) * cos(dha*DD2R) * sin(ddec*DD2R) - sin(lat) * cos(ddec*DD2R));
	dha = dha + sign*delta_ha;
	ddec = ddec + sign*delta_dec;
	break;
      case TX:
	/* delta h = + TX cos l sin h sec d / (sin d sin l + cos d cos h cos l) */
	/* delta d = + TX (cos l cos h sin d - sin l cos d) / (sin d sin l + cos d cos h cos l) */
	delta_ha = (cfg->model.term[j].value * s2d) *
	  (cos(lat) * sin(dha*DD2R) * sec(ddec*DD2R)) /
	  (sin(ddec*DD2R) * sin(lat) + cos(ddec*DD2R) * cos(dha*DD2R)*cos(lat));
	delta_dec = (cfg->model.term[j].value * s2d) *
	  (cos(lat) * cos(dha*DD2R) * sin(ddec*DD2R) - sin(lat) * cos(ddec*DD2R)) /
	  (sin(ddec*DD2R) * sin(lat) + cos(ddec*DD2R) * cos(dha*DD2R) * cos(lat));
	dha = dha + sign*delta_ha;
	ddec = ddec + sign*delta_dec;
	break;
      default:
	syslog(LOG_ERR,"Error: Unrecognized tpoint type %d", type);
	return(-1);
	break;
      }
    }

    if (cfg->model.method == 'S') {
      if (lat > 0.0) {
	if (dha < ramax) {
	  good_sol = 1;
	} else if (((dha - 360.0) > ramax) || ((dha - 180.0) < ramax)) {
	  flip = 0;
	} else {
	  good_sol = 1;
	  dha = dha - 360.0;
	}
      } else {
	/* south version */
	if ((dha > ramin) && (dha < ramax)) {
	  //syslog(LOG_INFO,"a]dha = %f, ddec = %f", dha, ddec);
	  good_sol = 1;
	} else if ((dha + 360.0) > ramax) {
	  //syslog(LOG_INFO,"b]dha = %f, ddec = %f", dha, ddec);
	  flip = 0;
	} else {
          //syslog(LOG_INFO,"c]dha = %f, ddec = %f", dha, ddec);
	  good_sol = 1;
	  dha += 360.0;
	}
      }
    } else {
      /* not finished yet */
      good_sol = 1;
    }
  }

  if ((ntry == 2) && (!good_sol)) {
    syslog(LOG_ERR,"apply_model failed to find a good solution! -- send to standby");
    dha = cfg->standbypos[0];
    ddec = cfg->standbypos[1];
    lat = 1.0;  /* so it doesn't try any funny stuff */
  }

  if (lat < 0.0) {
    /* and do the negative shuffle */
    if (ddec > 0.0) {   // otherwise it's already the correct sign
      ddec = ddec - 360.0;
    }
    dha *= -1.0;      // final inverse b/c model was built from inverted values
    ddec *= -1.0;     // final inverse b/c model was build from inverted values
  }

  //syslog(LOG_INFO,"dha= %f, ddec = %f", dha,ddec);

  encpos[0] = (int) (dha * cfg->deg2enc[0]) + cfg->zeropt[0];// + cfg->ptg_offset[0];
  encpos[1] = (int) (ddec * cfg->deg2enc[1]) + cfg->zeropt[1];// + cfg->ptg_offset[1];

  return(0);

}
