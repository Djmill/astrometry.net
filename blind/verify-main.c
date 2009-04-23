/*
 This file is part of the Astrometry.net suite.
 Copyright 2009 Dustin Lang, David W. Hogg.

 The Astrometry.net suite is free software; you can redistribute
 it and/or modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation, version 2.

 The Astrometry.net suite is distributed in the hope that it will be
 useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with the Astrometry.net suite ; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
*/

/**
 Runs the verification procedure in stand-alone mode.
 */
#include <sys/param.h>

#include "matchfile.h"
#include "matchobj.h"
#include "index.h"
#include "xylist.h"
#include "rdlist.h"
#include "log.h"
#include "errors.h"
#include "mathutil.h"

#include "verify.h"
//#include "verify2.h"

#define SIGN(x) (((x) >= 0) ? (1) : (-1))

static const char* OPTIONS = "hvi:m:f:r:p";

static void print_help(const char* progname) {
	printf("Usage:   %s\n"
	       "   -m <match-file>\n"
		   "   -f <xylist-file>\n"
		   "  (    -i <index-file>\n"
		   "   OR  -r <index-rdls>\n"
		   "  )\n"
           "   [-v]: verbose\n"
	       "\n", progname);
}

static int get_xy_bin(double x, double y,
					  double fieldW, double fieldH,
					  int nw, int nh) {
	int ix, iy;
	ix = (int)floor(nw * x / fieldW);
	ix = MAX(0, MIN(nw-1, ix));
	iy = (int)floor(nh * y / fieldH);
	iy = MAX(0, MIN(nh-1, iy));
	return iy * nw + ix;
}

int Npaths = 0;

static void explore_path(il** reflists, dl** problists, int i, int NT, int NR,
						 int* theta, double* logprobs, 
						 bool* refused, int mu,
						 double distractor, double logbg) {
	int j;
	double logprob;
	FILE* f = stderr;
	double logd = log(distractor + (1.0-distractor)*mu / (double)NR) + logbg;

	if (i == NT) {

		/*
		 fprintf(f, "allpaths.append(array([");
		 for (j=0; j<NT; j++)
		 fprintf(f, "%g,", logprobs[j]);
		 fprintf(f, "]))\n");
		 */
		fprintf(f, "alllogprobs.append(%g)\n", logprobs[i-1]);
		Npaths++;
		return;
	}

	if (i == 0)
		logprob = 0.0;
	else
		logprob = logprobs[i-1];

	for (j=0; reflists[i] && j<il_size(reflists[i]); j++) {
		int refi;
		refi = il_get(reflists[i], j);
		if (refused[refi])
			continue;

		logprobs[i] = logprob + dl_get(problists[i], j) - logbg;
		theta[i] = refi;
		//fprintf(f, "plot([%i, %i], [%g, %g], 'r-')\n", i, i+1, logprob, logprobs[i]);
		//fprintf(f, "pathsx.append(%i)\npathsy.append(%g)\n", i+1, logprobs[i]);
		fprintf(f, "pathsx.append([%i, %i])\npathsy.append([%g, %g])\n", i, i+1, logprob, logprobs[i]);
		fprintf(f, "pathst.append(%i)\n", refi);
		refused[refi] = TRUE;
		explore_path(reflists, problists, i+1, NT, NR, theta, logprobs,
					 refused, mu+1, distractor, logbg);
		refused[refi] = FALSE;
	}

	logprobs[i] = logprob + logd - logbg;
	theta[i] = -1;
	//fprintf(f, "plot([%i, %i], [%g, %g], 'r-')\n", i, i+1, logprob, logprobs[i]);
	//fprintf(f, "pathsx.append(%i)\npathsy.append(%g)\n", i+1, logprobs[i]);
	fprintf(f, "pathsx.append([%i, %i])\npathsy.append([%g, %g])\n", i, i+1, logprob, logprobs[i]);
	fprintf(f, "pathst.append(%i)\n", -1);
	explore_path(reflists, problists, i+1, NT, NR, theta, logprobs,
				 refused, mu, distractor, logbg);
}

