/* Apply_Matrix ===========================================
 *
 * Purpose: This function applies a two star pointing model matrix for
 *          simple pointing.
 *
 *  int apply_matrix(double ha, double dec, int *encpos, struct mountd_cfg *cfg);
 *
 * Created: 2002-04-02 E. Rykoff -- first official version
 * Updated: 2003-03-18 E. Rykoff -- mods for southern hemisphere
 * Updated: 2004-09-28 E. Rykoff -- added ptg_offset
 *=========================================================*/


#include <stdio.h>
#include <time.h>
#include <math.h>
#include <mountd_str.h>

#include <slamac.h>

#include "matrix_ops.h"
#include "protos.h"


int apply_matrix(double ha, double dec, int *encpos, struct mountd_cfg *cfg)
{
  struct matrix v,nv,a;
  double cosb,x,y,z,r;
  double dcmd, ramd;
  int i,j;

  float ramax;

  if (cfg->latitude < 0.0) {
    ramax = -1.0 * cfg->rarange[0];
  } else {
    ramax = cfg->rarange[1];
  }

  /* Convert vector to xyz */
  initmat(&v, 1, 3);
  cosb = cos(dec);
  v.val[0][0] = cos(ha) * cosb;
  v.val[0][1] = sin(ha) * cosb;
  v.val[0][2] = sin(dec);

  /* Set up rotation matrix from conf file */
  initmat(&a, 3, 3);
  for (i=0; i<3; i++)
    for (j=0; j<3; j++)
      a.val[i][j] = cfg->coomat[i][j];

  /* Apply rotation */
  if (mult_mat(a, v, &nv) != 0) {
    syslog(LOG_ERR,"Error with mult_mat");
    return(-1);
  }

  /* Convert vector back into spherical coords */
  x = nv.val[0][0];
  y = nv.val[0][1];
  z = nv.val[0][2];


  r = sqrt(x*x+y*y);

  dcmd = asin(z)*DR2D;
  ramd = acos(x/r)*DR2D;
  if (y < 0.0) ramd = 360. - ramd;

  if (ramd > cfg->rarange[1]) {
    ramd = ramd - 180.0;
    dcmd = 180.0 - dcmd;
  }
  if (ramd > cfg->rarange[1]) {
    syslog(LOG_INFO,"The dec axis is flipped!");
    ramd = ramd - 180.0;
    dcmd = 180.0 - dcmd;
  }

  dcmd -= cfg->poleoff;
  if (cfg->latitude < 0.0) {
    if (dcmd > 0.0) {
      dcmd -= 360.0;
    }
    ramd *= -1.0;     // final inverse b/c model was built from inverted values
    dcmd *= -1.0;     // final inverse b/c model was build from inverted values
  }

  /* Convert to encoder steps */

  encpos[0] = (int) (ramd * cfg->deg2enc[0]) + cfg->zeropt[0] + cfg->ptg_offset[0];
  encpos[1] = (int) (dcmd * cfg->deg2enc[1]) + cfg->zeropt[1] + cfg->ptg_offset[1];

  /* Release matrix memory */
  freemat(&nv);
  freemat(&a);
  freemat(&v);

  return(0);

}
