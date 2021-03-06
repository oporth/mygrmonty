#pragma once

// metric parameters
//  note: if METRIC_eKS, then the code will use "exponentialKS" coordinates
//        defined by x^a = { x^0, log(x^1), x^2, x^3 } where x^0, x^1, x^2,
//        x^3 are normal KS coordinates. in addition you must set METRIC_*
//        in order to specify how Xtoijk and gdet_zone should work.
extern int METRIC_eKS,METRIC_BHACMKS;
extern int with_derefine_poles, METRIC_MKS3, METRIC_sphMINK, METRIC_esphMINK;
extern double poly_norm, poly_xt, poly_alpha, mks_smooth; // mmks
extern double mks3R0, mks3H0, mks3MY1, mks3MY2, mks3MP0; // mks3

// coordinate functions
void set_dxdX(const double [NDIM], double [NDIM][NDIM]);
void bl_to_ks(const double X[NDIM], double ucon_bl[NDIM], double ucon_ks[NDIM]);
void vec_from_ks(const double X[NDIM], double v_ks[NDIM], double v_nat[NDIM]);

static inline __attribute__((always_inline)) void set_dxdX_metric(const double X[NDIM], double dxdX[NDIM][NDIM], int metric)
{
  // Jacobian with respect to KS basis where X is given in
  // non-KS basis
  MUNULOOP dxdX[mu][nu] = 0.;

  if ( METRIC_sphMINK ) {
    MUNULOOP dxdX[mu][nu] = mu==nu ? 1 : 0;
    return;
  } else if ( METRIC_esphMINK ) {
    MUNULOOP dxdX[mu][nu] = mu==nu ? 1 : 0;
    dxdX[1][1] = exp(X[1]);
    return;
  }

  if ( METRIC_eKS && metric==0 ) {

    MUNULOOP dxdX[mu][nu] = mu==nu ? 1 : 0;
    dxdX[1][1] = exp(X[1]);
    dxdX[2][2] = M_PI;

  } else if ( METRIC_MKS3 ) {

    // mks3 ..
    dxdX[0][0] = 1.;
    dxdX[1][1] = exp(X[1]);
    dxdX[2][1] = -(pow(2.,-1. + mks3MP0)*exp(X[1])*mks3H0*mks3MP0*(mks3MY1 -
              mks3MY2)*pow(M_PI,2)*pow(exp(X[1]) + mks3R0,-1 - mks3MP0)*(-1 +
              2*X[2])*1./tan((mks3H0*M_PI)/2.)*pow(1./cos(mks3H0*M_PI*(-0.5 + (mks3MY1 +
              (pow(2,mks3MP0)*(-mks3MY1 + mks3MY2))/pow(exp(X[1]) + mks3R0,mks3MP0))*(1 -
              2*X[2]) + X[2])),2));
    dxdX[2][2]= (mks3H0*pow(M_PI,2)*(1 - 2*(mks3MY1 + (pow(2,mks3MP0)*(-mks3MY1 +
             mks3MY2))/pow(exp(X[1]) + mks3R0,mks3MP0)))*1./tan((mks3H0*M_PI)/2.)*
             pow(1./cos(mks3H0*M_PI*(-0.5 + (mks3MY1 + (pow(2,mks3MP0)*(-mks3MY1 +
             mks3MY2))/pow(exp(X[1]) + mks3R0,mks3MP0))*(1 - 2*X[2]) + X[2])),2))/2.;
    dxdX[3][3] = 1.;

  } else if ( with_derefine_poles ) {

    // mmks
    dxdX[0][0] = 1.;
    dxdX[1][1] = exp(X[1]);
    dxdX[2][1] = -exp(mks_smooth*(startx[1]-X[1]))*mks_smooth*(
      M_PI/2. -
      M_PI*X[2] +
      poly_norm*(2.*X[2]-1.)*(1+(pow((-1.+2*X[2])/poly_xt,poly_alpha))/(1 + poly_alpha)) -
      1./2.*(1. - hslope)*sin(2.*M_PI*X[2])
      );
    dxdX[2][2] = M_PI + (1. - hslope)*M_PI*cos(2.*M_PI*X[2]) +
      exp(mks_smooth*(startx[1]-X[1]))*(
        -M_PI +
        2.*poly_norm*(1. + pow((2.*X[2]-1.)/poly_xt,poly_alpha)/(poly_alpha+1.)) +
        (2.*poly_alpha*poly_norm*(2.*X[2]-1.)*pow((2.*X[2]-1.)/poly_xt,poly_alpha-1.))/((1.+poly_alpha)*poly_xt) -
        (1.-hslope)*M_PI*cos(2.*M_PI*X[2])
        );
    dxdX[3][3] = 1.;

  } else if (METRIC_BHACMKS) {
     // fprintf(stderr, "BHAC\n");
     //BHAC MKS
    dxdX[0][0] = 1.;
    dxdX[1][1] = exp(X[1]);
    dxdX[2][2] = 1.+ hslope * cos(2. * X[2]);
    dxdX[3][3] = 1.;
  }else {

    // mks
    dxdX[0][0] = 1.;
    dxdX[1][1] = exp(X[1]);
    dxdX[2][2] = M_PI - (hslope - 1.)*M_PI*cos(2.*M_PI*X[2]);
    dxdX[3][3] = 1.;

  }

}