static void add_radial_and_tangential_correction(const double* in,
												 double r, double t,
												 const double* qc,
												 double* out,
												 int N) {
	int i;
	for (i=0; i<N; i++) {
		double rdir[2];
		// radial vector
		rdir[0] = in[2*i+0] - qc[0];
		rdir[1] = in[2*i+1] - qc[1];
		out[2*i+0] = in[2*i+0] - r * rdir[0] + t * rdir[1];
		out[2*i+1] = in[2*i+1] - r * rdir[1] - t * rdir[0];
	}
}


int main(int argc, char** args) {
	int argchar;
	int loglvl = LOG_MSG;
	char* indexfn = NULL;
	char* matchfn = NULL;
	char* xyfn = NULL;
	char* rdfn = NULL;

	index_t* index = NULL;
	matchfile* mf;
	MatchObj* mo;
	verify_field_t* vf;
	starxy_t* fieldxy;
	xylist_t* xyls;
	rdlist_t* rdls;

	double pix2 = 1.0;
	double distractors = 0.25;
	double fieldW=0, fieldH=0;
	double logbail = log(1e-100);
	double logkeep = log(1e12);
	double logaccept = HUGE_VAL;
	bool growvariance = TRUE;
	bool fake = FALSE;
	double logodds;
	bool do_paths = FALSE;

	while ((argchar = getopt(argc, args, OPTIONS)) != -1)
		switch (argchar) {
		case 'p':
			do_paths = TRUE;
			break;
		case 'r':
			rdfn = optarg;
			break;
		case 'f':
			xyfn = optarg;
			break;
        case 'i':
			indexfn = optarg;
			break;
		case 'm':
			matchfn = optarg;
			break;
		case 'h':
			print_help(args[0]);
			exit(0);
		case 'v':
			loglvl++;
			break;
		}

	log_init(loglvl);

	if (!(indexfn || rdfn) || !matchfn || !xyfn) {
		logerr("You must specify (index (-i) or index rdls (-r)) and matchfile (-m) and field xylist (-f).\n");
		print_help(args[0]);
		exit(-1);
	}

	mf = matchfile_open(matchfn);
	if (!mf) {
		ERROR("Failed to read match file %s", matchfn);
		exit(-1);
	}

	xyls = xylist_open(xyfn);
	if (!xyls) {
		ERROR("Failed to open xylist %s", xyfn);
		exit(-1);
	}
	// don't need these...
	xylist_set_include_flux(xyls, FALSE);
	xylist_set_include_background(xyls, FALSE);
	fieldW = xylist_get_imagew(xyls);
	fieldH = xylist_get_imageh(xyls);

    logmsg("Field W,H = %g, %g\n", fieldW, fieldH);

	mo = matchfile_read_match(mf);
	if (!mo) {
		ERROR("Failed to read object from match file.");
		exit(-1);
	}
    mo->wcstan.imagew = fieldW;
    mo->wcstan.imageh = fieldH;

	fieldxy = xylist_read_field(xyls, NULL);
	if (!fieldxy) {
		ERROR("Failed to read a field from xylist %s", xyfn);
		exit(-1);
	}

	if (indexfn) {
		index = index_load(indexfn, 0);
		if (!index) {
			ERROR("Failed to open index %s", indexfn);
			exit(-1);
		}

		pix2 += square(index->meta.index_jitter / mo->scale);

	} else {
		double indexjitter;

		rdls = rdlist_open(rdfn);
		if (!rdls) {
			ERROR("Failed to open rdlist %s", rdfn);
			exit(-1);
		}

		// HACK
		indexjitter = 1.0; // arcsec.
		pix2 += square(indexjitter / mo->scale);
	}

	logmsg("Pixel jitter: %g pix\n", sqrt(pix2));

	vf = verify_field_preprocess(fieldxy);

	if (index) {
		mo->logodds = 0.0;

		verify_hit(index->starkd, index->meta.cutnside,
				   mo, NULL, vf,
				   pix2, distractors, fieldW, fieldH,
				   logbail, logkeep, logaccept, growvariance,
				   index_get_quad_dim(index), fake);

		logodds = mo->logodds;

		index_close(index);

	} else {
		int cutnside;
		int cutnsweeps;
		int indexid;
		int uni_nw, uni_nh;
		int* perm;
		double* testxy;
		double* refxy;
		int i, j, k, NT, NR;
		double* sigma2s = NULL;
		rd_t* rd;
		double* bincenters;
		int* binids;
		double effA;
		double qc[2], Q2;
		double ror, newror;
		bool* goodbins = NULL;
		int Ngoodbins;

		// -get reference stars
		rd = rdlist_read_field(rdls, NULL);
		if (!rd) {
			ERROR("Failed to read rdls field");
			exit(-1);
		}
		NR = rd_n(rd);
		refxy = malloc(2 * NR * sizeof(double));
		for (i=0; i<NR; i++) {
			double ra, dec;
			ra  = rd_getra (rd, i);
			dec = rd_getdec(rd, i);
			if (!tan_radec2pixelxy(&(mo->wcstan), ra, dec, refxy + 2*i, refxy + 2*i + 1)) {
				ERROR("rdls point projects to wrong side of sphere!");
				exit(-1);
			}
		}
		// -remove the ref star closest to each quad star.
		for (i=0; i<mo->dimquads; i++) {
			double qxy[2];
			int besti = -1;
			double bestd2 = HUGE_VAL;
			if (!tan_xyzarr2pixelxy(&(mo->wcstan), mo->quadxyz + 3*i, qxy, qxy+1)) {
				ERROR("quad star projects to wrong side of sphere!");
				exit(-1);
			}
			logmsg("Ref quad star %i is at (%.1f, %.1f)\n", i, qxy[0], qxy[1]);
			for (j=0; j<NR; j++) {
				double d2 = distsq(qxy, refxy + 2*j, 2);
				if (d2 < bestd2) {
					bestd2 = d2;
					besti = j;
				}
			}
			logmsg("Ref star %i is closest: (%.1f, %.1f)\n", besti, refxy[2*besti+0], refxy[2*besti+1]);
			// remove it!
			memmove(refxy + 2*besti, refxy + 2*(besti + 1),
					2*(NR - besti - 1) * sizeof(double));
			NR--;
		}
		logmsg("Reference stars: %i\n", NR);

		indexid = mo->indexid;
		if (index_get_missing_cut_params(indexid, &cutnside, &cutnsweeps, NULL, NULL, NULL)) {
			ERROR("Failed to get index cut parameters for index id %i", indexid);
			exit(-1);
		}

		/*
		 verify_hit2(refxy, NULL, NR, cutnside, mo, NULL, vf,
		 pix2, distractors, fieldW, fieldH,
		 logbail, logaccept, HUGE_VAL, grow_variance);
		 */

		NT = verify_get_test_stars(vf, mo, pix2, do_gamma, fake,
								   &sigma2s, &perm);

		// -uniformize field stars
		indexid = mo->indexid;
		if (index_get_missing_cut_params(indexid, &cutnside, &cutnsweeps, NULL, NULL, NULL)) {
			ERROR("Failed to get index cut parameters for index id %i", indexid);
			exit(-1);
		}
		verify_get_uniformize_scale(cutnside, mo->scale, fieldW, fieldH, &uni_nw, &uni_nh);
		logmsg("Uniformizing test stars into %i x %i bins.\n", uni_nw, uni_nh);
		verify_uniformize_field(vf->xy, perm, NT, fieldW, fieldH, uni_nw, uni_nh, NULL, &binids);
		bincenters = verify_uniformize_bin_centers(fieldW, fieldH, uni_nw, uni_nh);
		verify_get_quad_center(vf, mo, qc, &Q2);

		ror = sqrt(Q2 * (1 + fieldW*fieldH*(1 - distractors) / (2. * M_PI * NR * pix2)));
		logmsg("Radius of relevance is %.1f\n", ror);

		verify_apply_radius_of_relevance(ror, uni_nw, uni_nh, 
										 //qc, Q2, fieldW, fieldH, distractors, NR, pix2,

		// Approximate cutting up the image by measuring distance to the bin centers.
		goodbins = malloc(uni_nw * uni_nh * sizeof(bool));
		Ngoodbins = 0;

		for (i=0; i<(uni_nw * uni_nh); i++) {
			double binr = sqrt(distsq(bincenters + 2*i, qc, 2));
			goodbins[i] = (binr < ror);
			if (goodbins[i])
				Ngoodbins++;
		}
		// Remove test stars in irrelevant bins...
		k = 0;
		for (i=0; i<NT; i++) {
			if (!goodbins[binids[i]])
				continue;
			cutperm[k] = cutperm[i];
			k++;
		}
		NT = k;
		logmsg("After removing %i/%i irrelevant bins: %i test stars.\n", (uni_nw*uni_nh)-Ngoodbins, uni_nw*uni_nh, NT);

		// Effective area: A * proportion of good bins.
		effA = fieldW * fieldH * Ngoodbins / (double)(uni_nw * uni_nh);

		// -remove reference stars in bad bins.
		k = 0;
		for (i=0; i<NR; i++) {
			int binid = get_xy_bin(refxy[2*i], refxy[2*i+1], fieldW, fieldH, uni_nw, uni_nh);
			if (!goodbins[binid])
				continue;
			if (i != k)
				memcpy(refxy + 2*k, refxy + 2*i, 2*sizeof(double));
			k++;
		}
		NR = k;
		logmsg("After removing irrelevant ref stars: %i ref stars.\n", NR);

		// New ROR is...
		newror = sqrt(Q2 * (1 + effA*(1 - distractors) / (2. * M_PI * NR * pix2)));
		logmsg("ROR changed from %g to %g\n", ror, newror);

		free(goodbins);
		free(binids);
		free(bincenters);

		{
			double d = distractors;
			// Predicted optimal number of reference stars:
			int mmax = (int)round(exp(log(effA*(1-d)/(2*M_PI*pix2)) + d*log(d)/(1-d) + (M_PI*Q2 + 1)/effA * log(M_PI*Q2 / (M_PI*Q2 + effA))));
			logmsg("mmax = %i\n", mmax);
			logmsg("first term: %g\n", effA*(1-d)/(2*M_PI*pix2));
			logmsg("second term: %g\n", exp(d*log(d) / (1-d)));
			logmsg("third term: %g\n", exp((M_PI*Q2 + 1)/effA * log(M_PI*Q2 / (M_PI*Q2 + effA))));

			// Predicted number of reference stars to allow the
			// accept threshold to be reached.
			double t1 = d*log(d) + (1-d)*(log(effA*(1-d)/(2*M_PI*pix2)) + (M_PI*Q2 + 1)/effA * log(M_PI*Q2 / (M_PI*Q2 + effA)));
			for (i=1; i<1000000; i++) {
				double logM = i*t1 - i*(1-d)*log(i);
				if (logM > logkeep) {
					logmsg("m = %i: M = %g\n", i, exp(logM));
					break;
				}
			}

			//NR = MIN(NR, 2 * i + 10);
			//logmsg("Setting NR to %i\n", NR);
		}

		


		// -remove test quad stars, and grab xy positions
		testxy = malloc(2 * NT * sizeof(double));
		k = 0;
		for (i=0; i<NT; i++) {
			int starindex = cutperm[i];
			if (!fake) {
				bool inquad = FALSE;
				for (j=0; j<mo->dimquads; j++)
					if (starindex == mo->field[j]) {
						inquad = TRUE;
						break;
					}
				if (inquad)
					continue;
			}
			//cutperm[k] = cutperm[i];
			starxy_get(vf->field, cutperm[i], testxy + 2*k);
			k++;
		}
		NT = k;

		free(cutperm);

		// -compute sigma2s
		sigma2s = verify_compute_sigma2s_arr(testxy, NT, qc, Q2, pix2, !fake);

		logmsg("Test stars: %i\n", NT);

		FILE* f = stderr;

		fprintf(f, "distractor = %g\nNR=%i\nNT=%i\n", distractors, NR, NT);
		fprintf(f, "W=%i\nH=%i\n", (int)fieldW, (int)fieldH);
		fprintf(f, "effA=%g\n", effA);

		fprintf(f, "quadxy = array([");
		for (i=0; i<mo->dimquads; i++)
			fprintf(f, "[%g,%g],", mo->quadpix[2*i+0], mo->quadpix[2*i+1]);
		fprintf(f, "])\n");

		fprintf(f, "testxy = array([");
		for (i=0; i<NT; i++)
			fprintf(f, "[%g,%g],", testxy[2*i+0], testxy[2*i+1]);
		fprintf(f, "])\n");

		fprintf(f, "sigmas = array([");
		for (i=0; i<NT; i++)
			fprintf(f, "%g,", sqrt(sigma2s[i]));
		fprintf(f, "])\n");

		double* rs2 = verify_compute_sigma2s_arr(refxy, NR, qc, Q2, pix2, !fake);
		fprintf(f, "refsigmas = array([");
		for (i=0; i<NR; i++)
			fprintf(f, "%g,", sqrt(rs2[i]));
		fprintf(f, "])\n");
		free(rs2);

		fprintf(f, "refxy = array([");
		for (i=0; i<NR; i++)
			fprintf(f, "[%g,%g],", refxy[2*i+0], refxy[2*i+1]);
		fprintf(f, "])\n");

		fprintf(f, "cutx = array([");
		for (i=0; i<=uni_nw; i++)
			fprintf(f, "%g,", i * fieldW / (float)uni_nw);
		fprintf(f, "])\n");

		fprintf(f, "cuty = array([");
		for (i=0; i<=uni_nh; i++)
			fprintf(f, "%g,", i * fieldH / (float)uni_nh);
		fprintf(f, "])\n");

		double* all_logodds;
		int* theta;
		int besti;
		double worst;

		logodds = verify_star_lists(refxy, NR, testxy, sigma2s, NT,
									effA, distractors, logbail, logaccept,
									&besti, &all_logodds, &theta, &worst);

		fprintf(f, "besti = %i\n", besti);

		fprintf(f, "worstlogodds = %g\n", worst);

		fprintf(f, "logodds = array([");
		for (i=0; i<NT; i++)
			fprintf(f, "%g,", all_logodds[i]);
		fprintf(f, "])\n");

		fprintf(f, "theta = array([");
		for (i=0; i<NT; i++)
			fprintf(f, "%i,", theta[i]);
		fprintf(f, "])\n");

		// compare observed sigmas to expected...
		fprintf(f, "obssigmas=array([");
		for (i=0; i<NT; i++) {
			double d2;
			if (theta[i] == -1)
				continue;
			d2 = distsq(testxy + 2*i, refxy + 2*theta[i], 2);
			fprintf(f, "[%g,%g],", sigma2s[i], d2);
		}
		fprintf(f, "])\n");


		{
			// introduce known radial and tangential terms and see if we can recover them...
			//add_radial_and_tangential_correction(testxy, -0.01, -0.01, qc, testxy, NT);

			// What is the ML correction to rotation and scaling?
			// (shear, translation?  distortion?)
			// -> may need all matches, not just nearest neighbour, to
			//    do this correctly.
			double racc = 0, tacc = 0;
			int mu = 0;

			for (i=0; i<NT; i++) {
				double dxy[2];
				double rdir[2];
				double R2, ddotr;
				double dr[2];
				double dt[2];
				double fr, ft;

				if (theta[i] == -1)
					continue;
				mu++;

				// jitter vector
				dxy[0] = testxy[2*i+0] - refxy[2*theta[i]+0];
				dxy[1] = testxy[2*i+1] - refxy[2*theta[i]+1];
				// radial vector (this should perhaps be to the ref star, not test)
				rdir[0] = testxy[2*i+0] - qc[0];
				rdir[1] = testxy[2*i+1] - qc[1];
				// 
				R2 = rdir[0]*rdir[0] + rdir[1]*rdir[1];
				ddotr = (dxy[0]*rdir[0] + dxy[1]*rdir[1]);
				// jitter vector projected onto radial vector.
				dr[0] = ddotr * rdir[0] / R2;
				dr[1] = ddotr * rdir[1] / R2;
				// tangential
				dt[0] = dxy[0] - dr[0];
				dt[1] = dxy[1] - dr[1];

				assert(fabs(dr[0] + dt[0] - dxy[0]) < 1e-10);
				assert(fabs(dr[1] + dt[1] - dxy[1]) < 1e-10);

				// fractional change in radial, tangential components.
				fr = SIGN(ddotr) * sqrt((dr[0]*dr[0] + dr[1]*dr[1]) / R2);
				ft = SIGN(rdir[0]*dt[1] - rdir[1]*dt[0]) * sqrt((dt[0]*dt[0] + dt[1]*dt[1]) / R2);

				racc += fr;
				tacc += ft;
			}
			racc /= (double)mu;
			tacc /= (double)mu;

			logmsg("Radial correction: %g\n", racc);
			logmsg("Tangential correction: %g\n", tacc);

			logmsg("Log-odds: %g\n", logodds);

			// Rotate and scale the test stars...
			double* t2xy = malloc(NT * 2 * sizeof(double));
			add_radial_and_tangential_correction(testxy, racc, tacc, qc, t2xy, NT);
			double logodds2 = verify_star_lists(refxy, NR, t2xy, sigma2s, NT,
												effA, distractors, logbail, logaccept,
												NULL, NULL, NULL, NULL);
			logmsg("Log-odds 2: %g\n", logodds2);


			fprintf(f, "t2xy = array([");
			for (i=0; i<NT; i++)
				fprintf(f, "[%g,%g],", t2xy[2*i+0], t2xy[2*i+1]);
			fprintf(f, "])\n");

			free(t2xy);

		}


		if (do_paths) {
			il** reflist;
			dl** problist;

			NT = besti+1;

			logmsg("Finding all matches...\n");

			verify_get_all_matches(refxy, NR, testxy, sigma2s, NT,
								   effA, distractors, 5.0, 0.5,
								   &reflist, &problist);

			/*
			 --reflist contains one list per test star, containing the
			 indices of reference stars within nsigma and within
			 limit of the distractor rate.

			 -a "regular" conflict occurs when one reference star
			 appears in more than one list.

			 -a "ref" conflict occurs when a list has more than one
              element in it.

			 -each star has some "clique" of stars that it can
              interact with (transitive connectivity of the 'nearby'
              graph).  These will usually be small, but might not
              be...  We can compute analytically the sums over small,
              simple groups, but more complex ones will be very hairy.

			 */

			double np;
			np = 1.0;
			for (i=0; i<NT; i++) {
				if (!reflist[i])
					continue;
				np *= (1.0 + il_size(reflist[i]));
			}
			logmsg("Number of paths: about %g\n", np);

			fprintf(f, "allpaths=[]\n");
			fprintf(f, "clf()\n");
			fprintf(f, "alllogprobs = []\n");
			fprintf(f, "pathsx = []\n");
			fprintf(f, "pathsy = []\n");
			fprintf(f, "pathst = []\n");

			int theta[NT];
			double logprobs[NT];
			bool refused[NR];
			for (i=0; i<NR; i++)
				refused[i] = FALSE;

			Npaths = 0;
			logmsg("Finding all paths...\n");
			explore_path(reflist, problist, 0, NT, NR, theta, logprobs, refused, 0, distractors, log(1.0/effA));
			logmsg("Number of paths: %i\n", Npaths);

			fprintf(f, "pathsx = array(pathsx)\npathsy = array(pathsy)\n");

			//fprintf(f, "axis([0, %i, -100, 100])\n", NT);

			for (i=0; i<NT; i++) {
				il_free(reflist[i]);
				dl_free(problist[i]);
			}
			free(reflist);
			free(problist);
		}



		free(theta);
		free(all_logodds);
		free(sigma2s);
		free(testxy);
		free(refxy);

		rd_free(rd);
		rdlist_close(rdls);
	}
	
    logmsg("Logodds: %g\n", logodds);
    logmsg("Odds: %g\n", logodds);

	verify_field_free(vf);
	starxy_free(fieldxy);

	xylist_close(xyls);
	matchfile_close(mf);

	return 0;
}
