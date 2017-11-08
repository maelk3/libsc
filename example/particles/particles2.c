/*
  This file is part of p4est.
  p4est is a C library to manage a collection (a forest) of multiple
  connected adaptive quadtrees or octrees in parallel.

  Copyright (C) 2010 The University of Texas System
  Additional copyright (C) 2011 individual authors
  Written by Carsten Burstedde, Lucas C. Wilcox, and Tobin Isaac

  p4est is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  p4est is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with p4est; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#ifndef P4_TO_P8
#include <p4est_bits.h>
#include <p4est_extended.h>
#include <p4est_search.h>
#else
#include <p8est_bits.h>
#include <p8est_extended.h>
#include <p8est_search.h>
#endif /* P4_TO_P8 */
#include <sc_notify.h>
#include <sc_options.h>
#include "global.h"

#define PARTICLES_xstr(s) PARTICLES_str(s)
#define PARTICLES_str(s) #s
#define PARTICLES_48() PARTICLES_xstr(P4EST_CHILDREN)

#define PART_PLANETS (2)

typedef struct pi_data
{
  double              sigma;
  double              invs2;
  double              gnorm;
  double              center[3];
}
pi_data_t;

/** Data type for payload data inside each quadrant */
typedef struct qu_data
{
  union
  {
    /** Offset into local array of all particles after this quadrant */
    long long           lpend;
    double              d;
  } u;

  /** counts of local particles remaining on this quadrant and recieved ones */
  int                 premain, preceive;
}
qu_data_t;

/** Property data stored in a flat array over all particles */
typedef struct pa_data
{
  double              xv[6];
  double              wo[6];
  double              up[6];
}
pa_data_t;

/** Search metadata stored in a flat array over all particles */
typedef struct pa_found
{
  p4est_locidx_t      pori;
}
pa_found_t;

/** Hash table entry for a process that we send messages to */
typedef struct comm_psend
{
  int                 rank;
  sc_array_t          message;     /** Message data to send */
}
comm_psend_t;

/** Array entry for a process that we send messages to */
typedef struct comm_prank
{
  int                 rank;
  comm_psend_t       *psend;        /**< Points to hash table entry */
}
comm_prank_t;

enum comm_tag_e
{
  COMM_TAG_ISEND = P4EST_COMM_TAG_LAST,
  COMM_TAG_LAST
};

static const double simpson[3] = { 1. / 6, 2. / 3., 1. / 6. };

static const double planet_xyz[PART_PLANETS][3] = {
  {.48, .48, .56},
  {.58, .43, .59}
};
static const double planet_mass[PART_PLANETS] = { .1, .2 };

static const double rk1b[0] = { };
static const double rk1g[1] = { 1. };
static const double rk2b[1] = { 1. };
static const double rk2g[2] = { .5, .5 };
static const double rk3b[2] = { 1. / 3., 2. / 3. };
static const double rk3g[3] = { .25, 0., .75 };
static const double rk4b[3] = { .5, .5, 1. };
static const double rk4g[4] = { 1. / 6., 1. / 3., 1. / 3., 1. / 6. };

static const double *prk[4][2] = {
  {rk1b, rk1g},
  {rk2b, rk2g},
  {rk3b, rk3g},
  {rk4b, rk4g}
};

static void
p4est_free_int (int **pptr)
{
  P4EST_ASSERT (pptr != NULL);
  P4EST_FREE (*pptr);
  *pptr = NULL;
}

static void
sc_array_destroy_null (sc_array_t ** parr)
{
  P4EST_ASSERT (parr != NULL);
  P4EST_ASSERT (*parr != NULL);
  sc_array_destroy (*parr);
  *parr = NULL;
}

static int
comm_prank_compare (const void *v1, const void *v2)
{
  return sc_int_compare (&((const comm_prank_t *) v1)->rank,
                         &((const comm_prank_t *) v2)->rank);
}

static double
gaussnorm (double sigma)
{
  return pow (2. * M_PI * sigma * sigma, -.5 * P4EST_DIM);
}

static double
pidense (double x, double y, double z, void *data)
{
  pi_data_t          *piddata = (pi_data_t *) data;

  P4EST_ASSERT (piddata != NULL);
  P4EST_ASSERT (piddata->sigma > 0.);
  P4EST_ASSERT (piddata->invs2 > 0.);
  P4EST_ASSERT (piddata->gnorm > 0.);

  return piddata->gnorm * exp (-.5 * (SC_SQR (x - piddata->center[0]) +
                                      SC_SQR (y - piddata->center[1]) +
                                      SC_SQR (z - piddata->center[2])) *
                               piddata->invs2);
}

static void
loopquad (part_global_t * g, p4est_topidx_t tt, p4est_quadrant_t * quad,
          double lxyz[3], double hxyz[3], double dxyz[3])
{

  int                 i;
  p4est_qcoord_t      qh;

  qh = P4EST_QUADRANT_LEN (quad->level);
  p4est_qcoord_to_vertex (g->conn, tt, quad->x, quad->y,
#ifdef P4_TO_P8
                          quad->z,
#endif
                          lxyz);
  p4est_qcoord_to_vertex (g->conn, tt, quad->x + qh, quad->y + qh,
#ifdef P4_TO_P8
                          quad->z + qh,
#endif
                          hxyz);
  for (i = 0; i < 3; ++i) {
    lxyz[i] /= g->bricklength;
    hxyz[i] /= g->bricklength;
    dxyz[i] = hxyz[i] - lxyz[i];
  }
}

static double
integrate (part_global_t * g, const double lxyz[3], const double dxyz[3])
{
  int                 i, j, k;
  double              wk, wkj, wkji;
  double              d;

  /*** run Simpson's rule ***/
  d = 0.;
#ifdef P4_TO_P8
  for (k = 0; k < 3; ++k) {
    wk = simpson[k] * dxyz[2];
#if 0
  }
#endif
#else
  k = 0;
  wk = 1.;
#endif
  for (j = 0; j < 3; ++j) {
    wkj = wk * simpson[j] * dxyz[1];
    for (i = 0; i < 3; ++i) {
      wkji = wkj * simpson[i] * dxyz[0];
      d += wkji * g->pidense (lxyz[0] + .5 * i * dxyz[0],
                              lxyz[1] + .5 * j * dxyz[1],
                              lxyz[2] + .5 * k * dxyz[2], g->piddata);
    }
  }
#ifdef P4_TO_P8
#if 0
  {
#endif
  }
#endif
  return d;
}

static int
initrp_refine (p4est_t * p4est,
               p4est_topidx_t which_tree, p4est_quadrant_t * quadrant)
{
  qu_data_t          *qud = (qu_data_t *) quadrant->p.user_data;
  part_global_t      *g = (part_global_t *) p4est->user_pointer;
  int                 ilem_particles;

  ilem_particles =
    (int) round (qud->u.d * g->num_particles / g->global_density);

  return (double) ilem_particles > g->elem_particles;
}

