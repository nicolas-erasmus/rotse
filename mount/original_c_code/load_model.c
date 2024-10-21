/* load_model ================================================
 *
 * Purpose: Load in a tpoint pointing model file into a model_st structure.
 *         
 * int load_model(char *filename, struct model_st *model);
 *
 * Created: 2002-04-02 E. Rykoff -- first official version
 *
 *===========================================================*/



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


int transl_name(char *name);


int load_model(char *filename, struct model_st *model)
{
  FILE *fp;
  int i,j;
  char dummy1,dummy2;
  char line[100];
  char name[10];

  /* First thing, clear the model */
  model->caption[0] = '\0';
  model->method = '\0';
  model->observations = 0;
  model->sky_rms = 0.0;
  model->refr_a = model->refr_b = 0.0;
  for (i=0;i<MAX_TERMS;i++) {
    model->term[i].parallel = 0;
    model->term[i].type = -1;
    model->term[i].value = model->term[i].sigma = 0.0;
  }


  /* Now open the file */
  
  if ((fp = fopen(filename, "r")) == NULL) {
    syslog(LOG_ERR, "Error opening %s: %m\n",filename);
    return(-1);
  }

  /* First line is the caption */
  j=0;
  fgets(model->caption, 81, fp);

  /* Next line has a whole bunch */
  fscanf(fp, "%c %5d %9lf %9lf %9lf", &(model->method), &(model->observations),
	 &(model->sky_rms), &(model->refr_a), &(model->refr_b));

  dummy1 = fgetc(fp);
  /* Now loop over the terms */
  i = 0;
  while (i < MAX_TERMS) {
    fgets(line,80,fp);
    if (strncmp("END", line, 3) == 0) break;

    sscanf(line,"%c%c%8s%10lf%10lf", &dummy1, &dummy2, name,
	   &(model->term[i].value), &(model->term[i].sigma));  
    if (dummy1 == '&') model->term[i].parallel = 1;
    if ((model->term[i].type = transl_name(name)) == -1) {
      syslog(LOG_ERR,"Error: Unrecognized term type %s\n",name);
      return(-1);
    }
    i++;   
  }
  if (i == MAX_TERMS) {
    syslog(LOG_ERR,"Error: we have reached the maximum number of terms\n");
    return(-1);
  }


  fclose(fp);
  return(0);

}

int transl_name(char *name)
{
  int len;
  int i;

  static struct term_name term_names[N_TERMTYPES] = {
    {"IH", IH},
    {"ID", ID},
    {"NP", NP},
    {"CH", CH},
    {"ME", ME},
    {"MA", MA},
    {"FO", FO},
    {"TF", TF},
    {"TX", TX}
  };


  i=0;
  while (i < N_TERMTYPES) {
    if ((len = strlen(name)) == strlen(term_names[i].name)) {
      if (strncmp(name, term_names[i].name, len) == 0) {
	return(term_names[i].type);
      }
    }
    i++;
  }
  
  return(-1);

}
