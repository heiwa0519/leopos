#include "leoorb.h"
/* troposphere model -----------------------------------------------------------
* compute tropospheric delay by standard atmosphere and saastamoinen model
* args   : gtime_t time     I   time
*          double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
*          double humi      I   relative humidity
* return : tropospheric delay (m)
*-----------------------------------------------------------------------------*/
extern double tropmodel(gtime_t time, const double *pos, const double *azel,
	double humi)
{
	const double temp0 = 15.0; /* temparature at sea level */
	double hgt, pres, temp, e, z, trph, trpw;

	if (pos[2]<-100.0 || 1E4<pos[2] || azel[1] <= 0) return 0.0;

	/* standard atmosphere */
	hgt = pos[2]<0.0 ? 0.0 : pos[2];

	pres = 1013.25*pow(1.0 - 2.2557E-5*hgt, 5.2568);
	temp = temp0 - 6.5E-3*hgt + 273.16;
	e = 6.108*humi*exp((17.15*temp - 4684.0) / (temp - 38.45));

	/* saastamoninen model */
	z = PI / 2.0 - azel[1];
	trph = 0.0022768*pres / (1.0 - 0.00266*cos(2.0*pos[0]) - 0.00028*hgt / 1E3) / cos(z);
	trpw = 0.002277*(1255.0 / temp + 0.05)*e / cos(z);
	return trph + trpw;
}
#ifndef IERS_MODEL

static double interpc(const double coef[], double lat)
{
	int i = (int)(lat / 15.0);
	if (i<1) return coef[0]; else if (i>4) return coef[4];
	return coef[i - 1] * (1.0 - lat / 15.0 + i) + coef[i] * (lat / 15.0 - i);
}
static double mapf(double el, double a, double b, double c)
{
	double sinel = sin(el);
	return (1.0 + a / (1.0 + b / (1.0 + c))) / (sinel + (a / (sinel + b / (sinel + c))));
}
static double nmf(gtime_t time, const double pos[], const double azel[],
	double *mapfw)
{
	/* ref [5] table 3 */
	/* hydro-ave-a,b,c, hydro-amp-a,b,c, wet-a,b,c at latitude 15,30,45,60,75 */
	const double coef[][5] = {
		{ 1.2769934E-3, 1.2683230E-3, 1.2465397E-3, 1.2196049E-3, 1.2045996E-3 },
		{ 2.9153695E-3, 2.9152299E-3, 2.9288445E-3, 2.9022565E-3, 2.9024912E-3 },
		{ 62.610505E-3, 62.837393E-3, 63.721774E-3, 63.824265E-3, 64.258455E-3 },

		{ 0.0000000E-0, 1.2709626E-5, 2.6523662E-5, 3.4000452E-5, 4.1202191E-5 },
		{ 0.0000000E-0, 2.1414979E-5, 3.0160779E-5, 7.2562722E-5, 11.723375E-5 },
		{ 0.0000000E-0, 9.0128400E-5, 4.3497037E-5, 84.795348E-5, 170.37206E-5 },

		{ 5.8021897E-4, 5.6794847E-4, 5.8118019E-4, 5.9727542E-4, 6.1641693E-4 },
		{ 1.4275268E-3, 1.5138625E-3, 1.4572752E-3, 1.5007428E-3, 1.7599082E-3 },
		{ 4.3472961E-2, 4.6729510E-2, 4.3908931E-2, 4.4626982E-2, 5.4736038E-2 }
	};
	const double aht[] = { 2.53E-5, 5.49E-3, 1.14E-3 }; /* height correction */

	double y, cosy, ah[3], aw[3], dm, el = azel[1], lat = pos[0] * R2D, hgt = pos[2];
	int i;

	if (el <= 0.0) {
		if (mapfw) *mapfw = 0.0;
		return 0.0;
	}
	/* year from doy 28, added half a year for southern latitudes */
	y = (time2doy(time) - 28.0) / 365.25 + (lat<0.0 ? 0.5 : 0.0);

	cosy = cos(2.0*PI*y);
	lat = fabs(lat);

	for (i = 0; i<3; i++) {
		ah[i] = interpc(coef[i], lat) - interpc(coef[i + 3], lat)*cosy;
		aw[i] = interpc(coef[i + 6], lat);
	}
	/* ellipsoidal height is used instead of height above sea level */
	dm = (1.0 / sin(el) - mapf(el, aht[0], aht[1], aht[2]))*hgt / 1E3;

	if (mapfw) *mapfw = mapf(el, aw[0], aw[1], aw[2]);

	return mapf(el, ah[0], ah[1], ah[2]) + dm;
}
#endif /* !IERS_MODEL */

/* troposphere mapping function ------------------------------------------------
* compute tropospheric mapping function by NMF
* args   : gtime_t t        I   time
*          double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
*          double *mapfw    IO  wet mapping function (NULL: not output)
* return : dry mapping function
* note   : see ref [5] (NMF) and [9] (GMF)
*          original JGR paper of [5] has bugs in eq.(4) and (5). the corrected
*          paper is obtained from:
*          ftp://web.haystack.edu/pub/aen/nmf/NMF_JGR.pdf
*-----------------------------------------------------------------------------*/
extern double tropmapf(gtime_t time, const double pos[], const double azel[],
	double *mapfw)
{
#ifdef IERS_MODEL
	const double ep[] = { 2000,1,1,12,0,0 };
	double mjd, lat, lon, hgt, zd, gmfh, gmfw;
#endif
	trace(6, "tropmapf: pos=%10.6f %11.6f %6.1f azel=%5.1f %4.1f\n",
		pos[0] * R2D, pos[1] * R2D, pos[2], azel[0] * R2D, azel[1] * R2D);

	if (pos[2]<-1000.0 || pos[2]>20000.0) {
		if (mapfw) *mapfw = 0.0;
		return 0.0;
	}
#ifdef IERS_MODEL
	mjd = 51544.5 + (timediff(time, epoch2time(ep))) / 86400.0;
	lat = pos[0];
	lon = pos[1];
	hgt = pos[2] - geoidh(pos); /* height in m (mean sea level) */
	zd = PI / 2.0 - azel[1];

	/* call GMF */
	gmf_(&mjd, &lat, &lon, &hgt, &zd, &gmfh, &gmfw);

	if (mapfw) *mapfw = gmfw;
	return gmfh;
#else
	return nmf(time, pos, azel, mapfw); /* NMF */
#endif
}