static void
initrp (part_global_t * g)
{
  int                 mpiret;
  int                 cycle, max_cycles;
  int                 ilem_particles;
  double              lxyz[3], hxyz[3], dxyz[3];
  double              d, ld;
  double              refine_maxd, refine_maxl;
  double              loclp[2], glolp[2];
  p4est_topidx_t      tt;
  p4est_locidx_t      lq;
  p4est_gloidx_t      old_gnum, new_gnum;
  p4est_tree_t       *tree;
  p4est_quadrant_t   *quad;
  qu_data_t          *qud;

  max_cycles = g->maxlevel - g->minlevel;
  for (cycle = 0;; ++cycle) {
    /*** iterate through local cells to determine local particle density ***/
    ld = 0.;
    refine_maxd = refine_maxl = 0.;
    for (tt = g->p4est->first_local_tree; tt <= g->p4est->last_local_tree;
         ++tt) {
      tree = p4est_tree_array_index (g->p4est->trees, tt);
      for (lq = 0; lq < (p4est_locidx_t) tree->quadrants.elem_count; ++lq) {
        quad = p4est_quadrant_array_index (&tree->quadrants, lq);
        qud = (qu_data_t *) quad->p.user_data;
        loopquad (g, tt, quad, lxyz, hxyz, dxyz);

        /***  integrate density over quadrant ***/
        qud->u.d = d = integrate (g, lxyz, dxyz);
        ld += d;

        /*** maximum particle count and level ***/
        refine_maxd = SC_MAX (refine_maxd, d);
        refine_maxl = SC_MAX (refine_maxl, (double) quad->level);
      }
    }

    /*** get global integral over density ***/
    mpiret = sc_MPI_Allreduce (&ld, &g->global_density, 1, sc_MPI_DOUBLE,
                               sc_MPI_SUM, g->mpicomm);
    SC_CHECK_MPI (mpiret);
    P4EST_GLOBAL_INFOF ("Global integral over density %g\n",
                        g->global_density);

    /*** get global maximum of particle count and level ***/
    loclp[0] = refine_maxd;
    loclp[1] = refine_maxl + g->bricklev;
    mpiret = sc_MPI_Allreduce (loclp, glolp, 2, sc_MPI_DOUBLE,
                               sc_MPI_MAX, g->mpicomm);
    SC_CHECK_MPI (mpiret);
    ilem_particles =
      (int) round (glolp[0] * g->num_particles / g->global_density);
    P4EST_GLOBAL_INFOF ("Maximum particle number per quadrant %d"
                        " and level %g\n", ilem_particles, glolp[1]);

    /*** we have computed the density, this may be enough ***/
    if (cycle >= max_cycles || (double) ilem_particles <= g->elem_particles) {
      break;
    }

    /*** refine and balance ***/
    old_gnum = g->p4est->global_num_quadrants;
    p4est_refine_ext (g->p4est, 0, g->maxlevel - g->bricklev,
                      initrp_refine, NULL, NULL);
    new_gnum = g->p4est->global_num_quadrants;
    if (old_gnum == new_gnum) {
      /* done with refinement if no quadrants were added globally */
      /* cannot happen due to particle count above */
      SC_ABORT_NOT_REACHED ();
      break;
    }
#if 0                           /* we do not need balance for this application */
    if (cycle > 0) {
      p4est_balance (g->p4est, P4EST_CONNECT_FULL, NULL);
    }
#endif

    /*** weighted partition ***/
    p4est_partition (g->p4est, 0, NULL);
  }
}

static void
srandquad (part_global_t * g, const double l[3])
{
  unsigned            u;

  P4EST_ASSERT (0 <= l[0] && l[0] < 1.);
  P4EST_ASSERT (0 <= l[1] && l[1] < 1.);
  P4EST_ASSERT (0 <= l[2] && l[2] < 1.);

  u = ((unsigned int) (l[2] * (1 << 10)) << 20) +
    ((unsigned int) (l[1] * (1 << 10)) << 10) +
    (unsigned int) (l[0] * (1 << 10));
  srand (u);
}

static void
create (part_global_t * g)
{
  int                 mpiret;
  int                 i, j;
  int                 ilem_particles;
  long long           lpnum;
  double              lxyz[3], hxyz[3], dxyz[3];
  double              r;
  p4est_topidx_t      tt;
  p4est_locidx_t      lq;
  p4est_tree_t       *tree;
  p4est_quadrant_t   *quad;
  qu_data_t          *qud;
  pa_data_t          *pad;

  /*** iterate through local cells and populate with particles ***/
  g->padata = sc_array_new (sizeof (pa_data_t));
  lpnum = 0;
  for (tt = g->p4est->first_local_tree; tt <= g->p4est->last_local_tree; ++tt) {
    tree = p4est_tree_array_index (g->p4est->trees, tt);
    for (lq = 0; lq < (p4est_locidx_t) tree->quadrants.elem_count; ++lq) {
      quad = p4est_quadrant_array_index (&tree->quadrants, lq);
      qud = (qu_data_t *) quad->p.user_data;

      /* TODO: maybe move this line elsewhere */
      qud->premain = qud->preceive = 0;

      ilem_particles =
        (int) round (qud->u.d / g->global_density * g->num_particles);
      pad = (pa_data_t *) sc_array_push_count (g->padata,
                                               (size_t) ilem_particles);

      /*** generate required number of particles ***/
      loopquad (g, tt, quad, lxyz, hxyz, dxyz);
      srandquad (g, lxyz);
      for (i = 0; i < ilem_particles; ++i) {
        for (j = 0; j < P4EST_DIM; ++j) {
          r = rand () / (double) RAND_MAX;
          pad->xv[j] = lxyz[j] + r * dxyz[j];
          pad->xv[3 + j] = 0.;
        }
#ifndef P4_TO_P8
        pad->xv[2] = pad->xv[5] = 0.;
#endif
#if 0
        P4EST_LDEBUGF ("Create particle <%g %g %g>\n",
                       pad->xv[0], pad->xv[1], pad->xv[2]);
#endif
        ++pad;
      }
      lpnum += (long long) ilem_particles;
      qud->u.lpend = lpnum;
    }
  }
  g->gplost = 0;
  mpiret = sc_MPI_Allreduce (&lpnum, &g->gpnum, 1, sc_MPI_LONG_LONG_INT,
                             sc_MPI_SUM, g->mpicomm);
  SC_CHECK_MPI (mpiret);
  P4EST_GLOBAL_INFOF ("Created %lld particles for %g\n",
                      g->gpnum, g->num_particles);
}

