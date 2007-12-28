/*
  This file is part of p4est.
  p4est is a C library to manage a parallel collection of quadtrees and/or
  octrees.

  Copyright (C) 2007 Carsten Burstedde, Lucas Wilcox.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * Usage: p4est_simple <configuration> <level>
 *        possible configurations:
 *        o unit    Refinement on the unit square.
 *        o three   Refinement on a forest with three trees.
 *        o evil    Check second round of refinement with np=5 level=7
 */

#include <p4est_algorithms.h>
#include <p4est_base.h>
#include <p4est_vtk.h>

enum {
  P4EST_CONFIG_NULL,
  P4EST_CONFIG_UNIT,
  P4EST_CONFIG_THREE,
  P4EST_CONFIG_EVIL,
};

typedef struct
{
  int32_t             a;
}
user_data_t;

typedef struct
{
  MPI_Comm            mpicomm;
  int                 mpirank;
}
mpi_context_t;

static int          refine_level = 0;

static void
init_fn (p4est_t * p4est, int32_t which_tree, p4est_quadrant_t * quadrant)
{
  user_data_t        *data = quadrant->user_data;

  data->a = which_tree;
}

static int
refine_normal_fn (p4est_t * p4est, int32_t which_tree,
                  p4est_quadrant_t * quadrant)
{
  if (quadrant->level >= (refine_level - (which_tree % 3))) {
    return 0;
  }
  if (quadrant->x == (1 << (P4EST_MAXLEVEL)) - (1 << (P4EST_MAXLEVEL - 2)) &&
      quadrant->y == (1 << (P4EST_MAXLEVEL)) - (1 << (P4EST_MAXLEVEL - 2))) {
    return 1;
  }
  if (quadrant->x >= (1 << (P4EST_MAXLEVEL - 2))) {
    return 0;
  }

  return 1;
}

static int
refine_evil_fn (p4est_t * p4est, int32_t which_tree,
                p4est_quadrant_t * quadrant)
{
  if (quadrant->level >= refine_level) {
    return 0;
  }
  if (p4est->mpirank <= 1) {
    return 1;
  }
  
  return 0;
}

static int
coarsen_evil_fn (p4est_t * p4est, int32_t which_tree,
                 p4est_quadrant_t * q0, p4est_quadrant_t * q1,
                 p4est_quadrant_t * q2, p4est_quadrant_t * q3)
{
  if (p4est->mpirank >= 2) {
    return 1;
  }

  return 0;
}

static void
abort_fn (void * data) 
{
#ifdef HAVE_MPI
  int                 mpiret;
#endif
  mpi_context_t      *mpi = data;

  fprintf (stderr, "[%d] p4est_simple abort handler\n", mpi->mpirank);

#ifdef HAVE_MPI
  mpiret = MPI_Abort (mpi->mpicomm, 1);
  P4EST_CHECK_MPI (mpiret);
#endif
}

int
main (int argc, char **argv)
{
#ifdef HAVE_MPI
  int                 use_mpi = 1;
  int                 mpiret;
#endif
  int                 wrongusage, config;
  unsigned            crc;
  char               *usage, *errmsg;
  mpi_context_t       mpi_context, *mpi = &mpi_context;
  p4est_t            *p4est;
  p4est_connectivity_t *connectivity;
  p4est_refine_t      refine_fn;
  p4est_coarsen_t     coarsen_fn;

  /* initialize MPI */
  mpi->mpirank = 0;
  mpi->mpicomm = MPI_COMM_NULL;
#ifdef HAVE_MPI
  if (use_mpi) {
    mpiret = MPI_Init (&argc, &argv);
    P4EST_CHECK_MPI (mpiret);
    mpi->mpicomm = MPI_COMM_WORLD;
    mpiret = MPI_Comm_rank (mpi->mpicomm, &mpi->mpirank);
    P4EST_CHECK_MPI (mpiret);
  }
#endif

  /* register MPI abort handler */
  p4est_set_abort_handler (mpi->mpirank, abort_fn, mpi);

  /* process command line arguments */
  usage =
    "Arguments: <configuration> <level>\n"
    "   Configuration can be any of unit|three|evil\n"
    "   Level controls the maximum depth of refinement\n";
  errmsg = NULL;
  wrongusage = 0;
  config = P4EST_CONFIG_NULL;
  if (!wrongusage && argc != 3) {
    wrongusage = 1;
  }
  if (!wrongusage) {
    if (!strcmp (argv[1], "unit")) {
      config = P4EST_CONFIG_UNIT;
    }
    else if (!strcmp (argv[1], "three")) {
      config = P4EST_CONFIG_THREE;    
    }
    else if (!strcmp (argv[1], "evil")) {
      config = P4EST_CONFIG_EVIL;    
    }
    else {
      wrongusage = 1;
    }
  }
  if (wrongusage) {
    if (mpi->mpirank == 0) {
      fputs ("Usage error\n", stderr);
      fputs (usage, stderr);
      if (errmsg != NULL) {
        fputs (errmsg, stderr);
      }
      p4est_abort ();
    }
#ifdef HAVE_MPI
    MPI_Barrier (mpi->mpicomm);
#endif
  }

  /* assign variables based on configuration */
  refine_level = atoi (argv[2]);
  if (config == P4EST_CONFIG_EVIL) {
    refine_fn = refine_evil_fn;
    coarsen_fn = coarsen_evil_fn;
  }
  else {
    refine_fn = refine_normal_fn;
    coarsen_fn = NULL;
  }

  /* create connectivity and forest structures */
  if (config == P4EST_CONFIG_THREE) {
    connectivity = p4est_connectivity_new_corner ();
  }
  else {
    connectivity = p4est_connectivity_new_unitsquare ();
  }
  p4est = p4est_new (mpi->mpicomm, stdout, connectivity,
                     sizeof (user_data_t), init_fn);
  p4est_tree_print (p4est_array_index (p4est->trees, 0),
                    mpi->mpirank, stdout);
  p4est_vtk_write_file (p4est, "mesh_simple_new");

  /* refinement and coarsening */
  p4est_refine (p4est, refine_fn, init_fn);
  if (coarsen_fn != NULL) {
    p4est_coarsen (p4est, coarsen_fn, init_fn);
  }
  p4est_vtk_write_file (p4est, "mesh_simple_refined");

  /* balance */
  p4est_balance (p4est, init_fn);
  p4est_vtk_write_file (p4est, "mesh_simple_balanced");
  crc = p4est_checksum (p4est);

  /* print forest checksum */
  if (mpi->mpirank == 0) {
    if (p4est->nout != NULL) {
      fprintf (p4est->nout, "Tree checksum 0x%x\n", crc);
    }
  }

  /* destroy the p4est and its connectivity structure */
  p4est_destroy (p4est);
  p4est_connectivity_destroy (connectivity);

  /* clean up and exit */
  p4est_memory_check ();

#ifdef HAVE_MPI
  if (use_mpi) {
    mpiret = MPI_Finalize ();
    P4EST_CHECK_MPI (mpiret);
  }
#endif

  return 0;
}

/* EOF simple.c */
