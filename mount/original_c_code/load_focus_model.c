/* load_focus_model ===============================================
 *
 * Purpose: Load in the bilinear focus model constants from a given
 *          configuration file.
 *
 * int load_focus_model(char *confile, struct focus_model_st *focmod);
 *
 * Created: 2002-04-02 E, Rykoff -- first official version
 * Updated: 2002-05-09 E. Rykoff -- changed focus model to arbitrary terms
 * ================================================================*/


/* system includes */

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
#include "schierd.h"

#define MAXTOKENS 3
#define MAXLINE 1024

int check_focus_str(char *focstr);


int load_focus_model(char *confile, struct focus_model_st *focmod)
{
  FILE *fp;
  int keyval;
  int ntok;
  char line[MAXLINE];
  char *(tokens[MAXTOKENS]);
  char *endptr = NULL;
  double dtmp;
  int i;

  enum {
    C_TERM, C_NKEYS
  };

  static struct keyword keytable[C_NKEYS] =
  {
    {"term", C_TERM}
  };


  focmod->nterms = 0;
  for (i=0;i<MAX_TERMS;i++) {
    bzero(focmod->term[i].str,sizeof(focmod->term[i].str));
    focmod->term[i].value = 0.0;
  }

  if ((fp = fopen(confile, "r")) == NULL) {
    syslog(LOG_ERR, "could not open %s: %m", confile);
    return (-1);
  }
  syslog(LOG_INFO,"Using focus model file %s", confile);

  dtmp = 0.0;
  while ((ntok = gettokens(fp, line, MAXLINE, tokens, MAXTOKENS)) != 0) {
    switch(keyval = getkey(tokens[0], keytable, C_NKEYS)) {
    case C_TERM:
      if (ntok != 3) {
	syslog(LOG_ERR,"%s entry format error", tokens[0]);
	return(-1);
      }
      strncpy(focmod->term[focmod->nterms].str, tokens[1], MAX_FOCUS_CHARS);

      dtmp = strtod(tokens[2], &endptr);
      if (*endptr != '\0' || (dtmp == 0.0 && endptr == tokens[1])) {
	syslog(LOG_ERR,"%s value %s not valid", tokens[0], tokens[1]);
	return(-1);
      }
      focmod->term[focmod->nterms].value = dtmp;
      dtmp = 0.0;
      
      focmod->nterms++;
      break;
    default:
      syslog(LOG_ERR,"Unrecognized keyval: %d, %s", keyval, tokens[0]);
    }
  }

  for (i=0;i<focmod->nterms;i++) {
    if (check_focus_str(focmod->term[i].str) != 0) {
      syslog(LOG_ERR,"Invalid focus term %s",focmod->term[i].str);
      return(-1);
    }
    if (isnan(focmod->term[i].value)) {
      syslog(LOG_ERR,"term %s value not valid",focmod->term[i].str);
      return(-1);
    }
  }

  return(0);
}

int check_focus_str(char *focstr)
{
  int i;

  for (i=0;i<strlen(focstr);i++) {
    if (strchr(FOCUS_TERMS,focstr[i]) == NULL) {
      syslog(LOG_ERR,"Focus term %c invalid", focstr[i]);
      return(-1);
    }
  }

  return(0);
}