static void
rkrhs (part_global_t * g, const double xv[6], double rk[6])
{
  int                 i;
  int                 j;
  double              d;
  double              diff[3];

  for (i = 0; i < P4EST_DIM; ++i) {
    rk[i] = xv[3 + i];
    rk[3 + i] = 0.;
  }
#ifndef P4_TO_P8
  rk[2] = rk[5] = 0.;
#endif

  for (j = 0; j < PART_PLANETS; ++j) {
    d = 0.;
    /* distance is always computed in 3D space */
    for (i = 0; i < 3; ++i) {
      diff[i] = planet_xyz[j][i] - xv[i];
      d += SC_SQR (diff[i]);
    }
    d = planet_mass[j] * pow (d, -1.5);
    for (i = 0; i < P4EST_DIM; ++i) {
      rk[3 + i] += d * diff[i];
    }
  }
}

static void
rkstage (part_global_t * g, pa_data_t * pad, double h)
{
  int                 stage = g->stage;
  int                 i;
  double              d;
  double              rk[6];

  /* evaluate right hand side */
  rkrhs (g, stage == 0 ? pad->xv : pad->wo, rk);

  /* compute new evaluation point if necessary */
  if (stage + 1 < g->order) {
    /* stage is not the last */
    d = h * prk[g->order - 1][0][stage];
    for (i = 0; i < 6; ++i) {
      pad->wo[i] = pad->xv[i] + d * rk[i];
    }
  }

  /* compute an update to the state */
  d = prk[g->order - 1][1][stage];
  if (stage == 0) {
    /* first stage */
    if (g->order > 1) {
      /* first stage is not the last */
      P4EST_ASSERT (stage + 1 < g->order);
      for (i = 0; i < 6; ++i) {
        pad->up[i] = d * rk[i];
      }
    }
    else {
      /* first stage is also the last */
      P4EST_ASSERT (stage + 1 == g->order);
      for (i = 0; i < 6; ++i) {
        pad->xv[i] += h * d * rk[i];
      }
    }
  }
  else {
    /* stage is not the first */
    if (stage + 1 < g->order) {
      /* stage is neither first nor last */
      P4EST_ASSERT (0 < stage);
      for (i = 0; i < 6; ++i) {
        pad->up[i] += d * rk[i];
      }
    }
    else {
      /* stage is last of several */
      P4EST_ASSERT (stage + 1 == g->order);
      for (i = 0; i < 6; ++i) {
        pad->xv[i] += h * (pad->up[i] + d * rk[i]);
      }
    }
  }

#if 0
  /* TODO: this is wrong for RK order > 1 */
  P4EST_LDEBUGF ("New location <%g %g %g>\n", pad->xv[0], pad->xv[1],
                 pad->xv[2]);
#endif
}

static int
psearch_quad (p4est_t * p4est, p4est_topidx_t which_tree,
              p4est_quadrant_t * quadrant, int pfirst, int plast,
              p4est_locidx_t local_num, void *point)
{
#ifdef P4EST_ENABLE_DEBUG
  qu_data_t          *qud;

  if (local_num >= 0) {
    qud = (qu_data_t *) quadrant->p.user_data;
    P4EST_ASSERT (qud->premain == 0);
    P4EST_ASSERT (qud->preceive == 0);
  }
#endif
  return 1;
}

static const double *
particle_lookfor (part_global_t * g, const pa_data_t * pad)
{
  P4EST_ASSERT (0 <= g->stage && g->stage < g->order);
  P4EST_ASSERT (pad != NULL);

  return g->stage + 1 < g->order ? pad->wo : pad->xv;
}

static int
psearch_point (p4est_t * p4est, p4est_topidx_t which_tree,
               p4est_quadrant_t * quadrant, int pfirst, int plast,
               p4est_locidx_t local_num, void *point)
{
  int                 i;
  size_t              zp;
  double              lxyz[3], hxyz[3], dxyz[3];
  const double       *x;
  part_global_t      *g = (part_global_t *) p4est->user_pointer;
  qu_data_t          *qud;
  pa_data_t          *pad = (pa_data_t *) point;
  pa_found_t         *pfn;

  /* access location of particle to be searched */
  x = particle_lookfor (g, pad);

  /* due to roundoff we call this even for a local leaf */
  loopquad (g, which_tree, quadrant, lxyz, hxyz, dxyz);
  for (i = 0; i < P4EST_DIM; ++i) {
    if (!(lxyz[i] <= x[i] && x[i] <= hxyz[i])) {
      /* the point is outside the search quadrant */
      return 0;
    }
  }

  /* find process/quadrant for this particle */
  if (local_num >= 0) {
    /* quadrant is a local leaf */
    P4EST_ASSERT (pfirst == g->mpirank && plast == g->mpirank);
    zp = sc_array_position (g->padata, point);
    pfn = (pa_found_t *) sc_array_index (g->pfound, zp);
    /* first local match counts (due to roundoff there may be multiple) */
    if (pfn->pori < g->mpisize) {
      /* bump counter of particles in this local quadrant */
      pfn->pori = (p4est_locidx_t) g->mpisize + local_num;
      *(p4est_locidx_t *) sc_array_push (g->iremain) = (p4est_locidx_t) zp;
      qud = (qu_data_t *) quadrant->p.user_data;
      ++qud->premain;
#if 0
      P4EST_LDEBUGF ("Found leaf particle %d local_num %d becomes %d\n",
                     (int) zp, local_num, pfn->pori);
#endif
    }
    /* return value will have no effect */
    return 0;
  }
  if (pfirst == plast) {
    if (pfirst == g->mpirank) {
      /* continue recursion for local branch quadrant */
      P4EST_ASSERT (plast == g->mpirank);
      return 1;
    }
    /* found particle on a remote process */
    P4EST_ASSERT (plast != g->mpirank);
    zp = sc_array_position (g->padata, point);
    pfn = (pa_found_t *) sc_array_index (g->pfound, zp);
    /* only count match if it has not been found locally or on lower rank */
    if (pfn->pori < 0 || (pfirst < pfn->pori && pfn->pori < g->mpisize)) {
      pfn->pori = (p4est_locidx_t) pfirst;
    }
    /* return value will have no effect */
    return 0;
  }

  /* the process for this particle has not yet been found */
  return 1;
}

static unsigned
psend_hash (const void *v, const void *u)
{
  const comm_psend_t *ps = (const comm_psend_t *) v;

  P4EST_ASSERT (u == NULL);

  return ps->rank;
}

static int
psend_equal (const void *v1, const void *v2, const void *u)
{
  const comm_psend_t *ps1 = (const comm_psend_t *) v1;
  const comm_psend_t *ps2 = (const comm_psend_t *) v2;

  P4EST_ASSERT (u == NULL);

  return ps1->rank == ps2->rank;
}

