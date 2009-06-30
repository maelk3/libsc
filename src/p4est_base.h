/*
  This file is part of p4est.
  p4est is a C library to manage a parallel collection of quadtrees and/or
  octrees.

  Copyright (C) 2007,2008 Carsten Burstedde, Lucas Wilcox.

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

#ifndef P4EST_BASE_H
#define P4EST_BASE_H

/* include p4est config header */

#include <p4est_config.h>

/* indirectly also include sc.h */

#include <sc_containers.h>

#if \
  (defined (P4EST_MPI) && !defined (SC_MPI)) || \
  (!defined (P4EST_MPI) && defined (SC_MPI))
#error "MPI configured differently in p4est and libsc"
#endif
#if \
  (defined (P4EST_MPIIO) && !defined (SC_MPIIO)) || \
  (!defined (P4EST_MPIIO) && defined (SC_MPIIO))
#error "MPI I/O configured differently in p4est and libsc"
#endif

SC_EXTERN_C_BEGIN;

/** Typedef for quadrant coordinates. */
typedef int32_t     p4est_qcoord_t;
#define P4EST_MPI_QCOORD MPI_INT
#define P4EST_VTK_QCOORD "Int32"
#define P4EST_QCOORD_MIN INT32_MIN
#define P4EST_QCOORD_MAX INT32_MAX

/** Typedef for counting topological entities (trees, vertices). */
typedef int32_t     p4est_topidx_t;
#define P4EST_MPI_TOPIDX MPI_INT
#define P4EST_VTK_TOPIDX "Int32"
#define P4EST_TOPIDX_MAX INT32_MAX

/** Typedef for processor-local indexing of quadrants and nodes. */
typedef int32_t     p4est_locidx_t;
#define p4est_locidx_compare sc_int32_compare
#define P4EST_MPI_LOCIDX MPI_INT
#define P4EST_VTK_LOCIDX "Int32"
#define P4EST_LOCIDX_MAX INT32_MAX

/** Typedef for globally unique indexing of quadrants. */
typedef int64_t     p4est_gloidx_t;
#define P4EST_MPI_GLOIDX MPI_LONG_LONG_INT
#define P4EST_VTK_GLOIDX "Int64"
#define P4EST_GLOIDX_MAX INT64_MAX

/* some error checking possibly specific to p4est */
#ifdef P4EST_DEBUG
#define P4EST_ASSERT(c) SC_CHECK_ABORT ((c), "Assertion '" #c "'")
#else
#define P4EST_ASSERT(c) SC_NOOP ()
#endif

/* macros for memory allocation, will abort if out of memory */
#define P4EST_ALLOC(t,n)          (t *) sc_malloc (p4est_package_id,    \
                                                   (n) * sizeof(t))
#define P4EST_ALLOC_ZERO(t,n)     (t *) sc_calloc (p4est_package_id,    \
                                                   (size_t) (n), sizeof(t))
#define P4EST_REALLOC(p,t,n)      (t *) sc_realloc (p4est_package_id,   \
                                                    (p), (n) * sizeof(t))
#define P4EST_STRDUP(s)                 sc_strdup (p4est_package_id, (s))
#define P4EST_FREE(p)                   sc_free (p4est_package_id, (p))

/* Only include the p4est log macros in C, not C++, since C++98
   does not allow variadic macros. */
#ifndef __cplusplus

#define P4EST_LOGF(category,priority,fmt,...)           \
  ((priority) < SC_LP_THRESHOLD ? (void) 0 :            \
   sc_logf (__FILE__, __LINE__,                         \
            p4est_package_id, (category), (priority),   \
            (fmt), __VA_ARGS__))
#define P4EST_LOG(c,p,s) P4EST_LOGF((c), (p), "%s", (s))
#define P4EST_GLOBAL_LOG(p,s) P4EST_LOG (SC_LC_GLOBAL, (p), (s))
#define P4EST_GLOBAL_LOGF(p,f,...) \
  P4EST_LOGF (SC_LC_GLOBAL, (p), (f), __VA_ARGS__)
#define P4EST_NORMAL_LOG(p,s) P4EST_LOG (SC_LC_NORMAL, (p), (s))
#define P4EST_NORMAL_LOGF(p,f,...) \
  P4EST_LOGF (SC_LC_NORMAL, (p), (f), __VA_ARGS__)

/* convenience global log macros will only print if identifier <= 0 */
#define P4EST_GLOBAL_TRACE(s) P4EST_GLOBAL_LOG (SC_LP_TRACE, (s))
#define P4EST_GLOBAL_TRACEF(f,...) \
  P4EST_GLOBAL_LOGF (SC_LP_TRACE, (f), __VA_ARGS__)
#define P4EST_GLOBAL_LDEBUG(s) P4EST_GLOBAL_LOG (SC_LP_DEBUG, (s))
#define P4EST_GLOBAL_LDEBUGF(f,...) \
  P4EST_GLOBAL_LOGF (SC_LP_DEBUG, (f), __VA_ARGS__)
#define P4EST_GLOBAL_VERBOSE(s) P4EST_GLOBAL_LOG (SC_LP_VERBOSE, (s))
#define P4EST_GLOBAL_VERBOSEF(f,...) \
  P4EST_GLOBAL_LOGF (SC_LP_VERBOSE, (f), __VA_ARGS__)