// cribbed from ipole
static inline __attribute__((always_inline)) void gcov_bl(double r, double th, double gcov[NDIM][NDIM])
{
  double sth, cth, s2, a2, r2, DD, mu;
  sth = fabs(sin(th));
  s2 = sth * sth;
  cth = cos(th);
  a2 = a * a;
  r2 = r * r;
  DD = 1. - 2. / r + a2 / r2;
  mu = 1. + a2 * cth * cth / r2;

  MUNULOOP gcov[mu][nu] = 0.;
  // Compute BL metric from BL coordinates
  gcov[0][0] = -(1. - 2. / (r * mu));
  gcov[0][3] = -2. * a * s2 / (r * mu);
  gcov[3][0] = gcov[0][3];
  gcov[1][1] = mu / DD;
  gcov[2][2] = r2 * mu;
  gcov[3][3] = r2 * sth * sth * (1. + a2 / r2 + 2. * a2 * s2 / (r2 * r * mu));

}

// compute KS metric at point (r,th) in KS coordinates (cyclic in t, ph)
static inline __attribute__((always_inline)) void gcov_ks(double r, double th, double gcov[NDIM][NDIM])
{
  double cth = cos(th);
  double sth = sin(th);

  double s2 = sth*sth;
  double rho2 = r*r + a*a*cth*cth;

  double gcov01 = 2.*r/rho2;
  double gcov03 = -a*s2*(   gcov01);
  double gcov13 = -a*s2*(1.+gcov01);

  // compute ks metric for ks coordinates (cyclic in t,phi)
  gcov[0][0] = -1. + 2.*r/rho2;
  gcov[0][1] = gcov01;
  gcov[0][2] = 0;
  gcov[0][3] = gcov03;

  gcov[1][0] = gcov01;
  gcov[1][1] = 1. + 2.*r/rho2;
  gcov[1][2] = 0;
  gcov[1][3] = gcov13;

  gcov[2][0] = 0;
  gcov[2][1] = 0;
  gcov[2][2] = rho2;
  gcov[2][3] = 0;

  gcov[3][0] = gcov03;
  gcov[3][1] = gcov13;
  gcov[3][2] = 0;
  gcov[3][3] = s2*(rho2 + a*a*s2*(1. + 2.*r/rho2));
}


int invert_matrix(double Am[][NDIM], double Aminv[][NDIM]);
int LU_decompose(double A[][NDIM], int permute[]);
void LU_substitution(double A[][NDIM], double B[], int permute[]);