static void
pack (part_global_t * g)
{
  int                 mpiret;
  int                 retval;
  long long           loclrs[4], glolrs[4];
  size_t              zz, numz;
  size_t              remainz, sendz, lostz;
  double             *msg;
  const double       *x;
  void              **hfound;
#if 0
  pa_data_t          *pad;
#endif
  pa_found_t         *pfn;
  comm_psend_t       *cps, *there;
  comm_prank_t       *trank;

  P4EST_ASSERT (g->pfound != NULL);
  numz = g->pfound->elem_count;

  P4EST_ASSERT (g->padata != NULL);
  P4EST_ASSERT (g->padata->elem_count == numz);

  P4EST_ASSERT (g->psmem != NULL);

  g->psend = sc_hash_new (psend_hash, psend_equal, NULL, NULL);
  g->recevs = sc_array_new (sizeof (comm_prank_t));

  remainz = sendz = lostz = 0;
  cps = (comm_psend_t *) sc_mempool_alloc (g->psmem);
  cps->rank = -1;
  for (zz = 0; zz < numz; ++zz) {
    pfn = (pa_found_t *) sc_array_index (g->pfound, zz);

    /* treat those that leave the domain or stay local */
#if 0
    P4EST_LDEBUGF ("Pack for %d is %d\n", (int) zz, pfn->pori);
#endif
    if (pfn->pori < 0) {
      ++lostz;
      continue;
    }
    if (pfn->pori >= g->mpisize) {
      ++remainz;
      continue;
    }

    /* access message structure */
    P4EST_ASSERT (0 <= pfn->pori && pfn->pori < g->mpisize);
    cps->rank = (int) pfn->pori;
    P4EST_ASSERT (cps->rank != g->mpirank);
    retval = sc_hash_insert_unique (g->psend, cps, &hfound);
    P4EST_ASSERT (hfound != NULL);
    there = *((comm_psend_t **) hfound);
    if (!retval) {
      /* message for this rank exists already */
#if 0
      P4EST_ASSERT (there->message.elem_size == sizeof (pa_data_t));
#endif
      P4EST_ASSERT (there->message.elem_size == 3 * sizeof (double));
      P4EST_ASSERT (there->message.elem_count > 0);
    }
    else {
      /* message is added for this rank */
      P4EST_ASSERT (there == cps);
      trank = (comm_prank_t *) sc_array_push (g->recevs);
      trank->rank = there->rank;
      trank->psend = there;
#if 0
      sc_array_init (&there->message, sizeof (pa_data_t));
#endif
      sc_array_init (&there->message, 3 * sizeof (double));
      cps = (comm_psend_t *) sc_mempool_alloc (g->psmem);
      cps->rank = -1;
    }

    /* add to message buffer */
#if 0
    /* we switched to sending three doubles only */
    pad = (pa_data_t *) sc_array_push (&there->message);
    memcpy (pad, sc_array_index (g->padata, zz), sizeof (pa_data_t));
#endif
    msg = (double *) sc_array_push (&there->message);
    x = particle_lookfor
      (g, (const pa_data_t *) sc_array_index (g->padata, zz));
    memcpy (msg, x, 3 * sizeof (double));

    /* this particle is to be sent to another process */
    ++sendz;
  }
  sc_mempool_free (g->psmem, cps);

  /* TODO: can comm pattern reversal be overlapped with communication? */
  sc_array_sort (g->recevs, comm_prank_compare);

  loclrs[0] = (long long) remainz;
  loclrs[1] = (long long) sendz;
  loclrs[2] = (long long) lostz;
  loclrs[3] = (long long) g->recevs->elem_count;
  mpiret = sc_MPI_Allreduce (loclrs, glolrs, 4, sc_MPI_LONG_LONG_INT,
                             sc_MPI_SUM, g->mpicomm);
  SC_CHECK_MPI (mpiret);
  P4EST_GLOBAL_INFOF ("Particles remain %lld sent %lld lost %lld"
                      " avg peers %.3g\n",
                      glolrs[0], glolrs[1], glolrs[2],
                      glolrs[3] / (double) g->mpisize);
  P4EST_ASSERT (glolrs[0] + glolrs[1] + glolrs[2] == g->gpnum);

  /* TODO: update lost count globally for next stage/step */
}

static void
send (part_global_t * g)
{
  int                 mpiret;
  int                 i;
  int                 num_receivers;
  int                 num_senders;
  int                *irecvs, *isends;
  void              **hfound;
  sc_array_t         *arr;
  comm_psend_t       *cps;
  comm_prank_t       *trank;

  P4EST_ASSERT (g->psmem != NULL);

  /* TODO: move some of this code into pack function? */

  /* post non-blocking send for messages */
  num_receivers = (int) g->recevs->elem_count;
  P4EST_ASSERT (0 <= num_receivers && num_receivers < g->mpisize);
  irecvs = P4EST_ALLOC (int, num_receivers);
  g->send_req = sc_array_new_count (sizeof (sc_MPI_Request), num_receivers);
  for (i = 0; i < num_receivers; ++i) {
    trank = (comm_prank_t *) sc_array_index_int (g->recevs, i);
    irecvs[i] = trank->rank;
    cps = trank->psend;
    P4EST_ASSERT (trank->rank == cps->rank);
    arr = &cps->message;
    P4EST_ASSERT (arr->elem_count > 0);
#if 0
    P4EST_ASSERT (arr->elem_size == sizeof (pa_data_t));
#endif
    P4EST_ASSERT (arr->elem_size == 3 * sizeof (double));
    mpiret = sc_MPI_Isend
      (arr->array, arr->elem_count * arr->elem_size, sc_MPI_BYTE,
       cps->rank, COMM_TAG_ISEND, g->mpicomm,
       (sc_MPI_Request *) sc_array_index_int (g->send_req, i));
    SC_CHECK_MPI (mpiret);
  }

  /* reverse communication pattern */
  isends = P4EST_ALLOC (int, g->mpisize);
  sc_notify (irecvs, num_receivers, isends, &num_senders, g->mpicomm);
  P4EST_ASSERT (0 <= num_senders && num_senders < g->mpisize);
  p4est_free_int (&irecvs);

  /* allocate slots to receive data */
  g->precv = sc_hash_new (psend_hash, psend_equal, NULL, NULL);
  g->sendes = sc_array_new_count (sizeof (comm_prank_t), num_senders);
  for (i = 0; i < num_senders; ++i) {
    cps = (comm_psend_t *) sc_mempool_alloc (g->psmem);
    cps->rank = isends[i];
    cps->message.elem_size = 0;
    P4EST_EXECUTE_ASSERT_TRUE
      (sc_hash_insert_unique (g->precv, cps, &hfound));
    P4EST_ASSERT (hfound != NULL);
    P4EST_ASSERT (*((comm_psend_t **) hfound) == cps);
    trank = (comm_prank_t *) sc_array_index_int (g->sendes, i);
    trank->rank = cps->rank;
    trank->psend = cps;
  }
  p4est_free_int (&isends);
}