#define P4EST_GLOBAL_INFO(s) P4EST_GLOBAL_LOG (SC_LP_INFO, (s))
#define P4EST_GLOBAL_INFOF(f,...) \
  P4EST_GLOBAL_LOGF (SC_LP_INFO, (f), __VA_ARGS__)
#define P4EST_GLOBAL_STATISTICS(s) P4EST_GLOBAL_LOG (SC_LP_STATISTICS, (s))
#define P4EST_GLOBAL_STATISTICSF(f,...) \
  P4EST_GLOBAL_LOGF (SC_LP_STATISTICS, (f), __VA_ARGS__)
#define P4EST_GLOBAL_PRODUCTION(s) P4EST_GLOBAL_LOG (SC_LP_PRODUCTION, (s))
#define P4EST_GLOBAL_PRODUCTIONF(f,...) \
  P4EST_GLOBAL_LOGF (SC_LP_PRODUCTION, (f), __VA_ARGS__)
#define P4EST_GLOBAL_NOTICE(s)      P4EST_GLOBAL_STATISTICS (s)
#define P4EST_GLOBAL_NOTICEF(f,...) P4EST_GLOBAL_STATISTICSF (f, __VA_ARGS__)
#define P4EST_GLOBAL_LERROR(s)      P4EST_GLOBAL_PRODUCTION (s)
#define P4EST_GLOBAL_LERRORF(f,...) P4EST_GLOBAL_PRODUCTIONF (f, __VA_ARGS__)

/* convenience log macros that are active on every processor */
#define P4EST_TRACE(s) P4EST_NORMAL_LOG (SC_LP_TRACE, (s))
#define P4EST_TRACEF(f,...) \
  P4EST_NORMAL_LOGF (SC_LP_TRACE, (f), __VA_ARGS__)
#define P4EST_LDEBUG(s) P4EST_NORMAL_LOG (SC_LP_DEBUG, (s))
#define P4EST_LDEBUGF(f,...) \
  P4EST_NORMAL_LOGF (SC_LP_DEBUG, (f), __VA_ARGS__)
#define P4EST_VERBOSE(s) P4EST_NORMAL_LOG (SC_LP_VERBOSE, (s))
#define P4EST_VERBOSEF(f,...) \
  P4EST_NORMAL_LOGF (SC_LP_VERBOSE, (f), __VA_ARGS__)
#define P4EST_INFO(s) P4EST_NORMAL_LOG (SC_LP_INFO, (s))
#define P4EST_INFOF(f,...) \
  P4EST_NORMAL_LOGF (SC_LP_INFO, (f), __VA_ARGS__)
#define P4EST_STATISTICS(s) P4EST_NORMAL_LOG (SC_LP_STATISTICS, (s))
#define P4EST_STATISTICSF(f,...) \
  P4EST_NORMAL_LOGF (SC_LP_STATISTICS, (f), __VA_ARGS__)
#define P4EST_PRODUCTION(s) P4EST_NORMAL_LOG (SC_LP_PRODUCTION, (s))
#define P4EST_PRODUCTIONF(f,...) \
  P4EST_NORMAL_LOGF (SC_LP_PRODUCTION, (f), __VA_ARGS__)
#define P4EST_NOTICE(s)      P4EST_STATISTICS (s)
#define P4EST_NOTICEF(f,...) P4EST_STATISTICSF (f, __VA_ARGS__)
#define P4EST_LERROR(s)      P4EST_PRODUCTION (s)
#define P4EST_LERRORF(f,...) P4EST_PRODUCTIONF (f, __VA_ARGS__)

#endif /* !__cplusplus */

/* extern declarations */
extern int          p4est_package_id;

/** Returns a pointer to an array element indexed by a p4est_topidx_t.
 * \param [in] index needs to be in [0]..[elem_count-1].
 */
/*@unused@*/
static inline void *
p4est_array_index_topidx (sc_array_t * array, p4est_topidx_t it)
{
  P4EST_ASSERT (it >= 0 && (size_t) it < array->elem_count);

  return (void *) (array->array + (array->elem_size * (size_t) it));
}

/** Find the lowest position k in a sorted array such that array[k] >= target.
 * \param [in]  target  The target lower bound to binary search for.
 * \param [in]  array   The 64bit integer array to binary search in.
 * \param [in]  size    The number of int64_t's in the array.
 * \param [in]  guess   Initial array position to look at.
 * \return  Returns the matching position
 *          or -1 if array[size-1] < target or if size == 0.
 */
ssize_t             p4est_int64_lower_bound (int64_t target,
                                             const int64_t * array,
                                             size_t size, size_t guess);

/** Registers the p4est library with SC and sets the logging behavior.
 * This function is optional.
 * If this function is not called or called with log_handler == NULL,
 * the default SC log handler will be used.
 * If this function is not called or called with log_threshold == SC_LP_DEFAULT,
 * the default SC log threshold will be used.
 * The default SC log settings can be changed with sc_set_log_defaults ().
 */
void                p4est_init (sc_log_handler_t log_handler,
                                int log_threshold);

SC_EXTERN_C_END;

#endif /* !P4EST_BASE_H */