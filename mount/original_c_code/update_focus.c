/* update_focus =====================================================
 *
 * Purpose: This function reads in focus information
 *
 *  int update_focus(struct mountd_cfg *cfg);
 *
 * Created: 2003-03-22 E. Rykoff -- first official version
 * Updated: 2004-09-13 E. Rykoff -- works with new focus offset files
 * Updated: 2004-09-28 E. Rykoff -- added update_statfile_offset call
 * Updated: 2005-02-22 E. Rykoff -- added return check on sscanf
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


#include "protos.h"
#include "schierd.h"


#define ISEOL(c)            ((c) == '#' || (c) == '\n' || (c) == '\0')
#define ISSPACE(c)          ((c) == ' ' || (c) == '\t')

#define MAXTOKENS 10
#define MAXLINE 1024

int update_focus(struct mountd_cfg *cfg) 
{
  /* Returns: 0 on success/update, 1 on file found/no update,
     2 on file not found, -1 on error */ 

  char focfile[MAXFILELEN];
  char statfile[MAXFILELEN];
  char datestr[10];
  
  struct stat stat_buf;
  FILE *fp;
  char line[MAXLINE];

  static double last_mjd = 0.0;
  static time_t last_atime = 0;

  double mjd;
  float focus, foc_err, chisq, az, el, temp, wspd;
  float old_focus, focus_delta;
  int i;
  int retval;

  mjd = 0.0;

  snprintf(focfile,MAXFILELEN,"%s/%s", cfg->statdir, cfg->focus_update);

  /* Check if statfile exists */
  if (stat(focfile, &stat_buf) == -1) {
    rlog(DEBUG, cfg->logfd, "focfile %s not found\n", focfile);
    return(2);
  }

  if (stat_buf.st_atime > last_atime) {
    last_atime = stat_buf.st_atime;

  
    /* Open 'er up */
    if ((fp = fopen(focfile, "r")) == NULL) {
      syslog(LOG_ERR,"Error opening %s: %m", focfile);
      return(-1);
    }

    fgets(line, MAXLINE, fp);
    retval = sscanf(line,"%lf %f %f %f %f %f %f %f", &mjd, &focus, &foc_err, &chisq,
		    &az, &el, &temp, &wspd);
    
    fclose(fp);

    if (retval != 8) {
      syslog(LOG_ERR,"Error with format of %s", focfile);
      return(-1);
    }

    
    /* Check if we want an update */
    /* we might have some issues whether we want this on startup;
       right now I'll leave it with an institutional memory */
    if ((mjd > last_mjd) && (focus > 0.0)) {
      /* we have an update to do */
      
      last_mjd = mjd;
      
      old_focus = apply_focus_model(cfg->focmod, (double) az, (double) el,
				    (double) temp);
      
      focus_delta = focus - old_focus;
      
      /* now need to find the correct focus term*/
      for (i=0;i<cfg->focmod.nterms;i++) {
	switch (cfg->focmod.term[i].str[0]) {
	case '1':
	  cfg->focmod.term[i].value += focus_delta;
	  syslog(LOG_INFO,"Focus Updated: %.3f + %.3f = %.3f",
		 cfg->focmod.term[i].value - focus_delta, focus_delta,
		 cfg->focmod.term[i].value);
	  
	  /* and log the update in the statfile -- if possible(?) */
	  get_ut_date(datestr);
	  snprintf(statfile, MAXFILELEN, "%s/%s_%s_run.fit", cfg->statdir, datestr, cfg->statroot);

	  if (update_statfile_offset(statfile, cfg->logfd, 
				     cfg->focmod.term[i].value - focus_delta,
				     cfg->focmod.term[i].value,
				     -1.0, -1.0, -100.0, -100.0) != 0) {
	    syslog(LOG_ERR,"error with update_statfile_offset (focus)");
	  }

	  break;
	}
      } 
    }
  }

  return(0);
}