static void
recv (part_global_t * g)
{
  int                 mpiret;
  int                 i;
  int                 num_senders;
  int                 source;
  int                 bcount;
  size_t              zcount;
  double             *msg;
  void              **hfound;
  sc_MPI_Status       status;
  comm_psend_t        pcps, *cps;

  /* receive particles into a flat array over all processes */
  g->prebuf = sc_array_new (3 * sizeof (double));

  /* TODO: do not go through precv here if not needed */

  /* loop to receive messages of unknown length */
  P4EST_ASSERT (g->precv != NULL);
  P4EST_ASSERT (g->sendes != NULL);
  num_senders = (int) g->sendes->elem_count;
  for (i = 0; i < num_senders; ++i) {
    mpiret = sc_MPI_Probe (sc_MPI_ANY_SOURCE, COMM_TAG_ISEND,
                           g->mpicomm, &status);
    SC_CHECK_MPI (mpiret);
    P4EST_ASSERT (status.MPI_TAG == COMM_TAG_ISEND);
    mpiret = sc_MPI_Get_count (&status, sc_MPI_BYTE, &bcount);
#if 0
    P4EST_ASSERT (0 < bcount && bcount % sizeof (pa_data_t) == 0);
    zcount = bcount / sizeof (pa_data_t);
#endif
    P4EST_ASSERT (0 < bcount && bcount % (3 * sizeof (double)) == 0);
    zcount = bcount / (3 * sizeof (double));
    source = status.MPI_SOURCE;
    P4EST_ASSERT (0 <= source && source < g->mpisize);
    P4EST_ASSERT (source != g->mpirank);
    cps = &pcps;
    cps->rank = source;
    P4EST_EXECUTE_ASSERT_TRUE (sc_hash_lookup (g->precv, cps, &hfound));
    P4EST_ASSERT (hfound != NULL);
    cps = *((comm_psend_t **) hfound);
    P4EST_ASSERT (cps != NULL && cps->rank == source);
    P4EST_ASSERT (cps->message.elem_size == 0);
#if 0
    sc_array_init_count (&cps->message, sizeof (pa_data_t), zcount);
    mpiret = sc_MPI_Recv (cps->message.array, bcount, sc_MPI_BYTE, source,
                          COMM_TAG_ISEND, g->mpicomm, sc_MPI_STATUS_IGNORE);
#endif
    cps->message.elem_size = 1;
    msg = (double *) sc_array_push_count (g->prebuf, zcount);
    mpiret = sc_MPI_Recv (msg, bcount, sc_MPI_BYTE, source,
                          COMM_TAG_ISEND, g->mpicomm, sc_MPI_STATUS_IGNORE);
    SC_CHECK_MPI (mpiret);

    /* TODO: do something at this point already with received data? */
  }
}

static int
slocal_quad (p4est_t * p4est, p4est_topidx_t which_tree,
             p4est_quadrant_t * quadrant, p4est_locidx_t local_num,
             void *point)
{
  return 1;
}

static int
slocal_point (p4est_t * p4est, p4est_topidx_t which_tree,
              p4est_quadrant_t * quadrant, p4est_locidx_t local_num,
              void *point)
{
  int                 i;
  double              lxyz[3], hxyz[3], dxyz[3];
  double             *x = (double *) point;
  part_global_t      *g = (part_global_t *) p4est->user_pointer;
  qu_data_t          *qud;

  /* due to roundoff we call this even for a local leaf */
  loopquad (g, which_tree, quadrant, lxyz, hxyz, dxyz);
  for (i = 0; i < P4EST_DIM; ++i) {
    if (!(lxyz[i] <= x[i] && x[i] <= hxyz[i])) {
      /* the point is outside the search quadrant */
      return 0;
    }
  }

  if (local_num >= 0) {
    /* quadrant is a local leaf */
    /* first local match counts (due to roundoff there may be multiple) */
    /* make sure this particle is not found again */
    x[0] = -1.;
    ++g->lfound;

    /* count this particle in its target quadrant */
    qud = (qu_data_t *) quadrant->p.user_data;
    ++qud->preceive;

    /* TODO: record which particles are found in this quadrant */

    /* return value will have no effect */
    return 0;
  }

  /* the leaf for this particle has not yet been found */
  return 1;
}

static int
use_coarsen (p4est_t * p4est, p4est_topidx_t which_tree,
             p4est_quadrant_t * quadrants[])
{
  int                 i;
  int                 remain, receive;
  qu_data_t          *qud;
  part_global_t      *g = (part_global_t *) p4est->user_pointer;

  /* TODO: coarsen/refine on sum of still-there and to-be-there particles? */

  /* maybe this quadrant is just called for counting */
  if (quadrants[1] == NULL) {
    qud = (qu_data_t *) quadrants[0]->p.user_data;
    P4EST_ASSERT (g->prevlp <= qud->u.lpend);
    g->prevlp = qud->u.lpend;
    g->irindex += qud->premain;
    return 0;
  }

  /* sum expected particle count over siblings */
  remain = receive = 0;
  for (i = 0; i < P4EST_CHILDREN; ++i) {
    qud = (qu_data_t *) quadrants[i]->p.user_data;
    remain += qud->premain;
    receive += qud->preceive;
  }
  if ((double) (remain + receive) < .5 * g->elem_particles) {
    /* we will coarsen and adjust prevlp and irindex in use_replace */
    g->qremain = remain;
    return 1;
  }
  else {
    /* we will not coarsen and proceed with next quadrant */
    /* TODO: change p4est to not call orphans in a non-coarsened family */
    qud = (qu_data_t *) quadrants[0]->p.user_data;
    g->prevlp = qud->u.lpend;
    g->irindex += qud->premain;
    return 0;
  }
}

static int
use_refine (p4est_t * p4est, p4est_topidx_t which_tree,
            p4est_quadrant_t * quadrant)
{
  qu_data_t          *qud = (qu_data_t *) quadrant->p.user_data;
  part_global_t      *g = (part_global_t *) p4est->user_pointer;

  /* TODO: adjust pori values in pfound for remaining particles */

  if ((double) (qud->premain + qud->preceive) > g->elem_particles) {
    /* we are trying to refine, we will possibly go into the replace function */
    g->prev2 = g->prevlp;
    g->prevlp = qud->u.lpend;
    g->ir2 = g->irindex;
    g->irindex += qud->premain;
    return 1;
  }
  else {
    /* maintain cumulative particle count for next quadrant */
    g->prevlp = qud->u.lpend;
    g->irindex += qud->premain;

    /* TODO: adjust pori values in pfound for remaining particles? */

    return 0;
  }
}

