
/* 

   grmonty Nph

   Using monte carlo method, estimate spectrum of an appropriately
   scaled GRMHD simulation as a function of latitudinal viewing angle,
   averaged over azimuth.

   Input simulation data is assumed to be in dump format provided by 
   HARM code.  Location of input file is, at present, hard coded
   (see init_sim_data.c).  

   Nph super-photons are generated in total and then allowed
   to propagate.  They are weighted according to the emissivity.
   The photons are pushed by the geodesic equation.
   Their weight decays according to the local absorption coefficient.
   The photons also scatter with probability related to the local
   scattering opacity.  

   The electrons are assumed to have a thermal distribution 
   function, and to be at the same temperature as the protons.

   CFG 8-17-06

   Implemented synchrotron sampling, 22 Jan 07

   fixed bugs in tetrad/compton scattering routines, 31 Jan 07

   Implemented normalization for output, 6 Feb 07

   Separated out different synchrotron sampling routines
   into separate files, 8 Mar 07

   fixed bug in energy recording; bug used Kcon[0] rather than 
   Kcov[0] as energy, 18 Mar 07

   major reorganization to encapsulate problem-dependent parts 5-6 Nov 07

 */

#include "decs.h"

/* defining declarations for global variables */
Params params = { 0 };
struct of_geom **geom;
struct of_tetrads ***tetrads;
double ***n2gens;
int nthreads;
int NPRIM, N1, N2, N3, n_within_horizon;
double F[N_ESAMP + 1], wgt[N_ESAMP + 1], zwgt[N_ESAMP + 1];
int Ns, N_superph_recorded, N_scatt;
int record_photons, bad_bias, invalid_bias;
double Ns_scale, N_superph_made;
struct of_spectrum spect[N_TYPEBINS][N_THBINS][N_EBINS] = { };

double t;
double a;
double R0, Rin, Rout, Rms, Rh, Rmax; // Rh, Rmax used to set stop/record geodesic criteria
double hslope;
double startx[NDIM], stopx[NDIM], dx[NDIM];

double dlE, lE0;
double gam;
double dMsim;
double M_unit, L_unit, T_unit;
double RHO_unit, U_unit, B_unit, Ne_unit, Thetae_unit;
double max_tau_scatt, Ladv, dMact, bias_norm;

// Define default, should be set by problem
double biasTuning = 1.;

