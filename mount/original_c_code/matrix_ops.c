#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <syslog.h>

#include "matrix_ops.h"

int cross_prod(struct matrix a, struct matrix b, struct matrix *c)
{
  if (a.nrows != 3 || a.ncols != 1 || b.nrows != 3 || b.ncols != 1) {
    fprintf(stderr,"Cannot cross_product\n");
    return(-1);
  }

  initmat(c,1,3);

  c->val[0][0] = a.val[0][1]*b.val[0][2] - b.val[0][1]*a.val[0][2];
  c->val[0][1] = b.val[0][0]*a.val[0][2] - a.val[0][0]*b.val[0][2];
  c->val[0][2] = a.val[0][0]*b.val[0][1] - b.val[0][0]*a.val[0][1];

  return(0);
}



void normalize_mat(struct matrix *vec)
{
  int i;
  double total = 0.0;

  for (i=0;i<(vec->nrows);i++) {
    total = total + vec->val[0][i]*vec->val[0][i];
  }
  for (i=0;i<vec->nrows;i++) {
    vec->val[0][i] = vec->val[0][i] / sqrt(total);
  }

}

int mult_mat(struct matrix a, struct matrix b, struct matrix *c)
{
  int i,j,k;

  if (a.ncols != b.nrows) {
    printf("cannot multiply\n");
    return(-1);
  }

  initmat(c, b.ncols, a.nrows);

  for (i=0;i<c->ncols;i++) {
    for (j=0;j<c->nrows;j++) {
      c->val[i][j] = 0.0;
      for (k=0;k<a.ncols;k++) {
	/*syslog(LOG_INFO,"c[%d][%d] += a[%d][%d] * b[%d][%d]",i,j,k,j,i,k);
	  syslog(LOG_INFO,"c[%d][%d] += %f * %f", i,j,a.val[k][j], b.val[i][k]);*/
	c->val[i][j] = c->val[i][j] + a.val[k][j] * b.val[i][k];
      }
      /*syslog(LOG_INFO,"c[%d][%d] = %f",i,j,c->val[i][j]);*/
    }
  }

  return(0);
}

int invertmat(struct matrix a, struct matrix *b)
{
  /* only works on 3x3 matrix */
  int i,j,k;
  double det_a;
  struct matrix adjunct;
  struct matrix temp;

  det_a = determinant(a);
  if (det_a == 0) {
    printf("Not invertable\n");
    return(-1);
  }
  initmat(&temp,3,3);
  initmat(&adjunct,3,3);
  for (i=0;i<3;i++) {
    for (j=0;j<3;j++) {
      copymat(a,&temp);
      for (k=0;k<3;k++) {
	temp.val[k][j] = 0.0;
	temp.val[i][k] = 0.0;
      }
      temp.val[i][j] = 1.0;
      adjunct.val[i][j] = determinant(temp);
    }
  }
  printmatrix(adjunct);

  for (i=0;i<3;i++) {
    for (j=0;j<3;j++) {
      b->val[i][j] = adjunct.val[j][i] / det_a;
    }
  }

  freemat(&adjunct);
  freemat(&temp);

  return(0);

}

void copymat(struct matrix a, struct matrix *b)
{
  int i,j;
  for (i=0;i<a.ncols;i++) {
    for (j=0;j<a.nrows;j++) {
      b->val[i][j] = a.val[i][j];
    }
  }

}

void freemat(struct matrix *mat)
{
  int i;

  for (i=0;i<mat->ncols;i++) {
    free(mat->val[i]);
  }
  free(mat->val);

  mat->nrows = 0;
  mat->ncols = 0;

}

void initmat(struct matrix *mat, int ncols, int nrows)
{
  int i,j;

  
  mat->nrows = nrows;
  mat->ncols = ncols;

  mat->val = malloc(ncols * sizeof(double *));
  for (i=0; i <ncols; i++) {
    mat->val[i] = malloc(nrows * sizeof(double));
  }

  for (i=0;i<ncols;i++) {
    for (j=0;j<nrows;j++) {
      mat->val[i][j] = 0.0;
    }
  }
}

void printmatrix(struct matrix mat)
{
  int i,j;
  
  for (j=0;j<mat.nrows;j++) {
    printf("( ");
    for (i=0;i<mat.ncols;i++) {
      printf("%f ", mat.val[i][j]);
      /*printf("(%d,%d) %f\n",i,j,mat.val[i][j]);*/
    }
    printf(")\n");
  }

}

double determinant(struct matrix mat)
{
  double det;
  /* only works on 3x3 */

  det = mat.val[0][0] * (mat.val[1][1] * mat.val[2][2] - mat.val[2][1] * mat.val[1][2]);
  det = det - mat.val[0][1]*(mat.val[1][0]*mat.val[2][2] - mat.val[2][0]*mat.val[1][2]);
  det = det + mat.val[0][2]*(mat.val[1][0]*mat.val[2][1] - mat.val[2][0]*mat.val[1][1]);

  return(det);
}