static void
split_by_coord (part_global_t * g, sc_array_t * in,
                sc_array_t * out[2], int component,
                const double lxyz[3], const double dxyz[3])
{
  p4est_locidx_t      ppos;
  const double       *x;
  size_t              zz, znum;
  pa_data_t          *pad;

  P4EST_ASSERT (g->padata != NULL);

  P4EST_ASSERT (in != NULL);
  P4EST_ASSERT (in->elem_size == sizeof (p4est_locidx_t));
  P4EST_ASSERT (out != NULL);
  P4EST_ASSERT (out[0] != NULL);
  P4EST_ASSERT (out[0]->elem_size == sizeof (p4est_locidx_t));
  sc_array_truncate (out[0]);
  P4EST_ASSERT (out[1] != NULL);
  P4EST_ASSERT (out[1]->elem_size == sizeof (p4est_locidx_t));
  sc_array_truncate (out[1]);

  znum = in->elem_count;
  for (zz = 0; zz < znum; ++zz) {
    ppos = *(p4est_locidx_t *) sc_array_index (in, zz);
    pad = (pa_data_t *) sc_array_index (g->padata, ppos);
    x = particle_lookfor (g, pad);
    if (x[component] <= lxyz[component] + .5 * dxyz[component]) {
      *(p4est_locidx_t *) sc_array_push (out[0]) = ppos;
    }
    else {
      *(p4est_locidx_t *) sc_array_push (out[1]) = ppos;
    }
  }
}

static void
use_replace (p4est_t * p4est, p4est_topidx_t which_tree,
             int num_outgoing, p4est_quadrant_t * outgoing[],
             int num_incoming, p4est_quadrant_t * incoming[])
{
#ifdef P4EST_ENABLE_DEBUG
  int                 i;
  int                 remain, receive;
  int                 iloc;
  long long           lpbeg, lpend;
#endif
  int                 wx, wy, wz;
  double              lxyz[3], hxyz[3], dxyz[3];
  sc_array_t          iview, *arr;
  sc_array_t         *ilow, *ihigh, *ilh[2];
  sc_array_t         *jlow, *jhigh, *jlh[2];
  sc_array_t         *klow, *khigh, *klh[2];
  p4est_locidx_t      irem, irbeg;
  p4est_quadrant_t  **pchild;
  qu_data_t          *qud, *qod;
  part_global_t      *g = (part_global_t *) p4est->user_pointer;

  if (num_outgoing == P4EST_CHILDREN) {
    P4EST_ASSERT (num_incoming == 1);
    /* we are coarsening */

#ifdef P4EST_ENABLE_DEBUG
    /* sum counts over siblings */
    remain = receive = 0;
    for (i = 0; i < P4EST_CHILDREN; ++i) {
      qud = (qu_data_t *) outgoing[i]->p.user_data;
      remain += qud->premain;
      receive += qud->preceive;
    }
    P4EST_ASSERT (remain == g->qremain);
#endif
    qod = (qu_data_t *) outgoing[P4EST_CHILDREN - 1]->p.user_data;
    qud = (qu_data_t *) incoming[0]->p.user_data;
    g->prevlp = qud->u.lpend = qod->u.lpend;
    qud->premain = g->qremain;

    /* TODO: fix preceive */
    qud->preceive = -1;
  }
  else {
    P4EST_ASSERT (num_outgoing == 1);
    P4EST_ASSERT (num_incoming == P4EST_CHILDREN);
    P4EST_ASSERT
      (((qu_data_t *) outgoing[0]->p.user_data)->u.lpend == g->prevlp);
    /* we are refining */

    /* recover window onto particles for the new family */
#ifdef P4EST_ENABLE_DEBUG
    lpbeg = g->prev2;
    lpend = g->prevlp;
    iloc = (int) (lpend - lpbeg);
    P4EST_ASSERT (iloc >= 0);
#endif
    irbeg = g->ir2;
    irem = g->irindex - irbeg;
    P4EST_ASSERT (irem >= 0);
    sc_array_init_view (&iview, g->iremain, irbeg, irem);

    /* access parent quadrant */
    loopquad (g, which_tree, outgoing[0], lxyz, hxyz, dxyz);
    qod = (qu_data_t *) outgoing[0]->p.user_data;
    P4EST_ASSERT (qod->premain == irem);

    /* sort remaining particles into the children */
    irbeg = 0;
    pchild = incoming;
    ilow = sc_array_new (sizeof (p4est_locidx_t));
    ihigh = sc_array_new (sizeof (p4est_locidx_t));
    ilh[0] = ilow;
    ilh[1] = ihigh;
    jlow = sc_array_new (sizeof (p4est_locidx_t));
    jhigh = sc_array_new (sizeof (p4est_locidx_t));
    jlh[0] = jlow;
    jlh[1] = jhigh;
#ifdef P4_TO_P8
    klow = sc_array_new (sizeof (p4est_locidx_t));
    khigh = sc_array_new (sizeof (p4est_locidx_t));
    klh[0] = klow;
    klh[1] = khigh;
    split_by_coord (g, &iview, klh, 2, lxyz, dxyz);
    for (wz = 0; wz < 2; ++wz) {
#if 0
    }
#endif
#else
    klh[0] = &iview;
    klh[1] = klow = khigh = NULL;
    wz = 0;
#endif
    split_by_coord (g, klh[wz], jlh, 1, lxyz, dxyz);
    for (wy = 0; wy < 2; ++wy) {
      split_by_coord (g, jlh[wy], ilh, 0, lxyz, dxyz);
      for (wx = 0; wx < 2; ++wx) {
        /* we have a set of particles for child 4 * wz + 2 * wy + wx */
        arr = ilh[wx];
        sc_array_init_view (&iview, g->iremain, irbeg, arr->elem_count);
        sc_array_copy (&iview, arr);
        qud = (qu_data_t *) (*pchild++)->p.user_data;
        qud->u.lpend = qod->u.lpend;
        irbeg += (qud->premain = (int) arr->elem_count);
      }
    }
#ifdef P4_TO_P8
#if 0
    {
#endif
    }
#endif
    P4EST_ASSERT (irbeg == irem);
    sc_array_destroy (ihigh);
    sc_array_destroy (ilow);
    sc_array_destroy (jhigh);
    sc_array_destroy (jlow);
#ifdef P4_TO_P8
    sc_array_destroy (khigh);
    sc_array_destroy (klow);
#endif

    /* TODO: currently the first child is assigned the whole padata of parent */
    /* TODO: sort received particles into the children */

    /* TODO: update pfound for the remaining particles? */
    /* TODO: update pfound for the non-remaining particles? */
  }
}