int main(int argc, char *argv[])
{
  // motd
  fprintf(stderr, "grmonty. githash: %s\n", xstr(VERSION));
  fprintf(stderr, "notes: %s\n\n", xstr(NOTES));

  double wtime = omp_get_wtime();

  // spectral bin parameters
  dlE = 0.25;         // bin width
  lE0 = log(1.e-12);  // location of first bin, in electron rest-mass units

  // Load parameters
  for (int i=0; i<argc-1; ++i) {
    if ( strcmp(argv[i], "-par") == 0 ) {
      load_par(argv[i+1], &params);
    }
  }

  init_model(argc, argv, &params);

  reset_zones();
  N_superph_made = 0;
  N_superph_recorded = 0;
  N_scatt = 0;
  bad_bias = 0;
  invalid_bias = 0;

  fprintf(stderr, "with synch: %i\n", SYNCHROTRON);
  fprintf(stderr, "with brems: %i\n", BREMSSTRAHLUNG);
  fprintf(stderr, "with compt: %i\n\n", COMPTON);

  int quit_flag = 0;

  if ( COMPTON && (params.fitBias!=0) ) {
    // find a good value for the bias tuning to make 
    // Nscatt / Nmade >~ 1
    fprintf(stderr, "Finding bias...\n");

    double lastscale = 0.;
    time_t starttime = time(NULL);
    int global_quit_flag = 0;

    while (1 == 1) {

      global_quit_flag = 0;
      quit_flag = 0;
      record_photons = 0;
      Ns_scale = 1. * params.fitBiasNs / Ns;
      if (Ns_scale > 1.) Ns_scale = 1.;

      fprintf(stderr, "bias %g ", biasTuning);

      // compute values
      quit_flag = 0;
      #pragma omp parallel firstprivate(quit_flag) shared(global_quit_flag)
      {
        struct of_photon ph;
        while(1) {
          make_super_photon(&ph, &quit_flag);
          if (global_quit_flag || quit_flag) break;

          track_super_photon(&ph);
          #pragma omp atomic
          N_superph_made += 1;

          if ((int)N_superph_made % 1000 == 0 && N_superph_made > 0) {
            if ((int)N_superph_made % 100000 == 0)
	      fprintf(stderr, ".");
            if (1. * N_scatt / N_superph_made > 10.) {
              /* if effectiveness ratio (see below after the omp
                 block) becomes too big, ends the bias tuning
                 algorithm */
              #pragma omp critical
              {
                global_quit_flag = 1;
              }
            }
          }
        }
      }
     
      // get effectiveness
      double ratio = 1. * N_scatt / N_superph_made;
      fprintf(stderr, "ratio = %g\n", ratio);

      // reset state
      reset_zones();
      N_superph_made = 0;
      N_superph_recorded = 0;
      N_scatt = 0;
      bad_bias = 0;

      // continue if good
      if (1 <= ratio && ratio < 3 /* a good ratio range */
          && !global_quit_flag)   /* all other threads should be good for this
                                     ratio range; it's probably fine to skip
                                     this check */
        break;

      if (ratio < 0.1) {
	/* CASE: ratio in [0, 0.1) */
        biasTuning *= sqrt(1./ratio);
        lastscale   = sqrt(1./ratio);
      } else if (ratio < 1.0) {
	/* CASE: ratio in [0.1, 1) */
        biasTuning *= 1.5;
        lastscale   = 1.5;
      } else if (ratio < 3.0) {
	/* CASE: ratio in [1, 3) */
        if (lastscale > 3.0) {
          /* If the ratio falls into this range because of a previous
             aggressive tuning step, "redo" the scaling in the previous step
             with a smaller scaling so ratio will become near 1 */
          biasTuning /= 3.0;
          lastscale  /= 3.0;
        } else
          break;
      } else {
        /* CASE: ratio in [3, inf) */
        if (lastscale > 1.5) {
          /* If the ratio falls into this range because of a previous
             tuning step, soften the previous step so `biasTuning`
             becomes slightly smaller; note that by repeating this
             step, biasTuning may converge to a constant value larger
             than 3 */
          biasTuning /= 1.5;
          lastscale  /= 1.5;
        } else
          break;
      }
    }
    fprintf(stderr, "tuning time %gs\n", (double)(time(NULL) - starttime));
    fprintf(stderr, "biasTuning = %g lastscale = %g\n\n",
            biasTuning, lastscale);
  }

  fprintf(stderr, "\nEntering main loop...\n");
  fprintf(stderr, "(aiming for Ns=%d)\n", Ns);
  summary(NULL, NULL); /* initialize main loop timer */
  
  quit_flag = 0;
  record_photons = 1;
  Ns_scale = 1.;
  invalid_bias = 0;
  
  #pragma omp parallel firstprivate(quit_flag)
  {
    struct of_photon ph;
    while (1) {

      // get pseudo-quanta 
      if (!quit_flag)
        make_super_photon(&ph, &quit_flag);
      if (quit_flag)
        break;
      
      // push them around 
      track_super_photon(&ph);

      // step
      #pragma omp atomic
      N_superph_made += 1;

      // give interim reports on rates
      if ((int)N_superph_made % 100000 == 0 && N_superph_made > 0)
        summary(stderr, NULL);

      // avoid too much scattering; break for all threads immediately
      if (N_scatt > 10000 && 1. * N_scatt / N_superph_made > 10.)
	bad_bias = 1;
      if (bad_bias)
        break;
    }
  }

  summary(stderr, "compute ");

  if (invalid_bias)
    fprintf(stderr, "\n%d invalid bias (bias < 1) skipped\n", invalid_bias);
  
  if (! bad_bias) {
    omp_reduce_spect();
    report_spectrum((int) N_superph_made, &params);
  } else {
    fprintf(stderr, "\nit seems the bias was too high -- aborting.\n");
  }

  wtime = omp_get_wtime() - wtime;
  fprintf(stderr, "Total wallclock time: %g s\n\n", wtime);

  return bad_bias;
}

