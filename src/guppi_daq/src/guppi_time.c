/* guppi_time.c
 *
 * Routines dealing with time conversion.
 */
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "slalib.h"
#include "guppi_error.h"
#include "guppi_defines.h"

// Reimplementation of slalib's slaCaldj routine.
// Calculate MJD from Gregorian date.
// From http://en.wikipedia.org/wiki/Julian_date#Converting_Gregorian_calendar_date_to_Julian_Day_Number
void slaCaldj(int year, int month, int day, double *mjd, int *rv)
{
  int a = (14-month) / 12;
  int y = year + 4800 - a;
  int m = month + 12*a - 3;
  int jd = day + (153*m + 2) / 5 + 365*y + y/4 - y/100 + y/400 - 32045;
  *mjd = jd - 2400000.5;
  *rv = 0;
}

// Reimplementation of slalib's slaDjcl routine.
// Calculate Gregorian date from MJD.
// From http://aa.usno.navy.mil/faq/docs/JD_Formula.php
void slaDjcl(double mjd, int *year, int *month, int *day, double *fracday, int *rv)
{
  int i, j, k, l, n;
  int jd = floor(mjd + 2400000.5);

  l= jd+68569;
  n= 4*l/146097;
  l= l-(146097*n+3)/4;
  i= 4000*(l+1)/1461001;
  l= l-1461*i/4+31;
  j= 80*l/2447;
  k= l-2447*j/80;
  l= j/11;
  j= j+2-12*l;
  i= 100*(n-49)+i+l;

  *year = i;
  *month = j;
  *day = k;
  *fracday = fmod(mjd, 1);
  *rv = 0;
}

int get_current_mjd(int *stt_imjd, int *stt_smjd, double *stt_offs) {
    int rv;
    struct timeval tv;
    struct tm gmt;
    double mjd;

    rv = gettimeofday(&tv,NULL);
    if (rv) { return(GUPPI_ERR_SYS); }

    if (gmtime_r(&tv.tv_sec, &gmt)==NULL) { return(GUPPI_ERR_SYS); }

    slaCaldj(gmt.tm_year+1900, gmt.tm_mon+1, gmt.tm_mday, &mjd, &rv);
    if (rv!=0) { return(GUPPI_ERR_GEN); }

    if (stt_imjd!=NULL) { *stt_imjd = (int)mjd; }
    if (stt_smjd!=NULL) { *stt_smjd = gmt.tm_hour*3600 + gmt.tm_min*60 
        + gmt.tm_sec; }
    if (stt_offs!=NULL) { *stt_offs = tv.tv_usec*1e-6; }

    return(GUPPI_OK);
}

#ifdef NEW_GBT

int get_current_mjd_double(double *mjd) {
    int rv;
    struct timeval tv;
    struct tm gmt;
    double day_usecs;

    if (mjd==NULL)
        return(GUPPI_ERR_SYS);

    rv = gettimeofday(&tv,NULL);
    if (rv) { return(GUPPI_ERR_SYS); }

    if (gmtime_r(&tv.tv_sec, &gmt)==NULL)
        return(GUPPI_ERR_SYS);

    /* Get integer portion of MJD */
    slaCaldj(gmt.tm_year+1900, gmt.tm_mon+1, gmt.tm_mday, mjd, &rv);
    if (rv!=0) { return(GUPPI_ERR_GEN); }

    /* Now calculate fractional day offset (to microsecond resolution) */
    day_usecs = gmt.tm_hour*3600*1e6 + gmt.tm_min*60*1e6 + gmt.tm_sec*1e6 + tv.tv_usec;
    *mjd += day_usecs / (double)(24*60*60*1e6);

    return(GUPPI_OK);
}

#endif

int datetime_from_mjd(long double MJD, int *YYYY, int *MM, int *DD, 
                      int *h, int *m, double *s) {
    int err;
    double fracday;
    
    slaDjcl(MJD, YYYY, MM, DD, &fracday, &err);
    if (err == -1) { return(GUPPI_ERR_GEN); }
    fracday *= 24.0;  // hours
    *h = (int) (fracday);
    fracday = (fracday - *h) * 60.0;  // min
    *m = (int) (fracday);
    *s = (fracday - *m) * 60.0;  // sec
    return(GUPPI_OK);
}

int get_current_lst(double mjd, int *lst_secs) {
#if 0
    int N = 0;
    double gmst, eqeqx, tt;
    double lon, lat, hgt, lst_rad;
    char scope[10]={"GBT"};
    char name[40];

    // Get Telescope information (currently hardcoded for GBT)
    slaObs(N, scope, name, &lon, &lat, &hgt);
    if (fabs(hgt-880.0) > 0.01) {
        printf("Warning!:  SLALIB is not correctly identifying the GBT!\n\n");
    }
    // These calculations use west longitude is negative
    lon = -lon;

    // Calculate sidereal time of Greenwich (in radians)
    gmst = slaGmst(mjd);

    // Equation of the equinoxes (requires TT)
    tt = mjd + (slaDtt(mjd) / 86400.0);
    eqeqx = slaEqeqx(tt);

    // Local sidereal time = GMST + EQEQX + Longitude in radians
    lst_rad = slaDranrm(gmst + eqeqx + lon);

    // Convert to seconds
    *lst_secs = (int) (lst_rad * 86400.0 / 6.283185307179586476925);
#endif

    *lst_secs = 0;

    return(GUPPI_OK);
}