static void
use (part_global_t * g)
{
#ifdef P4EST_ENABLE_DEBUG
  int                 i;
  int                 num_senders;
  comm_psend_t       *cps;
  comm_prank_t       *trank;
#endif

  P4EST_ASSERT (g->prebuf != NULL);
  P4EST_ASSERT (g->precv != NULL);
  P4EST_ASSERT (g->sendes != NULL);

  /* run local search to find particles sent to us */
  g->lfound = 0;
  p4est_search_local (g->p4est, 0, slocal_quad, slocal_point, g->prebuf);
  P4EST_ASSERT (g->prebuf->elem_count == (size_t) g->lfound);

  /* coarsen the forest according to expected number of particles */
  g->prevlp = 0;
  g->irindex = 0;
  p4est_coarsen_ext (g->p4est, 0, 1, use_coarsen, NULL, use_replace);
  P4EST_ASSERT ((size_t) g->prevlp == g->padata->elem_count);
  P4EST_ASSERT ((size_t) g->irindex == g->iremain->elem_count);

  /* refine the forest according to expected number of particles */
  g->prevlp = g->prev2 = 0;
  g->irindex = g->ir2 = 0;
  p4est_refine_ext (g->p4est, 0, g->maxlevel - g->bricklev,
                    use_refine, NULL, use_replace);
  P4EST_ASSERT ((size_t) g->prevlp == g->padata->elem_count);
  P4EST_ASSERT ((size_t) g->irindex == g->iremain->elem_count);

  /* TODO: if there is no coarsening or refinement we are done with loop */

  /* TODO: partition the forest and send particles along with the partition */

  /* TODO: think about partitioning and sending particles to future owner */

  /* TODO: has this loop become unnecessary? */
  /* go through received particles */
#ifdef P4EST_ENABLE_DEBUG
  num_senders = (int) g->sendes->elem_count;
  for (i = 0; i < num_senders; ++i) {
    trank = (comm_prank_t *) sc_array_index_int (g->sendes, i);
    cps = trank->psend;
    P4EST_ASSERT (cps->rank == trank->rank);
#if 0
    P4EST_ASSERT (cps->message.elem_size == sizeof (pa_data_t));
    P4EST_ASSERT (cps->message.elem_count > 0);
    sc_array_reset (&cps->message);
#endif
    P4EST_ASSERT (cps->message.elem_size == 1);
  }
#endif
  sc_array_destroy_null (&g->sendes);
  sc_hash_destroy (g->precv);
  g->precv = NULL;
}

static void
wait (part_global_t * g)
{
  int                 mpiret;
  int                 i;
  int                 num_receivers;
  comm_psend_t       *cps;
  comm_prank_t       *trank;

  P4EST_ASSERT (g->send_req != NULL);
  P4EST_ASSERT (g->recevs != NULL);
  P4EST_ASSERT (g->psend != NULL);

  /* wait for sent messages to complete */
  if ((num_receivers = (int) g->recevs->elem_count) > 0) {
    mpiret = sc_MPI_Waitall
      (num_receivers, (sc_MPI_Request *) sc_array_index (g->send_req, 0),
       sc_MPI_STATUSES_IGNORE);
    SC_CHECK_MPI (mpiret);
  }
  sc_array_destroy_null (&g->send_req);

  /* free send buffer */
  for (i = 0; i < num_receivers; ++i) {
    trank = (comm_prank_t *) sc_array_index_int (g->recevs, i);
    cps = trank->psend;
    P4EST_ASSERT (cps->rank == trank->rank);
#if 0
    P4EST_ASSERT (cps->message.elem_size == sizeof (pa_data_t));
#endif
    P4EST_ASSERT (cps->message.elem_size == 3 * sizeof (double));
    P4EST_ASSERT (cps->message.elem_count > 0);
    sc_array_reset (&cps->message);
  }
  sc_array_destroy_null (&g->recevs);
  sc_hash_destroy (g->psend);
  g->psend = NULL;
}

static void
sim (part_global_t * g)
{
  int                 i, k;
  int                 ilem_particles;
  long long           lpnum;
  double              t, h, f;
  p4est_topidx_t      tt;
  p4est_locidx_t      lq;
  p4est_tree_t       *tree;
  p4est_quadrant_t   *quad;
  qu_data_t          *qud;
  pa_data_t          *pad;

  P4EST_ASSERT (g->padata != NULL);

  /*** loop over simulation time ***/
  k = 0;
  t = 0.;
  while (t < g->finaltime) {
    h = g->deltat;
    f = t + h;
    if (f > g->finaltime - 1e-3 * g->deltat) {
      f = g->finaltime;
      h = f - t;
    }
    P4EST_GLOBAL_INFOF ("Time %g into step %d with %g\n", t, k, h);

    /*** loop over Runge Kutta stages ***/
    for (g->stage = 0; g->stage < g->order; ++g->stage) {

      /* if a stage is not the last compute new evaluation location */
      /* for the last stage compute the new location of the particle */
      /* do parallel transfer at end of each stage */

      /*** time step for local particles ***/
      lpnum = 0;
      if (g->padata->elem_count > 0) {
        pad = (pa_data_t *) sc_array_index (g->padata, 0);
        for (tt = g->p4est->first_local_tree; tt <= g->p4est->last_local_tree;
             ++tt) {
          tree = p4est_tree_array_index (g->p4est->trees, tt);
          for (lq = 0; lq < (p4est_locidx_t) tree->quadrants.elem_count; ++lq) {
            quad = p4est_quadrant_array_index (&tree->quadrants, lq);
            qud = (qu_data_t *) quad->p.user_data;
            ilem_particles = (int) (qud->u.lpend - lpnum);

            /*** loop through particles in this element */
            for (i = 0; i < ilem_particles; ++i) {
              /* one Runge Kutta stage for this particle */
              rkstage (g, pad++, h);
            }

            /* move to next quadrant */
            lpnum = qud->u.lpend;
          }
        }
      }

      /* TODO:
         reassign particle to other quadrant
         according to position in either wo (not last stage) or xv;
         notify quadrants of assignment to refine/coarsen
         parallel transfer and partition */

      /* begin loop */

      /* p4est_search_all to find new local element or process for each particle */
      g->pfound =
        sc_array_new_count (sizeof (pa_found_t), g->padata->elem_count);
      sc_array_memset (g->pfound, -1);
      g->iremain = sc_array_new (sizeof (p4est_locidx_t));
      p4est_search_all (g->p4est, 0, psearch_quad, psearch_point, g->padata);

      /* send to-be-received particles to receiver processes */
      g->psmem = sc_mempool_new (sizeof (comm_psend_t));
      pack (g);
      send (g);
      recv (g);
      use (g);
      wait (g);

      /* TODO: move deallocation of these upward */
      sc_mempool_destroy (g->psmem);
      g->psmem = NULL;
      sc_array_destroy_null (&g->iremain);
      sc_array_destroy_null (&g->prebuf);
      sc_array_destroy_null (&g->pfound);

      /* receive particles and run local search to count them per-quadrant */

      /* refine the mesh based on current + received count */

      /* if no refinement occurred, store received particles and break loop */

      /* partition weighted by current + received count (?) */

      /* transfer particles accordingly */

      /* end loop */
    }

    /*** finish up time step ***/
    ++k;
    t = f;
  }

  P4EST_GLOBAL_PRODUCTIONF ("Time %g is final after %d steps\n", t, k);
}

static void
run (part_global_t * g)
{
  int                 b;
  pi_data_t           spiddata, *piddata = &spiddata;

  /*** initial particle density ***/
  piddata->sigma = .1;
  piddata->invs2 = 1. / SC_SQR (piddata->sigma);
  piddata->gnorm = gaussnorm (piddata->sigma);
  piddata->center[0] = .3;
  piddata->center[1] = .4;
#ifndef P4_TO_P8
  piddata->center[2] = 0.;
#else
  piddata->center[2] = .5;
#endif
  g->pidense = pidense;
  g->piddata = piddata;

  /*** initial mesh for domain ***/
  b = g->bricklength = (1 << g->bricklev);
  if (g->bricklev > 0) {
    g->conn = p4est_connectivity_new_brick (b, b
#ifdef P4_TO_P8
                                            , b
#endif
                                            , 1, 1
#ifdef P4_TO_P8
                                            , 1
#endif
      );
  }
  else {
#ifndef P4_TO_P8
    g->conn = p4est_connectivity_new_unitsquare ();
#else
    g->conn = p8est_connectivity_new_unitcube ();
#endif
  }
  g->p4est = p4est_new_ext (g->mpicomm, g->conn, 0,
                            g->minlevel - g->bricklev, 1,
                            sizeof (qu_data_t), NULL, g);

  /*** initial refinement and partition ***/
  initrp (g);

  /*** create particles ***/
  create (g);

  /*** run simulation ***/
  sim (g);

  /*** destroy data ***/
  sc_array_destroy_null (&g->padata);

  /*** destroy mesh ***/
  p4est_destroy (g->p4est);
  g->p4est = NULL;
  p4est_connectivity_destroy (g->conn);
  g->conn = NULL;
}

static int
usagerr (sc_options_t * opt, const char *msg)
{
  int                 mpiret;

  SC_GLOBAL_LERRORF ("Usage required: %s\n", msg);
  sc_options_print_usage (p4est_package_id, SC_LP_ERROR, opt, NULL);

  mpiret = sc_MPI_Finalize ();
  SC_CHECK_MPI (mpiret);

  return 1;
}

int
main (int argc, char **argv)
{
  int                 mpiret;
  int                 first_argc;
  sc_options_t       *opt;
  part_global_t global, *g = &global;

  /*** setup mpi environment ***/

  mpiret = sc_MPI_Init (&argc, &argv);
  SC_CHECK_MPI (mpiret);

  g->mpicomm = sc_MPI_COMM_WORLD;
  mpiret = sc_MPI_Comm_size (g->mpicomm, &g->mpisize);
  SC_CHECK_MPI (mpiret);
  mpiret = sc_MPI_Comm_rank (g->mpicomm, &g->mpirank);
  SC_CHECK_MPI (mpiret);
  sc_init (g->mpicomm, 1, 1, NULL, SC_LP_DEFAULT);
  p4est_init (NULL, SC_LP_DEFAULT);

  /*** read command line parameters ***/

  opt = sc_options_new (argv[0]);
  sc_options_add_int (opt, 'l', "minlevel", &g->minlevel, 0, "Lowest level");
  sc_options_add_int (opt, 'L', "maxlevel", &g->maxlevel,
                      P4EST_QMAXLEVEL, "Highest level");
  sc_options_add_int (opt, 'b', "bricklev", &g->bricklev,
                      0, "Brick refinement level");
  sc_options_add_int (opt, 'r', "rkorder", &g->order,
                      1, "Order of Runge Kutta method");
  sc_options_add_double (opt, 'n', "particles", &g->num_particles,
                         1e3, "Global number of particles");
  sc_options_add_double (opt, 'e', "pperelem", &g->elem_particles,
                         3., "Number of particles per element");
  sc_options_add_double (opt, 'h', "deltat", &g->deltat,
                         1e-1, "Time step size");
  sc_options_add_double (opt, 'T', "finaltime", &g->finaltime,
                         1., "Final time of simulation");
  sc_options_add_switch (opt, 'V', "vtk", &g->vtk, "write VTK output");
  sc_options_add_switch (opt, 'C', "check", &g->check,
                         "write checkpoint output");
  sc_options_add_string (opt, 'P', "prefix", &g->prefix,
                         "p" PARTICLES_48 ()"rticles",
                         "prefix for file output");

  first_argc = sc_options_parse (p4est_package_id, SC_LP_DEFAULT,
                                 opt, argc, argv);
  if (first_argc < 0 || first_argc != argc) {
    return usagerr (opt, "No non-option arguments permitted");
  }
  if (g->minlevel < 0 || g->minlevel > P4EST_QMAXLEVEL) {
    return usagerr (opt, "Minlevel between 0 and P4EST_QMAXLEVEL");
  }
  if (g->maxlevel < g->minlevel || g->maxlevel > P4EST_QMAXLEVEL) {
    return usagerr (opt, "Maxlevel between minlevel and P4EST_QMAXLEVEL");
  }
  if (g->bricklev < 0 || g->bricklev > g->minlevel) {
    return usagerr (opt, "Brick level between 0 and minlevel");
  }
  if (g->order < 1 || g->order > 4) {
    return usagerr (opt, "Runge Kutta order between 1 and 4");
  }
  if (g->num_particles <= 0.) {
    return usagerr (opt, "Global number of particles positive");
  }
  if (g->elem_particles <= 0.) {
    return usagerr (opt, "Number of particles per element positive");
  }
  sc_options_print_summary (p4est_package_id, SC_LP_PRODUCTION, opt);
  sc_options_destroy (opt);

  /*** run program ***/

  run (g);

  /*** clean up and exit ***/

  sc_finalize ();

  mpiret = sc_MPI_Finalize ();
  SC_CHECK_MPI (mpiret);

  return 0;
}