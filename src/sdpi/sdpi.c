/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/* This file is part of SCIPSDP - a solving framework for mixed-integer      */
/* semidefinite programs based on SCIP.                                      */
/*                                                                           */
/* Copyright (C) 2011-2013 Discrete Optimization, TU Darmstadt,              */
/*                         EDOM, FAU Erlangen-Nürnberg                       */
/*               2014-2023 Discrete Optimization, TU Darmstadt               */
/*                                                                           */
/*                                                                           */
/* Licensed under the Apache License, Version 2.0 (the "License");           */
/* you may not use this file except in compliance with the License.          */
/* You may obtain a copy of the License at                                   */
/*                                                                           */
/*     http://www.apache.org/licenses/LICENSE-2.0                            */
/*                                                                           */
/* Unless required by applicable law or agreed to in writing, software       */
/* distributed under the License is distributed on an "AS IS" BASIS,         */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  */
/* See the License for the specific language governing permissions and       */
/* limitations under the License.                                            */
/*                                                                           */
/*                                                                           */
/* Based on SCIP - Solving Constraint Integer Programs                       */
/* Copyright (C) 2002-2023 Zuse Institute Berlin                             */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* #define SCIP_DEBUG*/
/* #define SCIP_MORE_DEBUG*/

/**@file   sdpi.c
 * @brief  General interface methods for SDP-preprocessing (mainly fixing variables and removing empty rows/cols)
 * @author Tristan Gally
 * @author Marc Pfetsch
 *
 * This file specifies a generic SDP-solver interface used by SCIP to create, modify, and solve semidefinite programs of
 * the (dual) form
 * \f{align*}{
 *    \min\quad & b^T y \\
 *    \mbox{s.t.}\quad & \sum_{j \in J} A_j^{(k)} y_j - A_0^{(k)} \succeq 0 && \forall \ k \in K, \\
 *     & \sum_{j \in J} d_{ij}\, y_j \geq c_i && \forall \ i \in I, \\
 *     & \ell_j \leq y_j \leq u_j && \forall \ j \in J,
 * \f}
 * for symmetric matrices \f$ A_i^{(k)} \in S_{n_k} \f$ and a matrix \f$ D \in \mathbb{R}^{I \times J} \f$ and query
 * information about the solution.
 * The code refers to this problem as the @em dual.
 *
 * In comparison the @em primal is
 * \f{eqnarray*}{
 *    \max & & \sum_{k \in K} A_0^{(k)} \bullet X^{(k)} + \sum_{i \in I} c_i\, x_i - \sum_{j \in J_u} u_j\, v_j + \sum_{j \in J_\ell} \ell_j\, w_j \\
 *    \mbox{s.t.} & & \sum_{k \in K} A_j^{(k)} \bullet X^{(k)} + \sum_{i \in I} d_{ij}\, x_i - 1_{\{u_j < \infty\}}\, v_j + 1_{\{\ell_j > -\infty\}}\, w_j = b_j \quad \forall \ j \in J,\\
 *      & & X^{(k)} \succeq 0 \quad \forall \ k \in K, \\
 *      & & x_i \geq 0 \qquad \forall \ i \in I,\\
 *      & & v_j \geq 0 \qquad \forall \ j \in J_u,\\
 *      & & w_j \geq 0 \qquad \forall \ j \in J_\ell,
 * \f}
 * where \f$J_\ell := \{j \in J: \ell_j > -\infty\}\f$ and \f$J_u := \{j \in J: u_j < \infty\}\f$.
 *
 * @section prep Preparation
 *
 * The interface performs some preprocessing on the current problem and can sometimes determine whether the dual and/or
 * primal is feasible or infeasible. The primal or dual is considered to be unbounded if there exists a ray and it is
 * feasible.
 *
 * @subsection fixed All variables are fixed
 *
 * This interface prepares the problem and checks whether all variables are fixed.
 * In this case, the influence of the fixed variables is transformed to the constant parts and the dual looks as follows:
 * \f{align*}{
 *    \min\quad & 0 \\
 *    \mbox{s.t.}\quad & - A_0^{(k)} \succeq 0 && \forall \ k \in K, \\
 *     & 0 \geq c_i && \forall \ i \in I,
 * \f}
 * which is feasible if \f$c \leq 0\f$ and \f$A_0^{(k)} \preceq 0\f$ for all \f$k\f$. If this is the case, the primal is feasible and bounded:
 * \f{eqnarray*}{
 *    \max & & \sum_{k \in K} A_0^{(k)} \bullet X^{(k)} + \sum_{i \in I} c_i\, x_i \\
 *    \mbox{s.t.} & & X^{(k)} \succeq 0 \quad \forall \ k \in K, \\
 *      & & x_i \geq 0 \qquad \forall \ i \in I.\\
 * \f}
 * Otherwise the dual is infeasible and the primal is unbounded (there exists a ray and it is feasible).
 *
 * @subsection infeas Infeasibility
 *
 * The interface can determine infeasibility in the case in which all variables are fixed or if variable bounds are
 * conflicting; in either case, @p infeasible is true. In the latter case, assume that \f$\ell_j > u_j\f$. Then one can
 * produce a ray for the primal as follows: Set all \f$X^{(k)} = 0\f$, \f$x = 0\f$, \f$v_r = w_r = 0\f$ for all \f$r \in
 * J\setminus \{j\}\f$. Furthermore, let \f$\gamma = v_j = w_j\f$ tend to infinity, then the objective is \f$(\ell_j -
 * u_j) \gamma \to \infty\f$.
 *
 * Note that @p infeasible is also true if the (dual) penalty formulation without the objective function has a strictly
 * positive optimal objective value. Since we solved the penalty formulation, @p allfixed is false. Thus, the dual
 * problem is infeasible and the ray defined above is valid for the primal problem.
 *
 * Feasibility of the primal depends on the problem.
 *
 * @subsection sdp1d SDP with only one variable
 *
 * The interface detects whether the SDP to be solved only has a single variable. In this case it applies a semi-smooth
 * Newton method to solve it. All solution information is updated accordingly.
 */

#include <assert.h>
#include <math.h>

#include "sdpi/sdpisolver.h"
#include "sdpi/sdpi.h"
#include "scipsdp/SdpVarfixer.h"
#include "sdpi/lapack_interface.h"           /* to check feasibility if all variables are fixed during preprocessing */
#include "sdpi/sdpiclock.h"
#include "sdpi/solveonevarsdp.h"

#include "blockmemshell/memory.h"            /* for memory allocation */
#include "scip/def.h"                        /* for SCIP_Real, _Bool, ... */
#include "scip/pub_misc.h"                   /* for sorting */
#include "scip/pub_message.h"                /* for debug and error message */
#include "scip/dbldblarith.h"                /* for row tightening */

/* turn off some lint warnings for whole file: */
/*lint --e{788,818}*/


/** Checks if a BMSallocMemory-call was successfull, otherwise returns SCIP_NOMEMORY */
#define BMS_CALL(x)   do                                                                                      \
                      {                                                                                       \
                          if( NULL == (x) )                                                                   \
                          {                                                                                   \
                             SCIPerrorMessage("No memory in function call\n");                                \
                             return SCIP_NOMEMORY;                                                            \
                          }                                                                                   \
                      }                                                                                       \
                      while( FALSE )

/** this will be called in all functions that want to access solution information to check if the problem was solved since the last change of the problem */
#define CHECK_IF_SOLVED(sdpi)  do                                                                             \
                      {                                                                                       \
                         if ( ! (sdpi->solved) )                                                              \
                         {                                                                                    \
                            SCIPerrorMessage("Tried to access solution information ahead of solving! \n");    \
                            return SCIP_LPERROR;                                                              \
                         }                                                                                    \
                      }                                                                                       \
                      while( FALSE )

/** same as CHECK_IF_SOLVED, but this will be used in functions returning a boolean value */
#define CHECK_IF_SOLVED_BOOL(sdpi)  do                                                                        \
                      {                                                                                       \
                         if ( ! (sdpi->solved) )                                                              \
                         {                                                                                    \
                            SCIPerrorMessage("Tried to access solution information ahead of solving! \n");    \
                            return FALSE;                                                                     \
                         }                                                                                    \
                      }                                                                                       \
                      while( FALSE )

/** duplicate an array that might be NULL (in that case NULL is returned, otherwise BMSduplicateMemory is called) */
#define DUPLICATE_ARRAY_NULL(blkmem, target, source, size) do                                                 \
                      {                                                                                       \
                         if (size > 0)                                                                        \
                            BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, target, source, size) );           \
                         else                                                                                 \
                            *target = NULL;                                                                   \
                      }                                                                                       \
                      while( FALSE )

/** same as SCIP_CALL, but gives a SCIP_PARAMETERUNKNOWN error if it fails */
#define SCIP_CALL_PARAM(x)   do                                                                               \
                      {                                                                                       \
                         SCIP_RETCODE _restat_;                                                               \
                         if ( (_restat_ = (x)) != SCIP_OKAY )                                                 \
                         {                                                                                    \
                            if ( _restat_ != SCIP_PARAMETERUNKNOWN )                                          \
                            {                                                                                 \
                               SCIPerrorMessage("Error <%d> in function call\n", _restat_);                   \
                               SCIPABORT();                                                                   \
                            }                                                                                 \
                            return _restat_;                                                                  \
                         }                                                                                    \
                      }                                                                                       \
                      while( FALSE )

/** same as SCIP_CALL_PARAM, but ignores SCIP_PARAMETERUNKNOWN */
#define SCIP_CALL_PARAM_IGNORE_UNKNOWN(x)   do                                                                \
                      {                                                                                       \
                         SCIP_RETCODE _restat_;                                                               \
                         if ( (_restat_ = (x)) != SCIP_OKAY )                                                 \
                         {                                                                                    \
                            if ( _restat_ != SCIP_PARAMETERUNKNOWN )                                          \
                            {                                                                                 \
                               SCIPerrorMessage("Error <%d> in function call\n", _restat_);                   \
                               SCIPABORT();                                                                   \
                            }                                                                                 \
                         }                                                                                    \
                      }                                                                                       \
                      while( FALSE )

#define MIN_GAPTOL                  1e-10    /**< minimum gaptolerance for SDP-solver if decreasing it for a penalty formulation */
#define DEFAULT_SDPSOLVERGAPTOL     1e-4     /**< the stopping criterion for the duality gap the SDP solver should use */
#define DEFAULT_FEASTOL             1e-6     /**< used to test for feasibility */
#define DEFAULT_EPSILON             1e-9     /**< used to test whether given values are equal */
#define DEFAULT_PENALTYPARAM        1e+5     /**< the starting penalty parameter Gamma used for the penalty formulation if the SDP-solver didn't converge */
#define DEFAULT_MAXPENALTYPARAM     1e+10    /**< the maximal penalty parameter Gamma used for the penalty formulation if the SDP-solver didn't converge */
#define DEFAULT_NPENALTYINCR        8        /**< maximal number of times the penalty parameter will be increased if penalty formulation failed */

/** one variable SDP status */
enum SCIP_Onevar_Status
{
   SCIP_ONEVAR_UNSOLVED       = 0,      /**< one variable SDP has not been solved */
   SCIP_ONEVAR_OPTIMAL        = 1,      /**< one variable SDP has been solved to optimality */
   SCIP_ONEVAR_INFEASIBLE     = 2       /**< one variable SDP has been detected to be infeasible */
};
typedef enum SCIP_Onevar_Status SCIP_ONEVAR_STATUS;


/** data for SDPI */
struct SCIP_SDPi
{
   SCIP_SDPISOLVER*      sdpisolver;         /**< pointer to the interface for the SDP-solver */
   SCIP_MESSAGEHDLR*     messagehdlr;        /**< messagehandler to printing messages, or NULL */
   BMS_BLKMEM*           blkmem;             /**< block memory */
   BMS_BUFMEM*           bufmem;             /**< buffer memory */
   int                   nvars;              /**< number of variables */
   int                   maxnvars;           /**< maximal number of variables */
   int                   nsdpblocks;         /**< number of SDP-blocks */
   int                   maxnsdpblocks;      /**< maximal number of required SDP blocks */
   int*                  sdpblocksizes;      /**< sizes of the SDP-blocks */
   int*                  sdpnblockvars;      /**< number of variables in each SDP-block */
   int*                  maxsdpnblockvars;   /**< maximal number of block variables */
   int*                  maxsdpblocksizes;   /**< maximal blocksizes */

   /* variable data */
   SCIP_Real*            obj;                /**< objective function values of variables */
   SCIP_Real*            lb;                 /**< lower bounds of variables */
   SCIP_Real*            ub;                 /**< upper bounds of variables */
   SCIP_Bool*            isintegral;         /**< whether the variables are integral */

   /* constant SDP data: */
   int                   sdpconstnnonz;      /**< number of nonzero elements in the constant matrices of the SDP blocks */
   int*                  sdpconstnblocknonz; /**< number of nonzeros for each variable in the constant matrix (size of sdpconst[row/col/val] */
   int*                  maxsdpconstnblocknonz; /**< maximal number of nonzeros in constant matrix */
   int**                 sdpconstrow;        /**< array for row-indices for each block */
   int**                 sdpconstcol;        /**< array for column-indices for each block */
   SCIP_Real**           sdpconstval;        /**< array for nonzero values for each block */

   /* non-constant SDP data: */
   int                   sdpnnonz;           /**< number of nonzero elements in the SDP-constraint matrices */
   int**                 sdpnblockvarnonz;   /**< sdpnblockvarnonz[i][j] = nonzeros of j-th variable in i-th block (length of row/col/val[i][j]) */
   int**                 sdpvar;             /**< sdpvar[b][j] = index of j-th variable in block b */
   int***                sdprow;             /**< sdprow[b][v][j] = row of j-th nonzero of variable v in block b */
   int***                sdpcol;             /**< sdprow[b][v][j] = column of j-th nonzero of variable v in block b */
   SCIP_Real***          sdpval;             /**< sdpval[i][j][k] = value of j-th nonzero of variable v in block b */

   int                   maxsdpstore;        /**< size of the storage arrays */
   int*                  sdprowstore;        /**< array to store all rows */
   int*                  sdpcolstore;        /**< array to store all columns */
   SCIP_Real*            sdpvalstore;        /**< array to store all nonzeros */

   /* lp data: */
   int                   nlpcons;            /**< number of LP-constraints */
   int                   maxnlpcons;         /**< maximal number of LP-constraints */
   SCIP_Real*            lplhs;              /**< left hand sides of LP rows */
   SCIP_Real*            lprhs;              /**< right hand sides of LP rows */
   int                   nactivelpcons;      /**< number of active LP-constraints */
   int                   lpnnonz;            /**< number of nonzero elements in the LP-constraint matrix */
   int                   maxlpnnonz;         /**< maximal number of nonzero elements in the LP-constraint matrix */
   int*                  lpbeg;              /**< start index of each row in ind- and val-array */
   int*                  lpind;              /**< column-index for each entry in lpval-array */
   SCIP_Real*            lpval;              /**< values of LP-constraint matrix entries */

   /* preprocessing data: */
   int**                 indchanges;         /**< number of indices removed before current row/col index or -1 if removed */
   int*                  nremovedinds;       /**< array for the number of rows/cols to be fixed for each block */
   int*                  blockindchanges;    /**< array for the index changes for each block, system is the same as for indchanges */
   int                   nremovedblocks;     /**< array for the number of blocks to be removed from the SDP */
   int*                  sdpilpindchanges;   /**< array for the number of LP-constraints removed before the current one (-1 if removed itself) */
   SCIP_Real*            sdpilplhs;          /**< working space for left hand sides of LP rows */
   SCIP_Real*            sdpilprhs;          /**< working space for right hand sides of LP rows */
   SCIP_Real*            sdpilb;             /**< copy for lower bounds of variables */
   SCIP_Real*            sdpiub;             /**< copy for upper bounds of variables */
   int*                  sdpilbrowidx;       /**< index of row that provided the bound in sdpilb (or -1) */
   int*                  sdpiubrowidx;       /**< index of row that provided the bound in sdpiub (or -1) */
   int*                  sdpilpbeg;          /**< working space for start index of each row in ind- and val-array */
   int*                  sdpilpind;          /**< working space for column-index for each entry in lpval-array */
   SCIP_Real*            sdpilpval;          /**< working space for values of LP-constraint matrix entries */

   /* statistics */
   int                   ninfeasible;        /**< total number of times infeasibility was detected in presolving */
   int                   nallfixed;          /**< total number of times all variables were fixed */
   int                   nonevarsdp;         /**< total number of times a one variable SDP was solved */

   /* other data */
   int                   slatercheck;        /**< should the Slater condition for the dual problem be checked ahead of each solving process */
   int                   sdpid;              /**< counter for the number of SDPs solved */
   int                   niterations;        /**< number of iterations since the last solve call */
   SCIP_Real             opttime;            /**< time for the last SDP optimization call of solver */
   int                   nsdpcalls;          /**< number of calls to the SDP-Solver since the last solve call */
   SCIP_Bool             solved;             /**< was the problem solved since the last change */
   SCIP_Bool             penalty;            /**< was the last solved problem a penalty formulation */
   SCIP_Bool             infeasible;         /**< was infeasibility detected in presolving? */
   SCIP_Bool             allfixed;           /**< could all variables be fixed during presolving? */
   SCIP_Real             epsilon;            /**< tolerance for absolute checks */
   SCIP_Real             gaptol;             /**< (previous: sdpsolverepsilon) this is used for checking if primal and dual objective are equal */
   SCIP_Real             feastol;            /**< this is used to check if the SDP-Constraint is feasible */
   SCIP_Real             penaltyparam;       /**< the starting penalty parameter Gamma used for the penalty formulation if the SDP-solver didn't converge */
   SCIP_Real             maxpenaltyparam;    /**< the maximal penalty parameter Gamma used for the penalty formulation if the SDP-solver didn't converge */
   int                   npenaltyincr;       /**< maximal number of times the penalty parameter will be increased if penalty formulation failed */
   SCIP_Real             peninfeasadjust;    /**< gap- or feastol will be multiplied by this before checking for infeasibility using the penalty formulation */
   SCIP_Real             bestbound;          /**< best bound computed with a penalty formulation */
   SCIP_SDPSLATER        primalslater;       /**< did the primal slater condition hold for the last problem */
   SCIP_SDPSLATER        dualslater;         /**< did the dual slater condition hold for the last problem */
   SDPI_CLOCK*           usedsdpitime;       /**< time needed for processing in SDPI */
   SCIP_ONEVAR_STATUS    solvedonevarsdp;    /**< whether we solve a one variable SDP (= 0 no, = 1 optimally solved, = 2 infeasibility detected */
   SCIP_Real             onevarsdpobjval;    /**< objective value of one variable SDP */
   SCIP_Real             onevarsdpoptval;    /**< optimal value of one variable SDP */
   int                   onevarsdpidx;       /**< index of variable for one variable SDP */
   SCIP_Real*            onevarsdpcertvec;   /**< one variable SDP certificate vector (eigenvector) */
   int                   onevarsdpcertsize;  /**< block size for one variable SDP certificate vector */
   SCIP_Real             onevarsdpcertval;   /**< one variable SDP certificate value (supergradient) */
   SCIP_Real**           allfixedeigenvecs;  /**< eigenvectors for instances if all variables are fixed */
};


/*
 * Local Functions
 */

#ifndef NDEBUG
/** tests whether the lower bound is an epsilon away from the upper bound */
static
SCIP_Bool isFixed(
   const SCIP_SDPI*      sdpi,               /**< pointer to an SDP-interface structure */
   int                   v                   /**< variable index */
   )
{
   SCIP_Real lb;
   SCIP_Real ub;

   assert( sdpi != NULL );
   assert( sdpi->sdpilb != NULL );
   assert( sdpi->sdpiub != NULL );
   assert( 0 <= v && v < sdpi->nvars );

   lb = sdpi->sdpilb[v];
   ub = sdpi->sdpiub[v];
   assert( lb <= ub + sdpi->epsilon );

   return ( ub - lb <= sdpi->epsilon );
}
#else
#define isFixed(sdpi, v) (sdpi->sdpiub[v] - sdpi->sdpilb[v] <= sdpi->epsilon)
#endif

/** calculate memory size for dynamically allocated arrays */
static
int calcGrowSize(
   int                   initsize,           /**< initial size of array */
   int                   num                 /**< minimum number of entries to store */
   )
{
   int oldsize;
   int size;

   assert( initsize >= 0 );
   assert( num >= 0 );

   /* calculate the size with loop, such that the resulting numbers are always the same (-> block memory) */
   initsize = MAX(initsize, SCIP_DEFAULT_MEM_ARRAYGROWINIT);
   size = initsize;
   oldsize = size - 1;

   /* second condition checks against overflow */
   while ( size < num && size > oldsize )
   {
      oldsize = size;
      size = (int)(SCIP_DEFAULT_MEM_ARRAYGROWFAC * size + initsize);
   }

   /* if an overflow happened, set the correct value */
   if ( size <= oldsize )
      size = num;

   assert( size >= initsize );
   assert( size >= num );

   return size;
}

/** ensure size of bound data */
static
SCIP_RETCODE ensureBoundDataMemory(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   int                   nvars               /**< number of variables */
   )
{
   int newsize;

   assert( sdpi != NULL );

   if ( nvars > sdpi->maxnvars )
   {
      newsize = calcGrowSize(sdpi->maxnvars, nvars);

      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->obj), sdpi->maxnvars, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->lb), sdpi->maxnvars, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->ub), sdpi->maxnvars, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->isintegral), sdpi->maxnvars, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpilb), sdpi->maxnvars, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpiub), sdpi->maxnvars, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpilbrowidx), sdpi->maxnvars, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpiubrowidx), sdpi->maxnvars, newsize) );
      sdpi->maxnvars = newsize;
   }

   return SCIP_OKAY;
}

/** ensure size of LP data */
static
SCIP_RETCODE ensureLPDataMemory(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   int                   nlpcons,            /**< number of required LP constraints */
   int                   nlpnonz             /**< number of required LP nonzeros */
   )
{
   int newsize;

   assert( sdpi != NULL );

   if ( nlpcons > sdpi->maxnlpcons )
   {
      newsize = calcGrowSize(sdpi->maxnlpcons, nlpcons);

      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->lplhs), sdpi->maxnlpcons, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->lprhs), sdpi->maxnlpcons, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->lpbeg), sdpi->maxnlpcons, newsize) );

      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpilpindchanges), sdpi->maxnlpcons, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpilplhs), sdpi->maxnlpcons, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpilprhs), sdpi->maxnlpcons, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpilpbeg), sdpi->maxnlpcons, newsize) );

      sdpi->maxnlpcons = newsize;
   }

   if ( nlpnonz > sdpi->maxlpnnonz )
   {
      newsize = calcGrowSize(sdpi->maxlpnnonz, nlpnonz);

      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->lpind), sdpi->maxlpnnonz, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->lpval), sdpi->maxlpnnonz, newsize) );

      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpilpind), sdpi->maxlpnnonz, newsize) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpilpval), sdpi->maxlpnnonz, newsize) );

      sdpi->maxlpnnonz = newsize;
   }

   return SCIP_OKAY;
}

/** ensure size of SDP data */
static
SCIP_RETCODE ensureSDPDataMemory(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   int                   nsdpblocks,         /**< number of required SDP blocks */
   int*                  sdpblocksizes,      /**< sizes of SDP blocks */
   int*                  sdpnblockvars,      /**< number of block variables */
   int**                 sdpnblockvarnonz,   /**< number of nonzeros in each matrix */
   int*                  sdpconstnblocknonz, /**< number of nonzeros for each variable in the constant matrix (size of sdpconst[row/col/val] */
   int                   sdpnnonz,           /**< total number of nonzeros */
   SCIP_Bool             allfixedeigenvecs   /**< whether we need space for eigenvectors if all variables are fixed */
   )
{
   int oldnsdpblocks;
   int cnt = 0;
   int b;
   int v;

   assert( sdpi != NULL );

   if ( nsdpblocks <= 0 )
      return SCIP_OKAY;

   assert( sdpnblockvars != NULL );
   assert( sdpnblockvarnonz != NULL );
   assert( sdpconstnblocknonz != NULL );

   if ( sdpnnonz > sdpi->maxsdpstore )
   {
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdprowstore), sdpi->maxsdpstore, sdpnnonz) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpcolstore), sdpi->maxsdpstore, sdpnnonz) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpvalstore), sdpi->maxsdpstore, sdpnnonz) );
      sdpi->maxsdpstore = sdpnnonz;
   }
   sdpi->sdpnnonz = sdpnnonz;

   /* we assume that the sizes for SDP constraints only change seldomly, so we do not use a grow factor */
   if ( nsdpblocks > sdpi->maxnsdpblocks )
   {
      oldnsdpblocks = sdpi->maxnsdpblocks;

      /* the following array pointers are all initialized (possibly with NULL) */
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpblocksizes), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpnblockvars), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->maxsdpnblockvars), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->maxsdpblocksizes), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstnblocknonz), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->maxsdpconstnblocknonz), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpnblockvarnonz), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstcol), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstrow), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstval), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpvar), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpcol), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdprow), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpval), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->indchanges), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->nremovedinds), sdpi->maxnsdpblocks, nsdpblocks) );
      BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->blockindchanges), sdpi->maxnsdpblocks, nsdpblocks) );
      assert( allfixedeigenvecs || sdpi->allfixedeigenvecs == NULL );
      if ( allfixedeigenvecs )
      {
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->allfixedeigenvecs), sdpi->maxnsdpblocks, nsdpblocks) );
      }
      sdpi->maxnsdpblocks = nsdpblocks;
   }
   else
      oldnsdpblocks = nsdpblocks;

   /* loop through previously existing blocks */
   for (b = 0; b < oldnsdpblocks; ++b)
   {
      /* the following array pointers should be initialized */
      if ( sdpconstnblocknonz[b] > sdpi->maxsdpconstnblocknonz[b] )
      {
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstcol[b]), sdpi->maxsdpconstnblocknonz[b], sdpconstnblocknonz[b]) );
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstrow[b]), sdpi->maxsdpconstnblocknonz[b], sdpconstnblocknonz[b]) );
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstval[b]), sdpi->maxsdpconstnblocknonz[b], sdpconstnblocknonz[b]) );
         sdpi->maxsdpconstnblocknonz[b] = sdpconstnblocknonz[b];
      }

      if ( sdpnblockvars[b] > sdpi->maxsdpnblockvars[b] )
      {
         assert( sdpi->sdpnblockvarnonz[b] != NULL );
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpnblockvarnonz[b]), sdpi->maxsdpnblockvars[b], sdpnblockvars[b]) );
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpvar[b]), sdpi->maxsdpnblockvars[b], sdpnblockvars[b]) );
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdprow[b]), sdpi->maxsdpnblockvars[b], sdpnblockvars[b]) );
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpcol[b]), sdpi->maxsdpnblockvars[b], sdpnblockvars[b]) );
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpval[b]), sdpi->maxsdpnblockvars[b], sdpnblockvars[b]) );
         sdpi->maxsdpnblockvars[b] = sdpnblockvars[b];
      }

      if ( sdpblocksizes[b] > sdpi->maxsdpblocksizes[b] )
      {
         BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->indchanges[b]), sdpi->maxsdpblocksizes[b], sdpblocksizes[b]) );
         if ( allfixedeigenvecs )
         {
            BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->allfixedeigenvecs[b]), sdpi->maxsdpblocksizes[b], sdpblocksizes[b]) );
         }
         sdpi->maxsdpblocksizes[b] = sdpblocksizes[b];
      }

      /* set pointers into storage */
      for (v = 0; v < sdpnblockvars[b]; ++v)
      {
         sdpi->sdprow[b][v] = &sdpi->sdprowstore[cnt];
         sdpi->sdpcol[b][v] = &sdpi->sdpcolstore[cnt];
         sdpi->sdpval[b][v] = &sdpi->sdpvalstore[cnt];
         cnt += sdpnblockvarnonz[b][v];
      }
      assert( cnt <= sdpi->maxsdpstore );
   }

   /* loop through new blocks */
   for (b = oldnsdpblocks; b < nsdpblocks; ++b)
   {
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpnblockvarnonz[b]), sdpnblockvars[b]) );
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpvar[b]), sdpnblockvars[b]) );
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdprow[b]), sdpnblockvars[b]) );
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpcol[b]), sdpnblockvars[b]) );
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpval[b]), sdpnblockvars[b]) );
      sdpi->maxsdpnblockvars[b] = sdpnblockvars[b];

      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstcol[b]), sdpconstnblocknonz[b]) );
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstrow[b]), sdpconstnblocknonz[b]) );
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->sdpconstval[b]), sdpconstnblocknonz[b]) );
      sdpi->maxsdpconstnblocknonz[b] = sdpconstnblocknonz[b];

      /* set pointers into storage */
      for (v = 0; v < sdpnblockvars[b]; ++v)
      {
         sdpi->sdprow[b][v] = &sdpi->sdprowstore[cnt];
         sdpi->sdpcol[b][v] = &sdpi->sdpcolstore[cnt];
         sdpi->sdpval[b][v] = &sdpi->sdpvalstore[cnt];
         cnt += sdpnblockvarnonz[b][v];
      }
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->indchanges[b]), sdpblocksizes[b]) );
      if ( allfixedeigenvecs )
      {
         BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->allfixedeigenvecs[b]), sdpblocksizes[b]) );
      }
      sdpi->maxsdpblocksizes[b] = sdpblocksizes[b];
   }

   return SCIP_OKAY;
}

/** Computes the constant matrix after all variables with lb=ub have been fixed and their nonzeros were moved to the constant matrix.
 *
 *  The size of sdpconstnblocknonz and the first pointers of sdpconst[row/col/val] should be equal to sdpi->nsdpblocks,
 *  the size of sdpconst[row/col/val] [i], which is given in sdpconstnblocknonz, needs to be sufficient.
 */
static
SCIP_RETCODE compConstMatAfterFixings(
   const SCIP_SDPI*      sdpi,               /**< pointer to an SDP-interface structure */
   const SCIP_Real*      sdpilb,             /**< array of lower bounds */
   const SCIP_Real*      sdpiub,             /**< array of upper bounds */
   int*                  sdpconstnnonz,      /**< pointer to store the total number of nonzero elements in the constant matrices of the SDP-blocks */
   int*                  sdpconstnblocknonz, /**< number of nonzeros for each variable in the constant matrix (size of sdpconst[row/col/val] */
   int**                 sdpconstrow,        /**< pointer to store row-indices for each block */
   int**                 sdpconstcol,        /**< pointer to store column-indices for each block */
   SCIP_Real**           sdpconstval         /**< pointer to store the values of the nonzeros for each block */
   )
{
   int i;
   int v;
   int b;
   int* fixedrows;
   int* fixedcols;
   SCIP_Real* fixedvals;

   assert( sdpi != NULL );
   assert( sdpilb != NULL );
   assert( sdpiub != NULL );
   assert( sdpconstnnonz != NULL );
   assert( sdpconstnblocknonz != NULL );
   assert( sdpconstrow != NULL );
   assert( sdpconstcol != NULL );
   assert( sdpconstval != NULL );

   *sdpconstnnonz = 0;

   /* allocate memory for the nonzeros that need to be fixed */
   BMS_CALL( BMSallocBufferMemoryArray(sdpi->bufmem, &fixedrows, sdpi->sdpnnonz) );
   BMS_CALL( BMSallocBufferMemoryArray(sdpi->bufmem, &fixedcols, sdpi->sdpnnonz) );
   BMS_CALL( BMSallocBufferMemoryArray(sdpi->bufmem, &fixedvals, sdpi->sdpnnonz) );

   /* iterate over all variables, saving the nonzeros of the fixed ones */
   for (b = 0; b < sdpi->nsdpblocks; ++b)
   {
      int nfixednonz = 0;
      int varidx;

      for (v = 0; v < sdpi->sdpnblockvars[b]; ++v)
      {
         varidx = sdpi->sdpvar[b][v];
         if ( isFixed(sdpi, varidx) && REALABS(sdpilb[varidx]) > sdpi->epsilon )
         {
            for (i = 0; i < sdpi->sdpnblockvarnonz[b][v]; ++i)
            {
               fixedrows[nfixednonz] = sdpi->sdprow[b][v][i];
               fixedcols[nfixednonz] = sdpi->sdpcol[b][v][i];
               fixedvals[nfixednonz] = - sdpi->sdpval[b][v][i] * sdpilb[varidx]; /* the -1 comes from +y_i A_i but -A_0 */
               ++nfixednonz;
            }
         }
      }

      SCIP_CALL( SCIPsdpVarfixerMergeArraysIntoNew(sdpi->blkmem, sdpi->epsilon,
            sdpi->sdpconstrow[b], sdpi->sdpconstcol[b], sdpi->sdpconstval[b], sdpi->sdpconstnblocknonz[b],
            fixedrows, fixedcols, fixedvals, nfixednonz,
            sdpconstrow[b], sdpconstcol[b], sdpconstval[b], &sdpconstnblocknonz[b]) );
      *sdpconstnnonz += sdpconstnblocknonz[b];
   }

   /* free memory */
   BMSfreeBufferMemoryArray(sdpi->bufmem, &fixedvals);
   BMSfreeBufferMemoryArray(sdpi->bufmem, &fixedcols);
   BMSfreeBufferMemoryArray(sdpi->bufmem, &fixedrows);

   return SCIP_OKAY;
}

/** remove empty rows and columns from given constant matrices
 *
 *  Receives constant matrix after fixings and checks for empty rows and columns in each block, which should be removed
 *  to not harm the Slater condition. It also removes SDP-blocks with no entries left, these are returned in
 *  blockindchanges and nremovedblocks.
 */
static
SCIP_RETCODE findEmptyRowColsSDP(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   int*                  sdpconstnblocknonz, /**< number of nonzeros for each variable in the constant matrix (size of sdpconst[row/col/val] */
   int**                 sdpconstrow,        /**< pointers to row-indices for each block */
   int**                 sdpconstcol,        /**< pointers to column-indices for each block */
   SCIP_Real**           sdpconstval         /**< pointers to the values of the nonzeros for each block */
   )
{
   int b;
   int v;
   int i;

   assert( sdpi != NULL );
   assert( sdpconstnblocknonz != NULL );
   assert( sdpconstrow != NULL );
   assert( sdpconstcol != NULL );
   assert( sdpconstval != NULL );

   if ( sdpi->nsdpblocks <= 0 )
      return SCIP_OKAY;

   assert( sdpi->indchanges != NULL );
   assert( sdpi->nremovedinds != NULL );
   assert( sdpi->blockindchanges != NULL );

   /* initialize indchanges with -1 */
   for (b = 0; b < sdpi->nsdpblocks; ++b)
   {
      for (i = 0; i < sdpi->sdpblocksizes[b]; i++)
         sdpi->indchanges[b][i] = -1;
   }
   sdpi->nremovedblocks = 0;

   /* iterate over all active nonzeros, setting the values of indchange for their row and col to 1 (this is an intermediate value to save that the
    * index is still needed, it will later be set to the number of rows/cols deleted earlier) */
   for (b = 0; b < sdpi->nsdpblocks; ++b)
   {
      int nfoundinds = 0;  /* number of indices already found, saved for prematurely stopping the loops */

      for (v = 0; v < sdpi->sdpnblockvars[b]; ++v)
      {
         if ( ! isFixed(sdpi, sdpi->sdpvar[b][v]) )
         {
            for (i = 0; i < sdpi->sdpnblockvarnonz[b][v]; ++i)
            {
               if ( sdpi->indchanges[b][sdpi->sdprow[b][v][i]] == -1 )
               {
                  sdpi->indchanges[b][sdpi->sdprow[b][v][i]] = 1;
                  ++nfoundinds;
               }

               if ( sdpi->indchanges[b][sdpi->sdpcol[b][v][i]] == -1 )
               {
                  sdpi->indchanges[b][sdpi->sdpcol[b][v][i]] = 1;
                  ++nfoundinds;
               }
               if ( nfoundinds == sdpi->sdpblocksizes[b] )
                  break;   /* we're done for this block */
            }
         }

         if ( nfoundinds == sdpi->sdpblocksizes[b] )
            break;   /* we're done for this block */
      }

      if ( nfoundinds < sdpi->sdpblocksizes[b] )
      {
         /* if some indices haven't been found yet, look in the constant matrix for them */
         for (i = 0; i < sdpconstnblocknonz[b]; ++i)
         {
            assert( REALABS(sdpconstval[b][i]) > sdpi->epsilon ); /* this should really be a nonzero */

            if ( sdpi->indchanges[b][sdpconstrow[b][i]] == -1 )
            {
               sdpi->indchanges[b][sdpconstrow[b][i]] = 1;
               ++nfoundinds;
            }

            if ( sdpi->indchanges[b][sdpconstcol[b][i]] == -1 )
            {
               sdpi->indchanges[b][sdpconstcol[b][i]] = 1;
               ++nfoundinds;
            }

            if ( nfoundinds == sdpi->sdpblocksizes[b] )
               break;   /* we're done for this block */
         }
      }

      /* now iterate over all indices to compute the final values of indchanges, all 0 are set to -1, all 1 are changed to the number of -1 before it */
      sdpi->nremovedinds[b] = 0;
      for (i = 0; i < sdpi->sdpblocksizes[b]; ++i)
      {
         if ( sdpi->indchanges[b][i] == -1 )
         {
            SCIPdebugMessage("empty row and col %d were removed from block %d of SDP %d.\n", i, b, sdpi->sdpid);
            /* this index wasn't found (indchanges was initialized with -1), so it can be removed */
            ++sdpi->nremovedinds[b];
         }
         else
         {
            /* this index has been found, so set the value to the number of removed inds before it */
            sdpi->indchanges[b][i] = sdpi->nremovedinds[b];
         }
      }

      /* check if the block became empty */
      if ( sdpi->nremovedinds[b] == sdpi->sdpblocksizes[b] )
      {
         SCIPdebugMessage("empty block %d detected in SDP %d, this will be removed.\n", b, sdpi->sdpid);
         sdpi->blockindchanges[b] = -1;
         ++sdpi->nremovedblocks;
      }
      else
         sdpi->blockindchanges[b] = sdpi->nremovedblocks;
   }

   return SCIP_OKAY;
}

/** tightens the coefficients of the given row based on the maximal activity and integrality of variables
 *
 *  See cons_linear.c:consdataTightenCoefs() and cuts.c:SCIPcutsTightenCoefficients() for details.
 *  The speed can possibly be improved by sorting the coefficients - see cuts.c:SCIPcutsTightenCoefficients().
 *
 *  Following the dissertation of Achterberg (Algorithm 10.1, page 134), the formulas for tightening a linear constraint
 *  \f$ \underline{\beta} \leq a^T x \leq \overline{\beta} \f$ are as follows:
 *  \f{align*}{
 *      & \text{For all } j \in I \text{ with } a_j > 0,\; \underline{\alpha} + a_j \geq \underline{\beta}, \text{ and } \overline{\alpha} - a_j \leq \overline{\beta}:\\
 *      & a'_j := \max \{ \underline{\beta} - \underline{\alpha},\; \overline{\alpha} - \overline{\beta} \};\\
 *      & \underline{\beta} := \underline{\beta} - (a_j - a'_j) \ell_j,\quad \overline{\beta} := \overline{\beta} - (a_j - a'_j) u_j;\\
 *      & a_j := a'_j.\\[2ex]
 *      & \text{For all } j \in I \text{ with } a_j < 0,\; \underline{\alpha} - a_j \geq \underline{\beta}, \text{ and } \overline{\alpha} + a_j \leq \overline{\beta}: \\
 *      & a'_j := \min \{\underline{\alpha} - \underline{\beta},\; \overline{\beta} - \overline{\alpha} \};\\
 *      & \underline{\beta} := \underline{\beta} - (a_j - a'_j) u_j,\quad \overline{\beta} := \overline{\beta} - (a_j - a'_j) \ell_j;\\
 *      & a_j := a'_j.
 *  \f}
 *  where \f$\underline{\alpha}\f$ and \f$\overline{\alpha}\f$ are the minimal and maximal activities.
 */
static
SCIP_RETCODE tightenRowCoefs(
   SCIP_SDPI*            sdpi,               /**< SDP interface */
   SCIP_Real*            sdpilb,             /**< current lower bounds */
   SCIP_Real*            sdpiub,             /**< current upper bounds */
   SCIP_Real*            rowvals,            /**< nonzero coefficients in row */
   int*                  rowinds,            /**< indices of the variables in row */
   int*                  rownnonz,           /**< pointer to store the number of nonzero coefficients */
   SCIP_Real*            rowlhs,             /**< lhs of row */
   SCIP_Real*            rowrhs,             /**< rhs of row */
   SCIP_Bool*            lhsredundant,       /**< pointer to store whether lhs of row is redundant */
   SCIP_Bool*            rhsredundant,       /**< pointer to store whether rhs of row is redundant */
   int*                  nchgcoefs           /**< pointer to count total number of changed coefficients */
   )
{
   SCIP_Real minact;
   SCIP_Real maxact;
   SCIP_Real QUAD(minactquad);
   SCIP_Real QUAD(maxactquad);
   SCIP_Bool minactinf = FALSE;
   SCIP_Bool maxactinf = FALSE;
   SCIP_Real maxintabsval = 0.0;
   SCIP_Bool hasintvar = FALSE;
   int i;

   assert( sdpi != NULL );
   assert( rowvals != NULL );
   assert( rowinds != NULL );
   assert( rownnonz != NULL );
   assert( rowlhs != NULL );
   assert( rowrhs != NULL );
   assert( lhsredundant != NULL );
   assert( rhsredundant != NULL );
   assert( nchgcoefs != NULL );

   *lhsredundant = FALSE;
   *rhsredundant = FALSE;
   *nchgcoefs = 0;

   /* do nothing for equations: we do not expect to be able to tighten coefficients */
   if ( REALABS(*rowlhs - *rowrhs) < sdpi->epsilon )
      return SCIP_OKAY;

   QUAD_ASSIGN(minactquad, 0.0);
   QUAD_ASSIGN(maxactquad, 0.0);

   /* compute activities */
   for (i = 0; i < *rownnonz; ++i)
   {
      SCIP_Real lb;
      SCIP_Real ub;

      assert( 0 <= rowinds[i] && rowinds[i] < sdpi->nvars );
      lb = sdpilb[rowinds[i]];
      ub = sdpiub[rowinds[i]];

      if ( sdpi->isintegral[rowinds[i]] )
      {
         maxintabsval = MAX(maxintabsval, REALABS(rowvals[i]));
         hasintvar = TRUE;
      }

      /* check sign */
      if ( rowvals[i] > 0.0 )
      {
         /* if upper bound is finite */
         if ( ub < SCIPsdpiInfinity(sdpi) )
            SCIPquadprecSumQD(maxactquad, maxactquad, rowvals[i] * ub);
         else
            maxactinf = TRUE;

         /* if lower bound is finite */
         if ( lb > - SCIPsdpiInfinity(sdpi) )
            SCIPquadprecSumQD(minactquad, minactquad, rowvals[i] * lb);
         else
            minactinf = TRUE;
      }
      else
      {
         /* if lower bound is finite */
         if ( lb > - SCIPsdpiInfinity(sdpi) )
            SCIPquadprecSumQD(maxactquad, maxactquad, rowvals[i] * lb);
         else
            maxactinf = TRUE;

         /* if upper bound is finite */
         if ( ub < SCIPsdpiInfinity(sdpi) )
            SCIPquadprecSumQD(minactquad, minactquad, rowvals[i] * ub);
         else
            minactinf = TRUE;
      }
   }

   /* if the constraint has no integer variable, we cannot tighten the coefficients */
   if ( ! hasintvar )
      return SCIP_OKAY;

   /* if both activities are infinity, we cannot do anything */
   if ( minactinf && maxactinf )
      return SCIP_OKAY;

   /* init activities */
   if ( minactinf )
      minact = - SCIPsdpiInfinity(sdpi);
   else
      minact = QUAD_TO_DBL(minactquad);

   if ( maxactinf )
      maxact = SCIPsdpiInfinity(sdpi);
   else
      maxact = QUAD_TO_DBL(maxactquad);

   /* if row is redundant in activity bounds for lhs */
   if ( *rowlhs <= - SCIPsdpiInfinity(sdpi) )
      *lhsredundant = TRUE;
   else if ( minact >= *rowlhs - sdpi->epsilon )
      *lhsredundant = TRUE;

   /* if row is redundant in activity bounds for rhs */
   if ( *rowrhs >= SCIPsdpiInfinity(sdpi) )
      *rhsredundant = TRUE;
   else if ( maxact <= *rowrhs + sdpi->epsilon )
      *rhsredundant = TRUE;

   /* if both sides are redundant, we can exit */
   if ( *lhsredundant && *rhsredundant )
      return SCIP_OKAY;

   /* no coefficient tightening can be performed if this check is true, see the tests below */
   if ( minact + maxintabsval < *rowlhs - sdpi->epsilon || maxact - maxintabsval > *rowrhs + sdpi->epsilon )
      return SCIP_OKAY;

   /* loop over the integral variables and try to tighten the coefficients */
   for (i = 0; i < *rownnonz;)
   {
      SCIP_Real QUAD(lhsdeltaquad);
      SCIP_Real QUAD(rhsdeltaquad);
      SCIP_Real QUAD(tmpquad);
      SCIP_Real newvallhs;
      SCIP_Real newvalrhs;
      SCIP_Real newval;
      SCIP_Real newlhs;
      SCIP_Real newrhs;
      SCIP_Real lb;
      SCIP_Real ub;

      /* skip continuous variables */
      if ( ! sdpi->isintegral[rowinds[i]] )
      {
         ++i;
         continue;
      }

      lb = sdpilb[rowinds[i]];
      ub = sdpiub[rowinds[i]];

      if ( rowvals[i] > 0.0 && minact + rowvals[i] >= *rowlhs - sdpi->epsilon && maxact - rowvals[i] <= *rowrhs + sdpi->epsilon )
      {
         newvallhs = *rowlhs - minact;
         newvalrhs = maxact - *rowrhs;
         newval = MAX(newvallhs, newvalrhs);
         assert( newval > -sdpi->epsilon );

         if ( REALABS(newval - rowvals[i]) > sdpi->epsilon )
         {
            /* compute new lhs: lhs - (oldval - newval) * lb = lhs + (newval - oldval) * lb */
            if ( *rowlhs > - SCIPsdpiInfinity(sdpi) )
            {
               SCIPquadprecSumDD(lhsdeltaquad, newval, -rowvals[i]);
               SCIPquadprecProdQD(lhsdeltaquad, lhsdeltaquad, lb);
               SCIPquadprecSumQD(tmpquad, lhsdeltaquad, *rowlhs);
               newlhs = QUAD_TO_DBL(tmpquad);
            }
            else
               newlhs = *rowlhs;

            /* compute new rhs: rhs - (oldval - newval) * ub = rhs + (newval - oldval) * ub */
            if ( *rowrhs < SCIPsdpiInfinity(sdpi) )
            {
               SCIPquadprecSumDD(rhsdeltaquad, newval, -rowvals[i]);
               SCIPquadprecProdQD(rhsdeltaquad, rhsdeltaquad, ub);
               SCIPquadprecSumQD(tmpquad, rhsdeltaquad, *rowrhs);
               newrhs = QUAD_TO_DBL(tmpquad);
            }
            else
               newrhs = *rowrhs;

            SCIPdebugPrintf("tightened coefficient from %g to %g; lhs changed from %g to %g; rhs changed from %g to %g; the bounds are [%g,%g]\n",
               rowvals[i], newval, *rowlhs, newlhs, *rowrhs, newrhs, lb, ub);

            *rowlhs = newlhs;
            *rowrhs = newrhs;

            ++(*nchgcoefs);

            if ( newval > sdpi->epsilon )
            {
               if ( *rowlhs > - SCIPsdpiInfinity(sdpi) )
               {
                  SCIPquadprecSumQQ(minactquad, minactquad, lhsdeltaquad);
                  minact = QUAD_TO_DBL(minactquad);
               }
               if ( *rowrhs < SCIPsdpiInfinity(sdpi) )
               {
                  SCIPquadprecSumQQ(maxactquad, maxactquad, rhsdeltaquad);
                  maxact = QUAD_TO_DBL(maxactquad);
               }

               rowvals[i] = newval;
            }
            else
            {
               --(*rownnonz);
               rowvals[i] = rowvals[*rownnonz];
               rowinds[i] = rowinds[*rownnonz];
               continue;
            }
         }
      }
      else if ( rowvals[i] < 0.0 && minact - rowvals[i] >= *rowlhs - sdpi->epsilon && maxact + rowvals[i] <= *rowrhs + sdpi->epsilon )
      {
         newvallhs = minact - *rowlhs;
         newvalrhs = *rowrhs - maxact;
         newval = MIN(newvallhs, newvalrhs);
         assert( newval < sdpi->epsilon );

         if ( REALABS(newval - rowvals[i]) > sdpi->epsilon )
         {
            /* compute new lhs: lhs - (oldval - newval) * ub = lhs + (newval - oldval) * ub */
            if ( *rowlhs > - SCIPsdpiInfinity(sdpi) )
            {
               SCIPquadprecSumDD(lhsdeltaquad, newval, -rowvals[i]);
               SCIPquadprecProdQD(lhsdeltaquad, lhsdeltaquad, ub);
               SCIPquadprecSumQD(tmpquad, lhsdeltaquad, *rowlhs);
               newlhs = QUAD_TO_DBL(tmpquad);
            }
            else
               newlhs = *rowlhs;

            /* compute new rhs: rhs - (oldval - newval) * lb = rhs + (newval - oldval) * lb */
            if ( *rowrhs < SCIPsdpiInfinity(sdpi) )
            {
               SCIPquadprecSumDD(rhsdeltaquad, newval, -rowvals[i]);
               SCIPquadprecProdQD(rhsdeltaquad, rhsdeltaquad, lb);
               SCIPquadprecSumQD(tmpquad, rhsdeltaquad, *rowrhs);
               newrhs = QUAD_TO_DBL(tmpquad);
            }
            else
               newrhs = *rowrhs;

            SCIPdebugPrintf("tightened coefficient from %g to %g; lhs changed from %g to %g; rhs changed from %g to %g; the bounds are [%g,%g]\n",
               rowvals[i], newval, *rowlhs, newlhs, *rowrhs, newrhs, lb, ub);

            *rowlhs = newlhs;
            *rowrhs = newrhs;

            ++(*nchgcoefs);

            if ( newval < - sdpi->epsilon )
            {
               if ( *rowlhs > - SCIPsdpiInfinity(sdpi) )
               {
                  SCIPquadprecSumQQ(minactquad, minactquad, lhsdeltaquad);
                  minact = QUAD_TO_DBL(minactquad);
               }
               if ( *rowrhs < SCIPsdpiInfinity(sdpi) )
               {
                  SCIPquadprecSumQQ(maxactquad, maxactquad, rhsdeltaquad);
                  maxact = QUAD_TO_DBL(maxactquad);
               }

               rowvals[i] = newval;
            }
            else
            {
               --(*rownnonz);
               rowvals[i] = rowvals[*rownnonz];
               rowinds[i] = rowinds[*rownnonz];
               continue;
            }
         }
      }

      ++i;
   }

   return SCIP_OKAY;
}

/** prepares LP data
 *
 *  - remove variables that are fixed and adjust lhs/rhs
 *  - tighten coefficients according to integrality
 *  - remove empty rows
 *  - convert rows with one nonzero into bounds
 *
 *  The same relative order in all data is preserved.
 */
static
SCIP_RETCODE prepareLPData(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   SCIP_Real*            sdpilb,             /**< prepared array of lower bounds */
   SCIP_Real*            sdpiub,             /**< prepared array of upper bounds */
   int*                  sdpilbrowidx,       /**< index of row that provided the bound in sdpilb (or -1) */
   int*                  sdpiubrowidx,       /**< index of row that provided the bound in sdpiub (or -1) */
   int*                  nsdpilpcons,        /**< pointer to store the number of resulting LP-constraints */
   int*                  sdpilpindchanges,   /**< array for the number of LP-constraints removed before the current one (-1 if removed itself) */
   SCIP_Real*            sdpilplhs,          /**< array to store lhs of LP-constraints after fixing variables */
   SCIP_Real*            sdpilprhs,          /**< array to store rhs of LP-constraints after fixing variables */
   int*                  sdpilpnnonz,        /**< pointer to store the number of nonzeros in prepared LP-constraints */
   int*                  sdpilpbeg,          /**< array to store start index of each row in ind- and val-array */
   int*                  sdpilpind,          /**< array to store column-index for each entry in lpval-array */
   SCIP_Real*            sdpilpval,          /**< array to store values of LP-constraint matrix entries */
   SCIP_Bool*            fixingsfound        /**< pointer to store whether a variable was fixed during this function call */
   )
{
   int i;

   assert( sdpi != NULL );
   assert( sdpilb != NULL );
   assert( sdpiub != NULL );
   assert( sdpilbrowidx != NULL );
   assert( sdpiubrowidx != NULL );
   assert( nsdpilpcons != NULL );
   assert( sdpi->nlpcons == 0 || sdpilpindchanges != NULL );
   assert( sdpi->nlpcons == 0 || sdpilplhs != NULL );
   assert( sdpi->nlpcons == 0 || sdpilprhs != NULL );
   assert( sdpi->nlpcons == 0 || sdpilpbeg != NULL );
   assert( sdpilpnnonz != NULL );
   assert( sdpi->nlpcons == 0 || sdpilpind != NULL );
   assert( sdpi->nlpcons == 0 || sdpilpval != NULL );
   assert( fixingsfound != NULL );

   *nsdpilpcons = 0;
   *sdpilpnnonz = 0;
   *fixingsfound = FALSE;

   /* if there is no LP-part, there is nothing to do */
   if ( sdpi->nlpcons == 0 || sdpi->lpnnonz == 0 )
      return SCIP_OKAY;

   /* init row indices */
   for (i = 0; i < sdpi->nvars; ++i)
   {
      sdpi->sdpilbrowidx[i] = 0;
      sdpi->sdpiubrowidx[i] = 0;
   }

   for (i = 0; i < sdpi->nlpcons; ++i)
   {
      SCIP_Real rowconst = 0.0;
      SCIP_Real lhs;
      SCIP_Real rhs;
      int nonzind = -1;
      int nrownonz = 0;
      int nlpnonz;
      int nextbeg;
      int j;

      /* init constraint to be removed */
      sdpilpindchanges[i] = -1;

      assert( 0 <= sdpi->lpbeg[i] && sdpi->lpbeg[i] < sdpi->lpnnonz );
      if ( i == sdpi->nlpcons - 1 )
         nextbeg = sdpi->lpnnonz;
      else
      {
         assert( sdpi->lpbeg[i] <= sdpi->lpbeg[i+1] );
         nextbeg = sdpi->lpbeg[i + 1];
      }

      /* initialize for debugging */
      sdpilplhs[i] = SCIP_INVALID;
      sdpilprhs[i] = SCIP_INVALID;

      sdpi->sdpilpbeg[i] = *sdpilpnnonz;
      nlpnonz = *sdpilpnnonz;
      for (j = sdpi->lpbeg[i]; j < nextbeg; ++j)
      {
         assert( 0 <= sdpi->lpind[j] && sdpi->lpind[j] < sdpi->nvars );

         /* count number of contained active variables */
         if ( ! isFixed(sdpi, sdpi->lpind[j]) )
         {
            sdpilpind[nlpnonz] = sdpi->lpind[j];
            sdpilpval[nlpnonz] = sdpi->lpval[j];
            ++nlpnonz;
            ++nrownonz;
            nonzind = j;  /* store unfixed variable in row (used for rows with nrownonz == 1) */
         }
         else
            rowconst += sdpi->lpval[j] * sdpilb[sdpi->lpind[j]];  /* contribution of the fixed variables */
         assert( ! SCIPsdpiIsInfinity(sdpi, rowconst) );
      }

      if ( sdpi->lplhs[i] > - SCIPsdpiInfinity(sdpi) )
         lhs = sdpi->lplhs[i] - rowconst;
      else
         lhs = - SCIPsdpiInfinity(sdpi);

      if ( sdpi->lprhs[i] < SCIPsdpiInfinity(sdpi) )
         rhs = sdpi->lprhs[i] - rowconst;
      else
         rhs = SCIPsdpiInfinity(sdpi);

      /* if the row has at least two active variables, we keep the lhs- and rhs-value */
      if ( nrownonz >= 2 )
      {
         SCIP_Bool lhsredundant;
         SCIP_Bool rhsredundant;
         int nchgcoefs;

         assert( 0 <= nonzind && nonzind < sdpi->lpnnonz );

         /* try to tighten coefficients based on integrality */
         SCIP_CALL( tightenRowCoefs(sdpi, sdpilb, sdpiub, &sdpilpval[*nsdpilpcons], &sdpilpind[*nsdpilpcons], &nrownonz, &lhs, &rhs, &lhsredundant, &rhsredundant, &nchgcoefs) );

         if ( ! lhsredundant || ! rhsredundant )
         {
            /* store number of rows removed before current one */
            sdpilpindchanges[i] = i - *nsdpilpcons;

            /* possibly correct length */
            nlpnonz = *sdpilpnnonz + nrownonz;

            sdpilplhs[i] = lhs;
            sdpilprhs[i] = rhs;
            *sdpilpnnonz = nlpnonz;
            ++(*nsdpilpcons);
         }
         else
         {
            SCIPdebugMessage("Constraint %d is redundant.\n", i);
         }
      }
      else if ( nrownonz == 1 )
      {
         SCIP_Real lpval;
         SCIP_Real lb;
         SCIP_Real ub;
         int lpcol;

         assert( 0 <= nonzind && nonzind < sdpi->lpnnonz );

         /* check whether the row leads to an improvement in the variables bounds */
         lpcol = sdpi->lpind[nonzind];
         assert( 0 <= lpcol && lpcol < sdpi->nvars );

         lpval = sdpi->lpval[nonzind];
         assert( REALABS(lpval) > sdpi->epsilon );

         /* compute new lower and upper bounds */
         if ( lpval > 0.0 )
         {
            if ( lhs > - SCIPsdpiInfinity(sdpi) )
               lb = lhs / lpval;
            else
               lb = - SCIPsdpiInfinity(sdpi);

            if ( rhs < SCIPsdpiInfinity(sdpi) )
               ub = rhs / lpval;
            else
               ub = SCIPsdpiInfinity(sdpi);
         }
         else
         {
            if ( rhs < SCIPsdpiInfinity(sdpi) )
               lb = rhs / lpval;
            else
               lb = - SCIPsdpiInfinity(sdpi);

            if ( lhs > - SCIPsdpiInfinity(sdpi) )
               ub = lhs / lpval;
            else
               ub = SCIPsdpiInfinity(sdpi);
         }

         /* check whether lower bound is stronger */
         if ( lb > sdpilb[lpcol] + sdpi->epsilon )
         {
            /* this bound is stronger than the original one */
            SCIPdebugMessage("LP-row %d with one nonzero has been removed from SDP %d, lower bound of variable %d has been strenghened to %g "
               "(originally %g)\n", i, sdpi->sdpid, lpcol, lb, sdpilb[lpcol]);
            sdpilb[lpcol] = lb;

            if ( lpval < 0.0 )
               sdpilbrowidx[lpcol] = i + 1;     /* the rhs lead to a change in lb */
            else
               sdpilbrowidx[lpcol] = - i - 1;   /* the lhs lead to a change in lb */
         }

         /* check whether upper bound is stronger */
         if ( ub < sdpiub[lpcol] - sdpi->epsilon )
         {
            /* this bound is stronger than the original one */
            SCIPdebugMessage("LP-row %d with one nonzero has been removed from SDP %d, upper bound of variable %d has been strenghened to %g "
               "(originally %g)\n", i, sdpi->sdpid, lpcol, ub, sdpiub[lpcol]);
            sdpiub[lpcol] = ub;

            if ( lpval > 0.0 )
               sdpiubrowidx[lpcol] = i + 1;     /* the rhs lead to a change in ub */
            else
               sdpiubrowidx[lpcol] = - i - 1;   /* the lhs lead to a change in ub */
         }

         /* check whether this makes the problem infeasible */
         if ( sdpiub[lpcol] < sdpilb[lpcol] - sdpi->epsilon )
         {
            SCIPdebugMessage("Found upper bound %g < lower bound %g for variable %d -> infeasible!\n", sdpiub[lpcol], sdpilb[lpcol], lpcol);
            sdpi->infeasible = TRUE;
            return SCIP_OKAY;
         }

         /* check if this leads to a fixing of this variable */
         if ( REALABS(sdpilb[lpcol] - sdpiub[lpcol]) < sdpi->epsilon )
         {
            SCIPdebugMessage("Fixed variable %d to value %g in SDP %d.\n", lpcol, sdpilb[lpcol], sdpi->sdpid);
            *fixingsfound = TRUE;
         }
      }
      else  /* rows with no entries */
      {
         assert( nonzind < 0 );
         assert( nrownonz == 0 );

         /* we have a constraint lhs <= 0 <= rhs, so lhs should be non-positive and rhs non-negative, otherwise the problem is infeasible */
         if ( lhs > sdpi->feastol || rhs < -sdpi->feastol )
         {
            SCIPdebugMessage("Found constraint  %g <= 0 <= %g after fixings -> infeasible!\n", lhs, rhs);
            sdpi->infeasible = TRUE;
            return SCIP_OKAY;
         }
      }
   }

   return SCIP_OKAY;
}

/** If all variables are fixed, check whether the remaining solution is feasible for the SDP-constraints (LP-constraints
 *  should have been checked already during preprocessing)
 */
static
SCIP_RETCODE checkFixedFeasibilitySdp(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   SCIP_Real*            sdpilb,             /**< array of lower bounds */
   SCIP_Real*            sdpiub              /**< array of upper bounds */
   )
{
   SCIP_Real* fullmatrix; /* we need to give the full matrix to LAPACK */
   int maxsize = -1;
   int b;
   int i;
   int v;

   assert( sdpi->allfixed );
   assert( ! sdpi->infeasible );

   /* as we don't want to allocate memory newly for every SDP-block, we allocate memory according to the size of the largest block */
   for (b = 0; b < sdpi->nsdpblocks; b++)
   {
      if ( sdpi->sdpblocksizes[b] > maxsize )
         maxsize = sdpi->sdpblocksizes[b];
   }
   if ( maxsize < 0 )
      return SCIP_OKAY;

   /* allocate memory */
   BMS_CALL( BMSallocBufferMemoryArray(sdpi->bufmem, &fullmatrix, maxsize * maxsize) ); /*lint !e647*/

   /* iterate over all SDP-blocks and check if the smallest eigenvalue is non-negative */
   for (b = 0; b < sdpi->nsdpblocks; b++)
   {
      SCIP_Real eigenvalue;
      SCIP_Real fixedval;
      int size;
      int r;
      int c;

      size = sdpi->sdpblocksizes[b];
      assert( size <= maxsize );

      /* initialize the matrix with zero */
      for (i = 0; i < size * size; i++)
         fullmatrix[i] = 0.0;

      /* add the constant matrix (with negative sign) */
      for (i = 0; i < sdpi->sdpconstnblocknonz[b]; i++)
      {
         r = sdpi->sdpconstrow[b][i];
         c = sdpi->sdpconstcol[b][i];

         assert( 0 <= r && r < size );
         assert( 0 <= c && c < size );
         fullmatrix[r * size + c] = - sdpi->sdpconstval[b][i]; /*lint !e679*/
         if ( r != c )
            fullmatrix[c * size + r] = - sdpi->sdpconstval[b][i]; /*lint !e679*/
      }

      /* add the contributions of the fixed variables */
      for (v = 0; v < sdpi->sdpnblockvars[b]; v++)
      {
         fixedval = sdpilb[sdpi->sdpvar[b][v]];
         assert( REALABS(fixedval - sdpiub[sdpi->sdpvar[b][v]]) <= sdpi->epsilon );

         /* if the variable is fixed to zero, we can ignore its contributions */
         if ( REALABS(fixedval) < sdpi->epsilon )
            continue;

         /* iterate over all nonzeros */
         for (i = 0; i < sdpi->sdpnblockvarnonz[b][v]; i++)
         {
            r = sdpi->sdprow[b][v][i];
            c = sdpi->sdpcol[b][v][i];

            assert( 0 <= r && r < size );
            assert( 0 <= c && c < size );
            fullmatrix[r * size + c] += fixedval * sdpi->sdpval[b][v][i]; /*lint !e679*/
            if ( r != c )
               fullmatrix[c * size + r] += fixedval * sdpi->sdpval[b][v][i]; /*lint !e679*/
         }
      }

      /* compute the smallest eigenvalue */
      if ( sdpi->allfixedeigenvecs != NULL )
      {
         SCIP_CALL( SCIPlapackComputeIthEigenvalue(sdpi->bufmem, TRUE, size, fullmatrix, 1, &eigenvalue, sdpi->allfixedeigenvecs[b]) );
      }
      else
      {
         SCIP_CALL( SCIPlapackComputeIthEigenvalue(sdpi->bufmem, FALSE, size, fullmatrix, 1, &eigenvalue, NULL) );
      }

      /* check if the eigenvalue is negative */
      if ( eigenvalue < - sdpi->feastol )
      {
         sdpi->infeasible = TRUE;
         SCIPdebugMessage("Detected infeasibility for SDP %d with all variables fixed (minimal eigenvalue: %g)!\n", sdpi->sdpid, eigenvalue);
         break;
      }
   }

   /* free memory */
   BMSfreeBufferMemoryArray(sdpi->bufmem, &fullmatrix);

   return SCIP_OKAY;
}

/** checks primal and dual Slater condition and outputs result depending on Slater settings in SDPI as well as updating
 *  sdpisolver->primalslater and sdpisolver->dualslater
 *
 *  The Slater condition for the dual problem is check using the formulation:
 *  \f{align*}{
 *    \inf\quad & r \\
 *    \mbox{s.t.}\quad & \sum_{j \in J} A_j^{(k)} y_j - A_0^{(k)} + I\, r \succeq 0 && \forall \ k \in K, \\
 *     & \sum_{j \in J} d_{ij}\, y_j \geq c_i && \forall \ i \in I, \\
 *     & \ell_j \leq y_j \leq u_j && \forall \ j \in J,
 *  \f}
 *  If \f$r < 0\f$, then the Slater condition holds.
 *
 *  For the primal problem, the formulation would be as follows:
  *  \f{align*}{
 *    \sup\quad & r \\
 *    \mbox{s.t.}\quad & A_i * (X + I\, r) = c_i && \forall \ i \in I, \\
 *     & X \succeq 0,\; r \geq 0.
 *  \f}
 *  Since we do not want to give equality constraints to the solver by reformulating the primal problem as a dual
 *  problem, we instead solve the primal dual pair
 *  \f{eqnarray*}{
 *  (P) \sup & \begin{pmatrix} 0 & 0\\ 0 & 1 \end{pmatrix} * Y'\\
 *  s.t. & \begin{pmatrix} A_i &  0 \\ 0 & \sum_j (A_i)_{jj} \end{pmatrix} * Y' = c_i & \forall i,\\
 *       &  Y' \succeq 0
 *  \f}
 *  \f{eqnarray*}{
 *  (D) \inf & \sum_i c_i x_i\\
 *   s.t. & \sum_i A_i x_i \succeq 0,\\
 *   & \sum_i \sum_j (A_i)_{jj} x_i \geq 1
 *  \f}
 *  where we also set all finite lhs/rhs of all lp-constraints and varbounds to zero.  If the objective is strictly
 *  positive, then we know that there exists some r > 0 such that Y is psd and \f$Y + r I\f$ is feasible for the equality
 *  constraints in our original primal problem, so \f$Y + r I\f$ is also feasible for the original primal problem and is strictly
 *  positive definite so the primal Slater condition holds.
 */
static
SCIP_RETCODE checkSlaterCondition(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   SCIP_Real             timelimit,          /**< after this many seconds solving will be aborted (currently only implemented for DSDP) */
   SCIP_Real*            sdpilb,             /**< array of lower bounds */
   SCIP_Real*            sdpiub,             /**< array of upper bounds */
   int*                  sdpconstnblocknonz, /**< number of nonzeros for each variable in the constant matrix (size of sdpconst[row/col/val] */
   int                   sdpconstnnonz,      /**< total number of nonzeros in the constant SDP part */
   int**                 sdpconstrow,        /**< pointer to row-indices of constant matrix for each block (may be NULL if sdpconstnnonz = 0) */
   int**                 sdpconstcol,        /**< pointer to column-indices of constant matrix for each block (may be NULL if sdpconstnnonz = 0) */
   SCIP_Real**           sdpconstval,        /**< pointer to values of constant matrix for each block (may be NULL if sdpconstnnonz = 0) */
   int**                 indchanges,         /**< number of indices removed before current row/col index or -1 if removed */
   int*                  nremovedinds,       /**< number of removed variables for each block (may be NULL if sdpi->nsdpblocks = 0) */
   int                   nlpcons,            /**< number of LP-constraints */
   int*                  sdpilpindchanges,   /**< array for the number of LP-constraints removed before the current one (-1 if removed itself) */
   SCIP_Real*            sdpilplhs,          /**< prepared array LP-constraints after fixing variables */
   SCIP_Real*            sdpilprhs,          /**< prepared array of LP-constraints after fixing variables */
   int                   sdpilpnnonz,        /**< number of nonzeros in prepared LP-constraints */
   int*                  sdpilpbeg,          /**< start index of each row in ind- and val-array */
   int*                  sdpilpind,          /**< column-index for each entry in lpval-array */
   SCIP_Real*            sdpilpval,          /**< values of LP-constraint matrix entries */
   int*                  blockindchanges,    /**< array for the index changes for each block, system is the same as for indchanges */
   int                   nremovedblocks,     /**< number of removed SDP-blocks */
   SCIP_Bool             rootnodefailed      /**< if TRUE we will output a message that the root node could not be solved and whether this was due
                                              *   to the Slater condition, otherwise we will print depending on sdpi->slatercheck */
   )
{
   SCIP_Real objval;
   SCIP_Bool origfeas = FALSE;
   SCIP_Bool penaltybound = FALSE;
   int* slaterlpbeg;
   int* slaterlpind;
   int* slaterlpindchanges;
   SCIP_Real* slaterlpval;
   SCIP_Real* slaterlplhs;
   SCIP_Real* slaterlprhs;
   int nremovedslaterlpinds;
   int slaternlpcons;
   SCIP_Real* slaterlb;
   SCIP_Real* slaterub;
   int slaternremovedvarbounds;
   int nremovedlpcons = 0;
   int i;
   int v;
   int b;

   assert( sdpi != NULL );
   assert( sdpconstnnonz == 0 || sdpconstnblocknonz != NULL );
   assert( sdpconstnnonz == 0 || sdpconstrow != NULL );
   assert( sdpconstnnonz == 0 || sdpconstcol != NULL );
   assert( sdpconstnnonz == 0 || sdpconstval != NULL );
   assert( sdpi->nsdpblocks == 0 || indchanges != NULL );
   assert( sdpi->nsdpblocks == 0 || nremovedinds != NULL );
   assert( nlpcons == 0 || sdpilpindchanges != NULL );
   assert( nlpcons == 0 || sdpilplhs != NULL );
   assert( nlpcons == 0 || sdpilprhs != NULL );
   assert( sdpi->nsdpblocks == 0 || blockindchanges != NULL );

   /* we solve the problem with a slack variable times identity added to the constraints and trying to minimize this slack variable r, if we are
    * still feasible for r < - feastol, then we have an interior point with smallest eigenvalue > feastol, otherwise the Slater condition is not fulfilled */
   SCIP_CALL( SCIPsdpiSolverLoadAndSolveWithPenalty(sdpi->sdpisolver, 1.0, FALSE, FALSE, sdpi->nvars, sdpi->obj, sdpilb, sdpiub,
         sdpi->nsdpblocks, sdpi->sdpblocksizes, sdpi->sdpnblockvars, sdpconstnnonz,
         sdpconstnblocknonz, sdpconstrow, sdpconstcol, sdpconstval,
         sdpi->sdpnnonz, sdpi->sdpnblockvarnonz, sdpi->sdpvar, sdpi->sdprow, sdpi->sdpcol, sdpi->sdpval,
         indchanges, nremovedinds, blockindchanges, nremovedblocks,
         nlpcons, sdpilpindchanges, sdpilplhs, sdpilprhs, sdpilpnnonz, sdpilpbeg, sdpilpind, sdpilpval,
         NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
         SCIP_SDPSOLVERSETTING_UNSOLVED, timelimit, sdpi->usedsdpitime, &origfeas, &penaltybound) );

   /* analyze result */
   if ( SCIPsdpiSolverIsOptimal(sdpi->sdpisolver) )
   {
      SCIP_CALL( SCIPsdpiSolverGetObjval(sdpi->sdpisolver, &objval) );

      if ( objval < - sdpi->feastol )
      {
         if ( rootnodefailed )
         {
            SCIPmessagePrintInfo(sdpi->messagehdlr, "Aborting: Failed to solve root node relaxation; Slater condition for dual problem holds (smallest eigenvalue %g).\n", - objval);
         }
         else
            SCIPdebugMessage("Slater condition for SDP %d is fulfilled for dual problem with smallest eigenvalue %g.\n", sdpi->sdpid, -1.0 * objval);/*lint !e687*/
         sdpi->dualslater = SCIP_SDPSLATER_HOLDS;
      }
      else if ( objval < sdpi->feastol )
      {
         if ( rootnodefailed )
         {
            SCIPmessagePrintInfo(sdpi->messagehdlr, "Aborting: Failed to solve root node relaxation; Slater condition for dual problem does not hold (smallest eigenvalue %g).\n", -objval);
         }
         else if ( sdpi->slatercheck == 2 )
         {
            SCIPmessagePrintInfo(sdpi->messagehdlr, "Slater condition for SDP %d not fulfilled for dual problem (smallest eigenvalue %g) - expecting numerical trouble.\n",
               sdpi->sdpid, - objval);
         }
         sdpi->dualslater = SCIP_SDPSLATER_NOT;
      }
      else
      {
         if ( sdpi->slatercheck == 2 )
         {
            SCIPmessagePrintInfo(sdpi->messagehdlr, "Slater condition for SDP %d not fulfilled for dual problem (smallest eigenvalue %g; problem infeasible).\n", sdpi->sdpid, -objval);
         }
         sdpi->dualslater = SCIP_SDPSLATER_INF;
      }
   }
   else if ( SCIPsdpiSolverIsDualUnbounded(sdpi->sdpisolver) )
   {
      if ( rootnodefailed )
      {
         SCIPmessagePrintInfo(sdpi->messagehdlr, "Aborting: Failed to solve root node relaxation; Slater condition for dual problem holds (problem unbounded).\n");
      }
      else
      {
         SCIPdebugMessage("Slater condition for dual problem for SDP %d fulfilled.\n", sdpi->sdpid);/*lint !e687*/
      }
      sdpi->dualslater = SCIP_SDPSLATER_HOLDS;
   }
   else if ( SCIPsdpiSolverIsDualInfeasible(sdpi->sdpisolver) )
   {
      if ( rootnodefailed )
      {
         SCIPmessagePrintInfo(sdpi->messagehdlr, "Aborting: Faild to solve root node relaxation; Slater condition for dual problem does not hold (problem infeasible).\n");
      }
      else if ( sdpi->slatercheck == 2 )
         SCIPmessagePrintInfo(sdpi->messagehdlr, "Slater condition for dual problem for SDP %d not fulfilled (problem infeasible).\n", sdpi->sdpid);
      sdpi->dualslater = SCIP_SDPSLATER_NOT;
   }
   else
   {
      assert( ! SCIPsdpiSolverIsOptimal(sdpi->sdpisolver) && ! SCIPsdpiSolverIsDualUnbounded(sdpi->sdpisolver) && ! SCIPsdpiSolverIsDualInfeasible(sdpi->sdpisolver) );

      if ( rootnodefailed )
      {
         SCIPmessagePrintInfo(sdpi->messagehdlr, "Aborting: Failed to solve root node relaxation; Slater condition for dual problem could not be checked.\n");
      }
      else if ( sdpi->slatercheck == 2 )
         SCIPmessagePrintInfo(sdpi->messagehdlr, "Unable to check Slater condition for dual problem.\n");
      sdpi->dualslater = SCIP_SDPSLATER_NOINFO;
   }


   /* check the Slater condition also for the primal problem */

   /* allocate the LP-arrays, as we have to add the additional LP-constraint. Because we want to add extra entries, we cannot use BMSduplicate... */
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &slaterlpind, sdpilpnnonz + sdpi->nvars) );/*lint !e776*/
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &slaterlpval, sdpilpnnonz + sdpi->nvars) );/*lint !e776*/

   /* copy all old LP-entries */
   for (i = 0; i < sdpilpnnonz; i++)
   {
      slaterlpind[i] = sdpilpind[i];
      slaterlpval[i] = sdpilpval[i];
   }

   /* add the new entries sum_j [(A_i)_jj], for this we have to iterate over the whole sdp-matrices (for all blocks), adding all diagonal entries */
   for (v = 0; v < sdpi->nvars; v++)
   {
      /* fill in all variables - filter out fixed variables below */
      slaterlpind[sdpilpnnonz + v] = v;/*lint !e679*/
      slaterlpval[sdpilpnnonz + v] = 0.0;/*lint !e679*/
   }

   for (b = 0; b < sdpi->nsdpblocks; b++)
   {
      for (v = 0; v < sdpi->sdpnblockvars[b]; v++)
      {
         if ( ! isFixed(sdpi, v) )
         {
            for (i = 0; i < sdpi->sdpnblockvarnonz[b][v]; i++)
            {
               if ( sdpi->sdprow[b][v][i] == sdpi->sdpcol[b][v][i] ) /* it is a diagonal entry */
                  slaterlpval[sdpilpnnonz + sdpi->sdpvar[b][v]] += sdpi->sdpval[b][v][i];/*lint !e679*/
            }
         }
      }
   }

   /* iterate over all added LP-entries and remove all zeros or fixed variables (by shifting further variables) */
   nremovedslaterlpinds = 0;
   for (v = 0; v < sdpi->nvars; v++)
   {
      if ( isFixed(sdpi, v) || REALABS(slaterlpval[sdpilpnnonz + v]) <= sdpi->epsilon )/*lint !e679*/
         ++nremovedslaterlpinds;
      else
      {
         /* shift the entries */
         slaterlpind[sdpilpnnonz + v - nremovedslaterlpinds] = slaterlpind[sdpilpnnonz + v];/*lint !e679*/
         slaterlpval[sdpilpnnonz + v - nremovedslaterlpinds] = slaterlpval[sdpilpnnonz + v];/*lint !e679*/
      }
   }

   /* allocate memory for l/r-hs */
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &slaterlpindchanges, nlpcons + 1) );/*lint !e776*/
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &slaterlplhs, nlpcons + 1) );/*lint !e776*/
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &slaterlprhs, nlpcons + 1) );/*lint !e776*/
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &slaterlpbeg, nlpcons + 1) );/*lint !e776*/

   /* set the old entries to zero (if existing), as A_0 (including the LP-part) is removed because of the changed primal objective */
   for (i = 0; i < nlpcons; i++)
   {
      slaterlpindchanges[i] = sdpilpindchanges[i];
      slaterlpbeg[i] = sdpilpbeg[i];
      if ( sdpilpindchanges[i] >= 0 )
      {
         if ( SCIPsdpiSolverIsInfinity(sdpi->sdpisolver, sdpilplhs[i]) )
            slaterlplhs[i] = sdpilplhs[i];
         else
            slaterlplhs[i] = 0.0;

         if ( SCIPsdpiSolverIsInfinity(sdpi->sdpisolver, sdpilprhs[i]) )
            slaterlprhs[i] = sdpilprhs[i];
         else
            slaterlprhs[i] = 0.0;
      }
      else
         ++nremovedlpcons;
   }

   /* add the new ones */
   slaterlpindchanges[nlpcons] = nremovedlpcons;
   slaterlplhs[nlpcons] = 1.0;
   slaterlprhs[nlpcons] = SCIPsdpiSolverInfinity(sdpi->sdpisolver);
   slaterlpbeg[nlpcons] = sdpilpnnonz;

   /* determine number of LP constraints */
   if ( nremovedslaterlpinds < sdpi->nvars )
      slaternlpcons = nlpcons + 1;
   else
      slaternlpcons = nlpcons; /* in this case there are no entries in the last row, so we skip it */

   /* copy the varbound arrays to change all finite varbounds to zero */
   DUPLICATE_ARRAY_NULL(sdpi->blkmem, &slaterlb, sdpilb, sdpi->nvars);
   DUPLICATE_ARRAY_NULL(sdpi->blkmem, &slaterub, sdpiub, sdpi->nvars);

   /* set all finite varbounds to zero */
   slaternremovedvarbounds = 0;
   for (v = 0; v < sdpi->nvars; v++)
   {
      if ( ! SCIPsdpiSolverIsInfinity(sdpi->sdpisolver, slaterlb[v]) )
      {
         slaterlb[v] = 0.0;
         slaternremovedvarbounds++;
      }
      if ( ! SCIPsdpiSolverIsInfinity(sdpi->sdpisolver, slaterub[v]) )
      {
         slaterub[v] = 0.0;
         slaternremovedvarbounds++;
      }
   }

   /* if all variables have finite upper and lower bounds these add variables to every constraint of the
    * primal problem that allow us to make the problem feasible for every primal matrix X, so the primal
    * Slater condition holds */
   if ( slaternremovedvarbounds == 2 * sdpi->nvars )
   {
      if ( rootnodefailed )
      {
         SCIPmessagePrintInfo(sdpi->messagehdlr, "Slater condition for primal problem holds since all variables have finite upper and lower bounds.\n");
      }
      else
         SCIPdebugMessage("Slater condition for primal problem for SDP %d fulfilled since all variables have finite upper and lower bounds.\n", sdpi->sdpid);/*lint !e687*/
      sdpi->primalslater = SCIP_SDPSLATER_HOLDS;
   }
   else
   {
      /* solve the problem to check Slater condition for primal of original problem */
      SCIP_CALL( SCIPsdpiSolverLoadAndSolve(sdpi->sdpisolver, sdpi->nvars, sdpi->obj, slaterlb, slaterub,
            sdpi->nsdpblocks, sdpi->sdpblocksizes, sdpi->sdpnblockvars, 0, NULL, NULL, NULL, NULL,
            sdpi->sdpnnonz, sdpi->sdpnblockvarnonz, sdpi->sdpvar, sdpi->sdprow, sdpi->sdpcol,
            sdpi->sdpval, indchanges, nremovedinds, blockindchanges, nremovedblocks, slaternlpcons, slaterlpindchanges, slaterlplhs, slaterlprhs,
            sdpilpnnonz + sdpi->nvars - nremovedslaterlpinds, slaterlpbeg, slaterlpind, slaterlpval, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, SCIP_SDPSOLVERSETTING_UNSOLVED, timelimit, sdpi->usedsdpitime) );

      /* analyze result */
      if ( SCIPsdpiSolverIsOptimal(sdpi->sdpisolver) )
      {
         SCIP_CALL( SCIPsdpiSolverGetObjval(sdpi->sdpisolver, &objval) );

         if ( objval > - sdpi->feastol)
         {
            if ( rootnodefailed )
            {
               SCIPmessagePrintInfo(sdpi->messagehdlr, "Slater condition for primal problem not fulfilled (smallest eigenvalue %g).\n", - objval);
            }
            else if ( sdpi->slatercheck == 2 )
            {
               SCIPmessagePrintInfo(sdpi->messagehdlr, "Slater condition for primal problem for SDP %d not fulfilled "
                        "(smallest eigenvalue %g) - expect numerical trouble or infeasible problem.\n",sdpi->sdpid, - objval);
            }
            sdpi->primalslater = SCIP_SDPSLATER_NOT;
         }
         else
         {
            if ( rootnodefailed )
            {
               SCIPmessagePrintInfo(sdpi->messagehdlr, "Slater condition for primal problem fulfilled (smallest eigenvalue %g).\n", - objval);
            }
            else
               SCIPdebugMessage("Slater condition for primal problem of SDP %d is fulfilled (smallest eigenvalue %g).\n", sdpi->sdpid, - objval);/*lint !e687*/
            sdpi->primalslater = SCIP_SDPSLATER_HOLDS;
         }
      }
      else if ( SCIPsdpiSolverIsDualUnbounded(sdpi->sdpisolver) )
      {
         if ( rootnodefailed )
         {
            SCIPmessagePrintInfo(sdpi->messagehdlr, "Primal Slater condition shows infeasibility.\\n");
         }
         else if ( sdpi->slatercheck == 2 )
         {
            SCIPmessagePrintInfo(sdpi->messagehdlr, "Slater condition for primal problem for SDP %d not fulfilled "
                  "(smallest eigenvalue has to be negative, so primal problem is infeasible; if the dual slater condition holds,"
                  "this means, that the original (dual) problem is unbounded).\n", sdpi->sdpid);
         }
         sdpi->primalslater = SCIP_SDPSLATER_NOT;
      }
      else if ( SCIPsdpiSolverIsPrimalUnbounded(sdpi->sdpisolver) )
      {
         if ( rootnodefailed )
         {
            SCIPmessagePrintInfo(sdpi->messagehdlr, "Slater condition for primal problems holds sunce smallest eigenvalue maximization problem is unbounded.\n");
         }
         else
            SCIPdebugMessage("Slater condition for primal problem for SDP %d fulfilled, smallest eigenvalue maximization problem unbounded.\n", sdpi->sdpid);/*lint !e687*/
         sdpi->primalslater = SCIP_SDPSLATER_HOLDS;
      }
      else
      {
         assert( ! SCIPsdpiSolverIsOptimal(sdpi->sdpisolver) && ! SCIPsdpiSolverIsDualUnbounded(sdpi->sdpisolver) && ! SCIPsdpiSolverIsPrimalUnbounded(sdpi->sdpisolver) );

         if ( rootnodefailed )
         {
            SCIPmessagePrintInfo(sdpi->messagehdlr, "Unable to check Slater condition for primal problem.\n");
         }
         else if ( sdpi->slatercheck == 2 )
            SCIPmessagePrintInfo(sdpi->messagehdlr, "Unable to check Slater condition for primal problem, could not solve auxilliary problem.\n");
         sdpi->primalslater = SCIP_SDPSLATER_NOINFO;
      }
   }

   /* free all memory */
   BMSfreeBlockMemoryArray(sdpi->blkmem, &slaterub, sdpi->nvars);/*lint !e737*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &slaterlb, sdpi->nvars);/*lint !e737*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &slaterlpindchanges, nlpcons + 1);/*lint !e737*//*lint !e776*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &slaterlprhs, nlpcons + 1);/*lint !e737*//*lint !e776*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &slaterlplhs, nlpcons + 1);/*lint !e737*//*lint !e776*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &slaterlpbeg, nlpcons + 1);/*lint !e737*//*lint !e776*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &slaterlpval, sdpilpnnonz + sdpi->nvars);/*lint !e737*//*lint !e776*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &slaterlpind, sdpilpnnonz + sdpi->nvars);/*lint !e737*//*lint !e776*/

   return SCIP_OKAY;
}

/*
 * Miscellaneous Methods
 */

/**@name Miscellaneous Methods */
/**@{ */


/** gets name and potentially version of SDP-solver */
const char* SCIPsdpiGetSolverName(
   void
   )
{
   return SCIPsdpiSolverGetSolverName();
}

/** gets description of SDP-solver (developer, webpage, ...) */
const char* SCIPsdpiGetSolverDesc(
   void
   )
{
   return SCIPsdpiSolverGetSolverDesc();
}

/** gets pointer for SDP-solver - use only with great care
 *
 *  The behavior of this function depends on the solver and its use is
 *  therefore only recommended if you really know what you are
 *  doing. In general, it returns a pointer to the SDP-solver object.
 */
void* SCIPsdpiGetSolverPointer(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   return SCIPsdpiSolverGetSolverPointer(sdpi->sdpisolver);
}

/** gets default number of increases of penalty parameter for SDP-solver in SCIP-SDP */
int SCIPsdpiGetDefaultSdpiSolverNpenaltyIncreases(
   void
   )
{
   return SCIPsdpiSolverGetDefaultSdpiSolverNpenaltyIncreases();
}

/** Should primal solution values be saved for warmstarting purposes? */
SCIP_Bool SCIPsdpiDoesWarmstartNeedPrimal(
   void
   )
{
   return SCIPsdpiSolverDoesWarmstartNeedPrimal();
}

/**@} */


/*
 * SDPI Creation and Destruction Methods
 */

/**@name SDPI Creation and Destruction Methods */
/**@{ */

/** creates an SDPI object */
SCIP_RETCODE SCIPsdpiCreate(
   SCIP_SDPI**           sdpi,               /**< pointer to an SDP-interface structure */
   SCIP_MESSAGEHDLR*     messagehdlr,        /**< message handler to use for printing messages, or NULL */
   BMS_BLKMEM*           blkmem,             /**< block memory */
   BMS_BUFMEM*           bufmem              /**< buffer memory */
   )
{
   assert( sdpi != NULL );
   assert( blkmem != NULL );

   SCIPdebugMessage("Calling SCIPsdpiCreate\n");

   BMS_CALL( BMSallocBlockMemory(blkmem, sdpi) );

   SCIP_CALL( SCIPsdpiSolverCreate(&((*sdpi)->sdpisolver), messagehdlr, blkmem, bufmem) );

   (*sdpi)->messagehdlr = messagehdlr;
   (*sdpi)->blkmem = blkmem;
   (*sdpi)->bufmem = bufmem;
   (*sdpi)->sdpid = 1;
   (*sdpi)->niterations = 0;
   (*sdpi)->opttime = 0.0;
   (*sdpi)->nsdpcalls = 0;
   (*sdpi)->nvars = 0;
   (*sdpi)->maxnvars = 0;
   (*sdpi)->nsdpblocks = 0;
   (*sdpi)->maxnsdpblocks = 0;
   (*sdpi)->sdpconstnnonz = 0;
   (*sdpi)->sdpnnonz = 0;
   (*sdpi)->nlpcons = 0;
   (*sdpi)->maxnlpcons = 0;
   (*sdpi)->nactivelpcons = -1;
   (*sdpi)->lpnnonz = 0;
   (*sdpi)->maxlpnnonz = 0;
   (*sdpi)->slatercheck = 0;
   (*sdpi)->solved = FALSE;
   (*sdpi)->penalty = FALSE;
   (*sdpi)->infeasible = FALSE;
   (*sdpi)->allfixed = FALSE;

   (*sdpi)->obj = NULL;
   (*sdpi)->lb = NULL;
   (*sdpi)->ub = NULL;
   (*sdpi)->isintegral = NULL;
   (*sdpi)->sdpilb = NULL;
   (*sdpi)->sdpiub = NULL;
   (*sdpi)->sdpilbrowidx = NULL;
   (*sdpi)->sdpiubrowidx = NULL;
   (*sdpi)->sdpblocksizes = NULL;
   (*sdpi)->sdpnblockvars = NULL;
   (*sdpi)->maxsdpnblockvars = NULL;
   (*sdpi)->maxsdpblocksizes = NULL;
   (*sdpi)->sdpconstnblocknonz = NULL;
   (*sdpi)->maxsdpconstnblocknonz = NULL;
   (*sdpi)->sdpconstrow = NULL;
   (*sdpi)->sdpconstcol = NULL;
   (*sdpi)->sdpconstval = NULL;
   (*sdpi)->sdpnblockvarnonz = NULL;
   (*sdpi)->sdpvar = NULL;
   (*sdpi)->sdprow = NULL;
   (*sdpi)->sdpcol = NULL;
   (*sdpi)->sdpval = NULL;
   (*sdpi)->maxsdpstore = 0;
   (*sdpi)->sdprowstore = NULL;
   (*sdpi)->sdpcolstore = NULL;
   (*sdpi)->sdpvalstore = NULL;
   (*sdpi)->indchanges = NULL;
   (*sdpi)->nremovedinds = NULL;
   (*sdpi)->blockindchanges = NULL;
   (*sdpi)->allfixedeigenvecs = NULL;
   (*sdpi)->nremovedblocks = 0;

   (*sdpi)->lplhs = NULL;
   (*sdpi)->lprhs = NULL;
   (*sdpi)->lpbeg = NULL;
   (*sdpi)->lpind = NULL;
   (*sdpi)->lpval = NULL;

   (*sdpi)->sdpilpindchanges = NULL;
   (*sdpi)->sdpilplhs = NULL;
   (*sdpi)->sdpilprhs = NULL;
   (*sdpi)->sdpilpbeg = NULL;
   (*sdpi)->sdpilpind = NULL;
   (*sdpi)->sdpilpval = NULL;

   (*sdpi)->epsilon = DEFAULT_EPSILON;
   (*sdpi)->gaptol = DEFAULT_SDPSOLVERGAPTOL;
   (*sdpi)->feastol = DEFAULT_FEASTOL;
   (*sdpi)->penaltyparam = DEFAULT_PENALTYPARAM;
   (*sdpi)->maxpenaltyparam = DEFAULT_MAXPENALTYPARAM;
   (*sdpi)->npenaltyincr = DEFAULT_NPENALTYINCR;
   (*sdpi)->bestbound = -SCIPsdpiSolverInfinity((*sdpi)->sdpisolver);
   (*sdpi)->primalslater = SCIP_SDPSLATER_NOINFO;
   (*sdpi)->dualslater = SCIP_SDPSLATER_NOINFO;
   (*sdpi)->solvedonevarsdp = SCIP_ONEVAR_UNSOLVED;
   (*sdpi)->onevarsdpobjval = SCIP_INVALID;
   (*sdpi)->onevarsdpoptval = SCIP_INVALID;
   (*sdpi)->onevarsdpidx = -1;
   (*sdpi)->onevarsdpcertvec = NULL;
   (*sdpi)->onevarsdpcertsize = -1;
   (*sdpi)->onevarsdpcertval = SCIP_INVALID;

   (*sdpi)->nallfixed = 0;
   (*sdpi)->ninfeasible = 0;
   (*sdpi)->nonevarsdp = 0;

   SCIP_CALL( SDPIclockCreate(&(*sdpi)->usedsdpitime) );

   return SCIP_OKAY;
}

/** deletes an SDPI object */
SCIP_RETCODE SCIPsdpiFree(
   SCIP_SDPI**           sdpi                /**< pointer to an SDP-interface structure */
   )
{
   int i;

   SCIPdebugMessage("Calling SCIPsdpiFree ...\n");

   assert( sdpi != NULL );
   assert( *sdpi != NULL );

   /* free clock */
   SDPIclockFree(&(*sdpi)->usedsdpitime);

   /* free the LP part */
   assert( 0 <= (*sdpi)->lpnnonz && (*sdpi)->lpnnonz <= (*sdpi)->maxlpnnonz );
   assert( 0 <= (*sdpi)->nlpcons && (*sdpi)->nlpcons <= (*sdpi)->maxnlpcons );

   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpilpind), (*sdpi)->maxlpnnonz);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpilpval), (*sdpi)->maxlpnnonz);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpilprhs), (*sdpi)->maxnlpcons);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpilplhs), (*sdpi)->maxnlpcons);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpilpindchanges), (*sdpi)->maxnlpcons);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpilpbeg), (*sdpi)->maxnlpcons);

   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->lpval), (*sdpi)->maxlpnnonz);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->lpind), (*sdpi)->maxlpnnonz);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->lprhs), (*sdpi)->maxnlpcons);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->lplhs), (*sdpi)->maxnlpcons);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->lpbeg), (*sdpi)->maxnlpcons);

   /* free the individual SDP nonzeros */
   assert( 0 <= (*sdpi)->nsdpblocks && (*sdpi)->nsdpblocks <= (*sdpi)->maxnsdpblocks );
   for (i = 0; i < (*sdpi)->maxnsdpblocks; i++)
   {
      assert( 0 <= (*sdpi)->sdpnblockvars[i] && (*sdpi)->sdpnblockvars[i] <= (*sdpi)->maxsdpnblockvars[i] );
      BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpval[i]), (*sdpi)->maxsdpnblockvars[i]);
      BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdprow[i]), (*sdpi)->maxsdpnblockvars[i]);
      BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpcol[i]), (*sdpi)->maxsdpnblockvars[i]);
      BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpvar[i]), (*sdpi)->maxsdpnblockvars[i]);
      BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpnblockvarnonz[i]), (*sdpi)->maxsdpnblockvars[i]);
      BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpconstval[i]), (*sdpi)->maxsdpconstnblocknonz[i]);
      BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpconstrow[i]), (*sdpi)->maxsdpconstnblocknonz[i]);
      BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpconstcol[i]), (*sdpi)->maxsdpconstnblocknonz[i]);
      BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->indchanges[i]), (*sdpi)->maxsdpblocksizes[i]);
      if ( (*sdpi)->allfixedeigenvecs != NULL )
         BMSfreeBlockMemoryArray((*sdpi)->blkmem, &((*sdpi)->allfixedeigenvecs[i]), (*sdpi)->maxsdpblocksizes[i]);
   }

   /* free the rest */
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->blockindchanges), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->nremovedinds), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->indchanges), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->allfixedeigenvecs), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpnblockvarnonz), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpconstnblocknonz), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->maxsdpconstnblocknonz), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpval), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpcol), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdprow), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpvar), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpconstval), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpconstcol), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpconstrow), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpnblockvars), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->maxsdpblocksizes), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->maxsdpnblockvars), (*sdpi)->maxnsdpblocks);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpblocksizes), (*sdpi)->maxnsdpblocks);

   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &(*sdpi)->sdpvalstore, (*sdpi)->maxsdpstore);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &(*sdpi)->sdpcolstore, (*sdpi)->maxsdpstore);
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &(*sdpi)->sdprowstore, (*sdpi)->maxsdpstore);

   assert( 0 <= (*sdpi)->nvars && (*sdpi)->nvars <= (*sdpi)->maxnvars );
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpiub), (*sdpi)->maxnvars);/*lint !e737*/
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpilb), (*sdpi)->maxnvars);/*lint !e737*/
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpiubrowidx), (*sdpi)->maxnvars);/*lint !e737*/
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->sdpilbrowidx), (*sdpi)->maxnvars);/*lint !e737*/
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->isintegral), (*sdpi)->maxnvars);/*lint !e737*/
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->ub), (*sdpi)->maxnvars);/*lint !e737*/
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->lb), (*sdpi)->maxnvars);/*lint !e737*/
   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->obj), (*sdpi)->maxnvars);/*lint !e737*/

   BMSfreeBlockMemoryArrayNull((*sdpi)->blkmem, &((*sdpi)->onevarsdpcertvec), (*sdpi)->onevarsdpcertsize);

   /* free the solver */
   SCIP_CALL( SCIPsdpiSolverFree(&((*sdpi)->sdpisolver)) );

   BMSfreeBlockMemory((*sdpi)->blkmem, sdpi);

   return SCIP_OKAY;
}

/** cloning method of the general SDP-Interface
 *
 *  @note The solver specific interface is created anew and not copied.
 */
SCIP_RETCODE SCIPsdpiClone(
   SCIP_SDPI*            oldsdpi,            /**< pointer to the SDP-interface structure that should be cloned */
   SCIP_SDPI*            newsdpi             /**< pointer to an SDP-interface structure to clone into */
   )
{
   BMS_BLKMEM* blkmem;
   int nvars;
   int nsdpblocks;
   int lpnnonz;
   int cnt = 0;
   int b;
   int v;

   assert( oldsdpi != NULL );

   SCIPdebugMessage("Cloning SDPI %d\n", oldsdpi->sdpid);

   /* general data */
   blkmem = oldsdpi->blkmem;
   nvars = oldsdpi->nvars;
   nsdpblocks = oldsdpi->nsdpblocks;
   lpnnonz = oldsdpi->lpnnonz;

   BMS_CALL( BMSallocBlockMemory(blkmem, &newsdpi) );

   SCIP_CALL( SCIPsdpiSolverCreate(&(newsdpi->sdpisolver), oldsdpi->messagehdlr, oldsdpi->blkmem, oldsdpi->bufmem) ); /* create new SDP-Solver Interface */

   newsdpi->messagehdlr = oldsdpi->messagehdlr;
   newsdpi->blkmem = blkmem;
   newsdpi->nvars = nvars;
   newsdpi->maxnvars = nvars;

   assert( 0 <= oldsdpi->nvars && oldsdpi->nvars <= oldsdpi->maxnvars );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->obj), oldsdpi->obj, nvars) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->lb), oldsdpi->lb, nvars) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->ub), oldsdpi->ub, nvars) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->isintegral), oldsdpi->isintegral, nvars) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpilb), nvars) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpiub), nvars) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpilbrowidx), nvars) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpiubrowidx), nvars) );

   newsdpi->nsdpblocks = nsdpblocks;
   newsdpi->maxnsdpblocks = nsdpblocks;

   assert( 0 <= oldsdpi->nsdpblocks && oldsdpi->nsdpblocks <= oldsdpi->maxnsdpblocks );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->sdpblocksizes), oldsdpi->sdpblocksizes, nsdpblocks) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->sdpnblockvars), oldsdpi->sdpnblockvars, nsdpblocks) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->maxsdpnblockvars), oldsdpi->sdpnblockvars, nsdpblocks) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->maxsdpblocksizes), oldsdpi->sdpblocksizes, nsdpblocks) );

   /* constant SDP data */
   newsdpi->sdpconstnnonz = oldsdpi->sdpconstnnonz;

   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->sdpconstnblocknonz), oldsdpi->sdpconstnblocknonz, nsdpblocks) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->maxsdpconstnblocknonz), oldsdpi->sdpconstnblocknonz, nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpconstrow), nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpconstcol), nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpconstval), nsdpblocks) );

   for (b = 0; b < nsdpblocks; b++)
   {
      BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->sdpconstrow[b]), oldsdpi->sdpconstrow[b], oldsdpi->sdpconstnblocknonz[b]) );
      BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->sdpconstcol[b]), oldsdpi->sdpconstcol[b], oldsdpi->sdpconstnblocknonz[b]) );
      BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->sdpconstval[b]), oldsdpi->sdpconstval[b], oldsdpi->sdpconstnblocknonz[b]) );
   }

   /* SDP data */
   newsdpi->sdpnnonz = oldsdpi->sdpnnonz;
   newsdpi->maxsdpstore = oldsdpi->sdpnnonz;

   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpnblockvarnonz), nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpvar), nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdprow), nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpcol), nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpval), nsdpblocks) );

   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdprowstore), newsdpi->maxsdpstore) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpcolstore), newsdpi->maxsdpstore) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpvalstore), newsdpi->maxsdpstore) );

   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->indchanges), nsdpblocks) );
   if ( oldsdpi->allfixedeigenvecs != NULL )
   {
      BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->allfixedeigenvecs), nsdpblocks) );
   }
   else
      newsdpi->allfixedeigenvecs = NULL;
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->nremovedinds), nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->blockindchanges), nsdpblocks) );
   newsdpi->nremovedblocks = 0;

   for (b = 0; b < nsdpblocks; b++)
   {
      assert( 0 <= oldsdpi->sdpnblockvars[b] && oldsdpi->sdpnblockvars[b] <= oldsdpi->maxsdpnblockvars[b] );
      BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->sdpnblockvarnonz[b]), oldsdpi->sdpnblockvarnonz[b], oldsdpi->sdpnblockvars[b]) );
      BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->sdpvar[b]), oldsdpi->sdpvar[b], oldsdpi->sdpnblockvars[b]) );

      BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdprow[b]), oldsdpi->sdpnblockvars[b]) );
      BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpcol[b]), oldsdpi->sdpnblockvars[b]) );
      BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpval[b]), oldsdpi->sdpnblockvars[b]) );

      BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->indchanges[b]), oldsdpi->sdpblocksizes[b]) );
      if ( newsdpi->allfixedeigenvecs != NULL )
      {
         BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->allfixedeigenvecs[b]), oldsdpi->sdpblocksizes[b]) );
      }

      /* set pointers into storage */
      for (v = 0; v < newsdpi->sdpnblockvars[b]; ++v)
      {
         newsdpi->sdprow[b][v] = &newsdpi->sdprowstore[cnt];
         newsdpi->sdpcol[b][v] = &newsdpi->sdpcolstore[cnt];
         newsdpi->sdpval[b][v] = &newsdpi->sdpvalstore[cnt];
         cnt += newsdpi->sdpnblockvarnonz[b][v];
      }
      assert( cnt <= newsdpi->maxsdpstore );
   }

   /* LP data */
   newsdpi->nlpcons = oldsdpi->nlpcons;
   newsdpi->maxnlpcons = oldsdpi->nlpcons;
   newsdpi->nactivelpcons = -1;

   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->lplhs), oldsdpi->lplhs, oldsdpi->nlpcons) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->lprhs), oldsdpi->lprhs, oldsdpi->nlpcons) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->lpbeg), oldsdpi->lpbeg, oldsdpi->nlpcons) );

   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpilpindchanges), oldsdpi->nlpcons) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpilplhs), oldsdpi->nlpcons) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpilprhs), oldsdpi->nlpcons) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpilpbeg), oldsdpi->nlpcons) );

   newsdpi->lpnnonz = lpnnonz;
   newsdpi->maxlpnnonz = lpnnonz;

   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->lpind), oldsdpi->lpind, lpnnonz) );
   BMS_CALL( BMSduplicateBlockMemoryArray(blkmem, &(newsdpi->lpval), oldsdpi->lpval, lpnnonz) );

   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpilpind), lpnnonz) );
   BMS_CALL( BMSallocBlockMemoryArray(blkmem, &(newsdpi->sdpilpval), lpnnonz) );

   /* other data */
   newsdpi->solved = FALSE; /* as we don't copy the sdpisolver, this needs to be set to false */
   newsdpi->penalty = FALSE; /* all things about SDP-solutions are set to false as well, as we didn't solve the problem */
   newsdpi->infeasible = FALSE;
   newsdpi->allfixed = FALSE;
   newsdpi->sdpid = 1000000 + oldsdpi->sdpid; /* this is only used for debug output, setting it to this value should make it clear, that it is a new sdpi */
   newsdpi->epsilon = oldsdpi->epsilon;
   newsdpi->gaptol = oldsdpi->gaptol;
   newsdpi->feastol = oldsdpi->feastol;

   newsdpi->solvedonevarsdp = SCIP_ONEVAR_UNSOLVED;
   newsdpi->onevarsdpobjval = SCIP_INVALID;
   newsdpi->onevarsdpoptval = SCIP_INVALID;
   newsdpi->onevarsdpidx = -1;
   newsdpi->onevarsdpcertvec = NULL;
   newsdpi->onevarsdpcertsize = -1;
   newsdpi->onevarsdpcertval = SCIP_INVALID;

   newsdpi->nallfixed = 0;
   newsdpi->ninfeasible = 0;
   newsdpi->nonevarsdp = 0;

   SCIP_CALL( SDPIclockCreate(&newsdpi->usedsdpitime) );

   return SCIP_OKAY;
}

/**@} */


/*
 * Modification Methods
 */

/**@name Modification Methods */
/**@{ */

/** copies SDP data into SDP-solver
 *
 *  @note As the SDP-constraint-matrices are symmetric, only the lower triangular part of them must be specified.
  * @note It is assumed that the matrices are in lower triangular form.
 *  @note There must be at least one variable, the SDP- and/or LP-part may be empty.
 */
SCIP_RETCODE SCIPsdpiLoadSDP(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   nvars,              /**< number of variables */
   SCIP_Real*            obj,                /**< objective function values of variables */
   SCIP_Real*            lb,                 /**< lower bounds of variables */
   SCIP_Real*            ub,                 /**< upper bounds of variables */
   SCIP_Bool*            isintegral,         /**< whether the variables are integral (or NULL) */
   int                   nsdpblocks,         /**< number of SDP-blocks */
   int*                  sdpblocksizes,      /**< sizes of the SDP-blocks (may be NULL if nsdpblocks = sdpconstnnonz = sdpnnonz = 0) */
   int*                  sdpnblockvars,      /**< number of variables in each SDP-block (may be NULL if nsdpblocks = sdpconstnnonz = sdpnnonz = 0) */
   int                   sdpconstnnonz,      /**< number of nonzero elements in the constant matrices of the SDP-blocks */
   int*                  sdpconstnblocknonz, /**< number of nonzeros for each variable in the constant matrix (size of sdpconst[row/col/val] */
   int**                 sdpconstrow,        /**< pointer to row-indices of constant matrix for each block (may be NULL if sdpconstnnonz = 0) */
   int**                 sdpconstcol,        /**< pointer to column-indices of constant matrix for each block (may be NULL if sdpconstnnonz = 0) */
   SCIP_Real**           sdpconstval,        /**< pointer to values of entries of constant matrix for each block (may be NULL if sdpconstnnonz = 0) */
   int                   sdpnnonz,           /**< number of nonzero elements in the SDP-constraint-matrices */
   int**                 sdpnblockvarnonz,   /**< sdpnblockvarnonz[i][j] = nonzeros of j-th variable in i-th block (length of row/col/val[i][j]) */
   int**                 sdpvar,             /**< sdpvar[b][j] = index of j-th variable in block b */
   int***                sdprow,             /**< sdprow[b][v][j] = row of j-th nonzero of variable v in block b */
   int***                sdpcol,             /**< sdprow[b][v][j] = column of j-th nonzero of variable v in block b */
   SCIP_Real***          sdpval,             /**< sdpval[i][j][k] = value of j-th nonzero of variable v in block b */
   int                   nlpcons,            /**< number of LP-constraints */
   SCIP_Real*            lplhs,              /**< left-hand sides of LP rows (may be NULL if nlpcons = 0) */
   SCIP_Real*            lprhs,              /**< right-hand sides of LP rows (may be NULL if nlpcons = 0) */
   int                   lpnnonz,            /**< number of nonzero elements in the LP-constraint-matrix */
   int*                  lpbeg,              /**< start index of each row in ind- and val-array, or NULL if nnonz == 0 */
   int*                  lpind,              /**< column indices of constraint matrix entries, or NULL if nnonz == 0 */
   SCIP_Real*            lpval,              /**< values of constraint matrix entries, or NULL if nnonz == 0 */
   SCIP_Bool             allfixedprimalray   /**< whether we should return a primal ray if the problem is infeasible if all variables are fixed */
   )
{
   int cnt = 0;
   int v;
   int b;
   int i;

   SCIPdebugMessage("Calling SCIPsdpiLoadSDP (%d) ...\n", sdpi->sdpid);

   assert( sdpi != NULL );
   assert( nvars >= 0 );
   assert( obj != NULL );
   assert( lb != NULL );
   assert( ub != NULL );

#ifdef SCIP_DEBUG
   if ( sdpconstnnonz > 0 || sdpnnonz > 0 || nsdpblocks > 0 )
   {
      assert( sdpblocksizes != NULL );
      assert( sdpnblockvars != NULL );
      assert( nsdpblocks > 0 );
      assert( sdpconstnblocknonz != NULL );
      assert( sdpnblockvarnonz != NULL );

      if ( sdpconstnnonz > 0 )
      {
         assert( sdpconstrow != NULL );
         assert( sdpconstcol != NULL );
         assert( sdpconstval != NULL );

         for (i = 0; i < nsdpblocks; i++)
         {
            if ( sdpconstnblocknonz[i] > 0 )
            {
               assert( sdpconstrow[i] != NULL );
               assert( sdpconstcol[i] != NULL );
               assert( sdpconstval[i] != NULL );
            }
         }
      }

      if ( sdpnnonz > 0 )
      {
         assert( sdprow != NULL );
         assert( sdpcol != NULL );
         assert( sdpval != NULL );

         for (i = 0; i < nsdpblocks; i++)
         {
            assert( sdpcol[i] != NULL );
            assert( sdprow[i] != NULL );
            assert( sdpval[i] != NULL );

            for (v = 0; v < sdpnblockvars[i]; v++)
            {
               if ( sdpnblockvarnonz[i][v] > 0 )
               {
                  assert( sdpcol[i][v] != NULL );
                  assert( sdprow[i][v] != NULL );
                  assert( sdpval[i][v] != NULL );
               }
            }
         }
      }
   }
   for (i = 0; i < nvars; ++i)
   {
      assert( lb[i] < SCIPsdpiInfinity(sdpi) );   /* lower bound should not be infinity */
      assert( ub[i] > -SCIPsdpiInfinity(sdpi) );  /* upper bound should not be - infinity */
   }
#endif

   assert( nlpcons == 0 || lplhs != NULL );
   assert( nlpcons == 0 || lprhs != NULL );
   assert( nlpcons == 0 || lpbeg != NULL );
   assert( lpnnonz == 0 || lpind != NULL );
   assert( lpnnonz == 0 || lpval != NULL );

   /* ensure memory */
   SCIP_CALL( ensureBoundDataMemory(sdpi, nvars) );
   SCIP_CALL( ensureLPDataMemory(sdpi, nlpcons, lpnnonz) );
   SCIP_CALL( ensureSDPDataMemory(sdpi, nsdpblocks, sdpblocksizes, sdpnblockvars, sdpnblockvarnonz, sdpconstnblocknonz, sdpnnonz, allfixedprimalray) );

   /* copy data in arrays */
   BMScopyMemoryArray(sdpi->obj, obj, nvars);
   BMScopyMemoryArray(sdpi->lb, lb, nvars);
   BMScopyMemoryArray(sdpi->ub, ub, nvars);
   BMScopyMemoryArray(sdpi->sdpblocksizes, sdpblocksizes, nsdpblocks);
   BMScopyMemoryArray(sdpi->sdpnblockvars, sdpnblockvars, nsdpblocks);
   BMScopyMemoryArray(sdpi->sdpconstnblocknonz, sdpconstnblocknonz, nsdpblocks);

   if ( isintegral != NULL )
      BMScopyMemoryArray(sdpi->isintegral, isintegral, nvars);
   else
   {
      for (i = 0; i < nvars; ++i)
         sdpi->isintegral[i] = FALSE;
   }

   for (b = 0; b < nsdpblocks; ++b)
   {
#ifndef NDEBUG
      /* make sure that we have a lower triangular matrix */
      for (v = 0; v < sdpi->sdpconstnblocknonz[b]; ++v)
         assert( sdpconstrow[b][v] >= sdpconstcol[b][v] );
#endif

      BMScopyMemoryArray(sdpi->sdpnblockvarnonz[b], sdpnblockvarnonz[b], sdpnblockvars[b]);
      BMScopyMemoryArray(sdpi->sdpvar[b], sdpvar[b], sdpnblockvars[b]);

      if ( sdpconstnblocknonz[b] > 0 )
      {
         BMScopyMemoryArray(sdpi->sdpconstval[b], sdpconstval[b], sdpconstnblocknonz[b]);
         BMScopyMemoryArray(sdpi->sdpconstcol[b], sdpconstcol[b], sdpconstnblocknonz[b]);
         BMScopyMemoryArray(sdpi->sdpconstrow[b], sdpconstrow[b], sdpconstnblocknonz[b]);
      }

      assert( 0 <= sdpnblockvars[b] && sdpnblockvars[b] <= nvars );
      for (v = 0; v < sdpi->sdpnblockvars[b]; v++)
      {
#ifndef NDEBUG
         int j;

         /* make sure that we have a lower triangular matrix */
         for (j = 0; j < sdpi->sdpnblockvarnonz[b][v]; ++j)
            assert( sdprow[b][v][j] >= sdpcol[b][v][j] );
#endif
         assert( 0 <= sdpvar[b][v] && sdpvar[b][v] < nvars );

         assert( sdpi->sdpvalstore != NULL );
         assert( sdpi->sdpcolstore != NULL );
         assert( sdpi->sdprowstore != NULL );

         BMScopyMemoryArray(&sdpi->sdpvalstore[cnt], sdpval[b][v], sdpnblockvarnonz[b][v]);
         BMScopyMemoryArray(&sdpi->sdpcolstore[cnt], sdpcol[b][v], sdpnblockvarnonz[b][v]);
         BMScopyMemoryArray(&sdpi->sdprowstore[cnt], sdprow[b][v], sdpnblockvarnonz[b][v]);
         cnt += sdpnblockvarnonz[b][v];
         assert( cnt <= sdpnnonz );
      }
   }

   if ( nlpcons > 0 )
   {
      BMScopyMemoryArray(sdpi->lplhs, lplhs, nlpcons);
      BMScopyMemoryArray(sdpi->lprhs, lprhs, nlpcons);
      BMScopyMemoryArray(sdpi->lpbeg, lpbeg, nlpcons);
      BMScopyMemoryArray(sdpi->lpind, lpind, lpnnonz);
      BMScopyMemoryArray(sdpi->lpval, lpval, lpnnonz);
   }

   /* set the general information */
   sdpi->nvars = nvars;
   sdpi->nsdpblocks = nsdpblocks;

   sdpi->sdpconstnnonz = sdpconstnnonz;
   sdpi->sdpnnonz = sdpnnonz;

   /* LP part */
   sdpi->lpnnonz = lpnnonz;
   sdpi->nlpcons = nlpcons;
   sdpi->nactivelpcons = -1;

   sdpi->solved = FALSE;
   sdpi->infeasible = FALSE;
   sdpi->allfixed = FALSE;
   sdpi->nsdpcalls = 0;
   sdpi->niterations = 0;
   sdpi->opttime = 0.0;

   return SCIP_OKAY;
}

/** adds rows to the LP-Block
 *
 *  @note Arrays are not checked for duplicates, problems may appear if indices are added more than once.
 */
SCIP_RETCODE SCIPsdpiAddLPRows(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   nrows,              /**< number of rows to be added */
   const SCIP_Real*      lhs,                /**< left-hand sides of new rows */
   const SCIP_Real*      rhs,                /**< right-hand sides of new rows */
   int                   nnonz,              /**< number of nonzero elements to be added to the LP constraint matrix */
   const int*            beg,                /**< start index of each row in ind- and val-array, or NULL if nnonz == 0 */
   const int*            ind,                /**< column indices of constraint matrix entries, or NULL if nnonz == 0 */
   const SCIP_Real*      val                 /**< values of constraint matrix entries, or NULL if nnonz == 0 */
   )
{
   int i;

   SCIPdebugMessage("Adding %d LP-Constraints to SDP %d.\n", nrows, sdpi->sdpid);

   assert( sdpi != NULL );

   if ( nrows == 0 )
      return SCIP_OKAY; /* nothing to do in this case */

   assert( lhs != NULL );
   assert( rhs != NULL );
   assert( nnonz >= 0 );
   assert( nnonz == 0 || beg != NULL );
   assert( nnonz == 0 || ind != NULL );
   assert( nnonz == 0 || val != NULL );

   /* speed up things if LP part is emtpy */
   if ( sdpi->nlpcons == 0 )
   {
      assert( sdpi->lpnnonz == 0 );

      SCIP_CALL( ensureLPDataMemory(sdpi, nrows, nnonz) );
      BMScopyMemoryArray(sdpi->lplhs, lhs, nrows);
      BMScopyMemoryArray(sdpi->lprhs, rhs, nrows);
      BMScopyMemoryArray(sdpi->lpbeg, beg, nrows);
      BMScopyMemoryArray(sdpi->lpind, ind, nnonz);
      BMScopyMemoryArray(sdpi->lpval, val, nnonz);
      sdpi->nlpcons = nrows;
      sdpi->lpnnonz = nnonz;
      sdpi->nactivelpcons = -1;

#ifndef NDEBUG
      for (i = 0; i < nnonz; i++)
      {
         assert( val[i] != 0.0 );
         assert( 0 <= ind[i] && ind[i] < sdpi->nvars );
      }
#endif
   }
   else
   {
      SCIP_CALL( ensureLPDataMemory(sdpi, sdpi->nlpcons + nrows, sdpi->lpnnonz + nnonz) );
      BMScopyMemoryArray(&(sdpi->lplhs[sdpi->nlpcons]), lhs, nrows);
      BMScopyMemoryArray(&(sdpi->lprhs[sdpi->nlpcons]), rhs, nrows);
      BMScopyMemoryArray(&(sdpi->lpbeg[sdpi->nlpcons]), beg, nrows);

      for (i = 0; i < nnonz; i++)
      {
         assert( 0 <= ind[i] && ind[i] < sdpi->nvars ); /* only existing vars should be added to the LP-constraints */
         sdpi->lpind[sdpi->lpnnonz + i] = ind[i]; /*lint !e679*/
         sdpi->lpval[sdpi->lpnnonz + i] = val[i]; /*lint !e679*/
      }

      sdpi->nlpcons = sdpi->nlpcons + nrows;
      sdpi->lpnnonz = sdpi->lpnnonz + nnonz;
      sdpi->nactivelpcons = -1;
   }

   sdpi->solved = FALSE;
   sdpi->infeasible = FALSE;
   sdpi->nsdpcalls = 0;
   sdpi->niterations = 0;
   sdpi->opttime = 0.0;

   return SCIP_OKAY;
}

/** deletes all rows in the given range from the LP-Block */
SCIP_RETCODE SCIPsdpiDelLPRows(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   firstrow,           /**< first row to be deleted */
   int                   lastrow             /**< last row to be deleted */
   )
{
   int i;
   int deletedrows;
   int deletednonz;
   int nextbeg;

   SCIPdebugMessage("Deleting rows %d to %d from SDP %d.\n", firstrow, lastrow, sdpi->sdpid);

   assert( sdpi != NULL );
   assert( firstrow >= 0 );
   assert( firstrow <= lastrow );
   assert( lastrow < sdpi->nlpcons );

   /* shorten the procedure if the whole LP-part is to be deleted */
   if (firstrow == 0 && lastrow == sdpi->nlpcons - 1)
   {
      sdpi->nlpcons = 0;
      sdpi->lpnnonz = 0;
      sdpi->nactivelpcons = -1;

      sdpi->solved = FALSE;
      sdpi->infeasible = FALSE;
      sdpi->allfixed = FALSE;
      sdpi->nsdpcalls = 0;
      sdpi->niterations = 0;
      sdpi->opttime = 0.0;

      return SCIP_OKAY;
   }

   deletedrows = lastrow - firstrow + 1; /*lint !e834*/

   /* first delete the left- and right-hand-sides */
   for (i = lastrow + 1; i < sdpi->nlpcons; i++) /* shift all rhs after the deleted rows */
   {
      sdpi->lplhs[i - deletedrows] = sdpi->lplhs[i]; /*lint !e679*/
      sdpi->lprhs[i - deletedrows] = sdpi->lprhs[i]; /*lint !e679*/
      sdpi->lpbeg[i - deletedrows] = sdpi->lpbeg[i]; /*lint !e679*/
   }

   if ( lastrow == sdpi->nlpcons )
      nextbeg = sdpi->lpnnonz;
   else
      nextbeg = sdpi->lpbeg[lastrow + 1];

   deletednonz = nextbeg - sdpi->lpbeg[firstrow] + 1;

   for (i = nextbeg; i < sdpi->lpnnonz; ++i)
   {
      sdpi->lpind[i - deletednonz] = sdpi->lpind[i]; /*lint !e679*/
   }

   sdpi->nlpcons = sdpi->nlpcons - deletedrows;
   sdpi->lpnnonz = sdpi->lpnnonz - deletednonz;
   sdpi->nactivelpcons = -1;

   sdpi->solved = FALSE;
   sdpi->infeasible = FALSE;
   sdpi->allfixed = FALSE;
   sdpi->nsdpcalls = 0;
   sdpi->niterations = 0;
   sdpi->opttime = 0.0;

   return SCIP_OKAY;
}

/** deletes LP-rows from SDP-interface */
SCIP_RETCODE SCIPsdpiDelLPRowset(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  dstat               /**< deletion status of LP rows <br>
                                              *   input:  1 if row should be deleted, 0 otherwise <br>
                                              *   output: new position of row, -1 if row was deleted */
   )
{
   int i;
   int oldnlpcons;
   int deletedrows;

   SCIPdebugMessage("Calling SCIPsdpiDelLPRowset for SDP %d.\n", sdpi->sdpid);

   assert( sdpi != NULL );
   assert( dstat != NULL );

   oldnlpcons = sdpi->nlpcons;
   deletedrows = 0;

   for (i = 0; i < oldnlpcons; i++)
   {
      if ( dstat[i] == 1 )
      {
         /* delete this row, it is shifted by - deletedrows, because in this problem the earlier rows have already been deleted */
         SCIP_CALL( SCIPsdpiDelLPRows(sdpi, i - deletedrows, i - deletedrows) );
         dstat[i] = -1;
         deletedrows++;
      }
      else
         dstat[i] = i - deletedrows;
   }

   sdpi->solved = FALSE;
   sdpi->infeasible = FALSE;
   sdpi->allfixed = FALSE;
   sdpi->nsdpcalls = 0;
   sdpi->niterations = 0;
   sdpi->opttime = 0.0;

   return SCIP_OKAY;
}

/** clears the whole SDP */
SCIP_RETCODE SCIPsdpiClear(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   int b;
   int v;

   assert( sdpi != NULL );

   SCIPdebugMessage("SCIPsdpiClear in SDP %d.\n", sdpi->sdpid);

   /* reset all counters */
   sdpi->nlpcons = 0;
   sdpi->lpnnonz = 0;
   sdpi->nactivelpcons = -1;

   for (b = 0; b < sdpi->nsdpblocks; ++b)
   {
      for (v = 0; v < sdpi->sdpnblockvars[b]; ++v)
         sdpi->sdpnblockvarnonz[b][v] = 0;
      sdpi->sdpnblockvars[b] = 0;
      sdpi->sdpconstnblocknonz[b] = 0;
      sdpi->sdpblocksizes[b] = 0;
   }
   sdpi->sdpconstnnonz = 0;
   sdpi->sdpnnonz = 0;

   sdpi->nsdpblocks = 0;
   sdpi->nvars = 0;
   sdpi->sdpid = 1;
   SCIP_CALL( SCIPsdpiSolverResetCounter(sdpi->sdpisolver) );

   return SCIP_OKAY;
}

/** changes objective coefficients of variables */
SCIP_RETCODE SCIPsdpiChgObj(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   nvars,              /**< number of variables to change objective coefficients for */
   const int*            ind,                /**< variables indices */
   const SCIP_Real*      obj                 /**< new objective coefficients */
   )
{
   int i;

   SCIPdebugMessage("Changing %d objective coefficients in SDP %d\n", nvars, sdpi->sdpid);

   assert( sdpi != NULL );
   assert( ind != NULL );
   assert( obj != NULL );

   for (i = 0; i < nvars; i++)
   {
      assert( 0 <= ind[i] && ind[i] < sdpi->nvars );
      sdpi->obj[ind[i]] = obj[i];
   }

   sdpi->solved = FALSE;
   sdpi->nsdpcalls = 0;
   sdpi->niterations = 0;
   sdpi->opttime = 0.0;

   return SCIP_OKAY;
}

/** changes lower and upper bounds of variables */
SCIP_RETCODE SCIPsdpiChgBounds(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   nvars,              /**< number of variables to change bounds for */
   const int*            ind,                /**< variables indices */
   const SCIP_Real*      lb,                 /**< values for the new lower bounds */
   const SCIP_Real*      ub                  /**< values for the new upper bounds */
   )
{
   int i;

   SCIPdebugMessage("Changing %d variable bounds in SDP %d\n", nvars, sdpi->sdpid);

   assert( sdpi != NULL );
   assert( ind != NULL );
   assert( lb != NULL );
   assert( ub != NULL );

   for (i = 0; i < nvars; i++)
   {
      assert( 0 <= ind[i] && ind[i] < sdpi->nvars );
      sdpi->lb[ind[i]] = lb[i];
      sdpi->ub[ind[i]] = ub[i];
   }

   sdpi->solved = FALSE;
   sdpi->infeasible = FALSE;
   sdpi->allfixed = FALSE;
   sdpi->nsdpcalls = 0;
   sdpi->niterations = 0;
   sdpi->opttime = 0.0;

   return SCIP_OKAY;
}

/** changes left- and right-hand sides of LP rows */
SCIP_RETCODE SCIPsdpiChgLPLhRhSides(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   nrows,              /**< number of LP rows to change right hand sides for */
   const int*            ind,                /**< row indices between 1 and nlpcons */
   const SCIP_Real*      lhs,                /**< new values for left-hand sides */
   const SCIP_Real*      rhs                 /**< new values for right-hand sides */
   )
{
   int i;

   SCIPdebugMessage("Changing %d left and right hand sides of SDP %d\n", nrows, sdpi->sdpid);

   assert( sdpi != NULL );
   assert( 0 <= nrows && nrows <= sdpi->nlpcons );
   assert( ind != NULL );
   assert( lhs != NULL );
   assert( rhs != NULL );

   for (i = 0; i < nrows; i++)
   {
      assert( ind[i] >= 0 );
      assert( ind[i] < sdpi->nlpcons );
      sdpi->lplhs[ind[i]] = lhs[i];
      sdpi->lprhs[ind[i]] = rhs[i];
   }

   sdpi->solved = FALSE;
   sdpi->infeasible = FALSE;
   sdpi->allfixed = FALSE;
   sdpi->nsdpcalls = 0;
   sdpi->niterations = 0;
   sdpi->opttime = 0.0;

   return SCIP_OKAY;
}
/**@} */


/*
 * Data Accessing Methods
 */

/**@name Data Accessing Methods */
/**@{ */

/** returns the currently installed sdpi message handler, or NULL if messages are currently suppressed */
SCIP_MESSAGEHDLR* SCIPsdpiGetMessagehdlr(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   return sdpi->messagehdlr;
}

/** gets the number of LP-rows in the SDP */
SCIP_RETCODE SCIPsdpiGetNLPRows(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  nlprows             /**< pointer to store the number of rows */
   )
{
   assert( sdpi != NULL );
   assert( nlprows != NULL );

   *nlprows = sdpi->nlpcons;

   return SCIP_OKAY;
}

/** gets the number of SDP-Blocks in the SDP */
SCIP_RETCODE SCIPsdpiGetNSDPBlocks(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  nsdpblocks          /**< pointer to store the number of blocks */
   )
{
   assert( sdpi != NULL );
   assert( nsdpblocks != NULL );

   *nsdpblocks = sdpi->nsdpblocks;

   return SCIP_OKAY;
}

/** gets the number of variables in the SDP */
SCIP_RETCODE SCIPsdpiGetNVars(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  nvars               /**< pointer to store the number of variables */
   )
{
   assert( sdpi != NULL );
   assert( nvars != NULL );

   *nvars = sdpi->nvars;

   return SCIP_OKAY;
}

/** gets the number of nonzero elements in the SDP-constraint-matrices */
SCIP_RETCODE SCIPsdpiGetSDPNNonz(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  nnonz               /**< pointer to store the number of nonzeros in the SDP-constraint-matrices */
   )
{
   assert( sdpi != NULL );
   assert( nnonz != NULL );

   *nnonz = sdpi->sdpnnonz;

   return SCIP_OKAY;
}

/** gets the number of nonzero elements in the constant matrices of the SDP-Blocks */
SCIP_RETCODE SCIPsdpiGetConstNNonz(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  nnonz               /**< pointer to store the number of nonzeros in the constant matrices of the SDP-Blocks */
   )
{
   assert( sdpi != NULL );
   assert( nnonz != NULL );

   *nnonz = sdpi->sdpconstnnonz;

   return SCIP_OKAY;
}

/** gets the number of nonzero elements in the LP-Matrix */
SCIP_RETCODE SCIPsdpiGetLPNNonz(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  nnonz               /**< pointer to store the number of nonzeros in the LP Matrix */
   )
{
   assert( sdpi != NULL );
   assert( nnonz != NULL );

   *nnonz = sdpi->lpnnonz;

   return SCIP_OKAY;
}

/** gets SDP data from SDP-interface */
SCIP_RETCODE SCIPsdpiGetSDPdata(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int**                 sdpblocksizes,      /**< sizes of the SDP-blocks */
   int**                 sdpnblockvars,      /**< number of variables in each SDP-block */
   int***                sdpnblockvarnonz,   /**< sdpnblockvarnonz[i][j] = nonzeros of j-th variable in i-th block (length of row/col/val[i][j]) */
   int***                sdpvar,             /**< sdpvar[b][j] = sdp-index of j-th variable in block b */
   int****               sdprow,             /**< sdprow[b][v][j] = row of j-th nonzero of variable v in block b */
   int****               sdpcol,             /**< sdprow[b][v][j] = column of j-th nonzero of variable v in block b */
   SCIP_Real****         sdpval,             /**< sdpval[i][j][k] = value of j-th nonzero of variable v in block b */
   int**                 sdpconstnblocknonz, /**< number of nonzeros for each variable in the constant matrix (size of sdpconst[row/col/val] */
   int***                sdpconstrow,        /**< pointers to row-indices for each block */
   int***                sdpconstcol,        /**< pointers to column-indices for each block */
   SCIP_Real***          sdpconstval         /**< pointers to the values of the nonzeros for each block */
   )
{
   assert( sdpi != NULL );
   assert( sdpblocksizes != NULL );
   assert( sdpnblockvars != NULL );
   assert( sdpnblockvarnonz != NULL );
   assert( sdpvar != NULL );
   assert( sdprow != NULL );
   assert( sdpcol != NULL );
   assert( sdpval != NULL );
   assert( sdpconstnblocknonz != NULL );
   assert( sdpconstrow != NULL );
   assert( sdpconstcol != NULL );
   assert( sdpconstval != NULL );

   *sdpblocksizes = sdpi->sdpblocksizes;
   *sdpnblockvars = sdpi->sdpnblockvars;
   *sdpnblockvarnonz = sdpi->sdpnblockvarnonz;
   *sdpvar = sdpi->sdpvar;
   *sdprow = sdpi->sdprow;
   *sdpcol = sdpi->sdpcol;
   *sdpval = sdpi->sdpval;
   *sdpconstnblocknonz = sdpi->sdpconstnblocknonz;
   *sdpconstrow = sdpi->sdpconstrow;
   *sdpconstcol = sdpi->sdpconstcol;
   *sdpconstval = sdpi->sdpconstval;

   return SCIP_OKAY;
}

/** gets objective coefficients from SDP-interface */
SCIP_RETCODE SCIPsdpiGetObj(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   firstvar,           /**< first variable to get objective coefficient for */
   int                   lastvar,            /**< last variable to get objective coefficient for */
   SCIP_Real*            vals                /**< pointer to store objective coefficients (memory of size lastvar - firstvar + 1 needs to be allocated) */
   )
{
   int i;

   assert( sdpi != NULL );
   assert( firstvar >= 0 );
   assert( firstvar <= lastvar );
   assert( lastvar < sdpi->nvars);
   assert( vals != NULL );

   for (i = 0; i < lastvar - firstvar + 1; i++) /*lint !e834*/
      vals[i] = sdpi->obj[firstvar + i]; /*lint !e679*/

   return SCIP_OKAY;
}

/** gets current variable lower and/or upper bounds from SDP-interface */
SCIP_RETCODE SCIPsdpiGetBounds(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   firstvar,           /**< first variable to get bounds for */
   int                   lastvar,            /**< last variable to get bounds for */
   SCIP_Real*            lbs,                /**< pointer to store lower bound values (memory of size lastvar - firstvar + 1 needs to be allocated), or NULL */
   SCIP_Real*            ubs                 /**< pointer to store upper bound values (memory of size lastvar - firstvar + 1 needs to be allocated), or NULL */
   )
{
   int i;

   assert( sdpi != NULL );
   assert( firstvar >= 0 );
   assert( firstvar <= lastvar );
   assert( lastvar < sdpi->nvars);
   assert( lbs != NULL );
   assert( ubs != NULL );

   for (i = 0; i < lastvar - firstvar + 1; i++) /*lint !e834*/
   {
      if (lbs != NULL)
         lbs[i] = sdpi->lb[firstvar + i]; /*lint !e679*/
      if (ubs != NULL)
         ubs[i] = sdpi->ub[firstvar + i]; /*lint !e679*/
   }

   return SCIP_OKAY;
}

/** gets current left-hand sides from SDP-interface */
SCIP_RETCODE SCIPsdpiGetLhSides(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   firstrow,           /**< first row to get sides for */
   int                   lastrow,            /**< last row to get sides for */
   SCIP_Real*            lhss                /**< pointer to store left-hand side values (memory of size lastvar - firstvar + 1 needs to be allocated) */
   )
{
   int i;

   assert( sdpi != NULL );
   assert( firstrow >= 0 );
   assert( firstrow <= lastrow );
   assert( lastrow < sdpi->nlpcons);
   assert( lhss != NULL );

   for (i = 0; i < lastrow - firstrow + 1; i++) /*lint !e834*/
      lhss[firstrow + i] = sdpi->lplhs[i]; /*lint !e679*/

   return SCIP_OKAY;
}

/** gets current right-hand sides from SDP-interface */
SCIP_RETCODE SCIPsdpiGetRhSides(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   firstrow,           /**< first row to get sides for */
   int                   lastrow,            /**< last row to get sides for */
   SCIP_Real*            rhss                /**< pointer to store right-hand side values (memory of size lastvar - firstvar + 1 needs to be allocated) */
   )
{
   int i;

   assert( sdpi != NULL );
   assert( firstrow >= 0 );
   assert( firstrow <= lastrow );
   assert( lastrow < sdpi->nlpcons);
   assert( rhss != NULL );

   for (i = 0; i < lastrow - firstrow + 1; i++) /*lint !e834*/
      rhss[firstrow + i] = sdpi->lprhs[i]; /*lint !e679*/

   return SCIP_OKAY;
}


/**@} */



/*
 * Solving Methods
 */

/**@name Solving Methods */
/**@{ */

/** solves the SDP, as start optionally a starting point for the solver may be given, if it is NULL, the solver will start from scratch
 *
 *  @note starting point needs to be given with original indices (before any local presolving), last block should be the LP block with indices
 *  lhs(row0), rhs(row0), lhs(row1), ..., lb(var1), ub(var1), lb(var2), ... independent of some lhs/rhs being infinity (the starting point
 *  will later be adjusted accordingly).
 */
SCIP_RETCODE SCIPsdpiSolve(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Real*            starty,             /**< NULL or dual vector y as starting point for the solver, this should have length nvars */
   int*                  startZnblocknonz,   /**< dual matrix Z = sum Ai yi as starting point for the solver: number of nonzeros for each block,
                                              *   also length of corresponding row/col/val-arrays; or NULL */
   int**                 startZrow,          /**< dual matrix Z = sum Ai yi as starting point for the solver: row indices for each block;
                                              *   may be NULL if startZnblocknonz = NULL */
   int**                 startZcol,          /**< dual matrix Z = sum Ai yi as starting point for the solver: column indices for each block;
                                              *   may be NULL if startZnblocknonz = NULL */
   SCIP_Real**           startZval,          /**< dual matrix Z = sum Ai yi as starting point for the solver: values for each block;
                                              *   may be NULL if startZnblocknonz = NULL */
   int*                  startXnblocknonz,   /**< primal matrix X as starting point for the solver: number of nonzeros for each block,
                                              *   also length of corresponding row/col/val-arrays; or NULL */
   int**                 startXrow,          /**< primal matrix X as starting point for the solver: row indices for each block;
                                              *   may be NULL if startXnblocknonz = NULL */
   int**                 startXcol,          /**< primal matrix X as starting point for the solver: column indices for each block;
                                              *   may be NULL if startXnblocknonz = NULL */
   SCIP_Real**           startXval,          /**< primal matrix X as starting point for the solver: values for each block;
                                              *   may be NULL if startXnblocknonz = NULL */
   SCIP_SDPSOLVERSETTING startsettings,      /**< settings used to start with in SDPA, currently not used for DSDP or MOSEK, set this to
                                              *   SCIP_SDPSOLVERSETTING_UNSOLVED to ignore it and start from scratch */
   SCIP_Bool             enforceslatercheck, /**< always check for Slater condition in case the problem could not be solved and printf the solution
                                              *   of this check */
   SCIP_Real             timelimit           /**< after this many seconds solving will be aborted (currently only implemented for DSDP and MOSEK) */
   )
{
   int* sdpconstnblocknonz = NULL;
   int* maxsdpconstnblocknonz = NULL;
   int** sdpconstrow = NULL;
   int** sdpconstcol = NULL;
   SCIP_Real** sdpconstval = NULL;
   SCIP_Real addedopttime;
   SCIP_Real fixedvarsobjcontr = 0.0;
   SCIP_Bool fixingfound;
   int sdpconstnnonz;
   int sdpilpnnonz = 0;
   int nactivevars = 0;
   int activevaridx = -1;
   int naddediterations;
   int naddedsdpcalls;
   int b;
   int v;

   assert( sdpi != NULL );

   SCIPdebugMessage("Forwarding SDP %d to solver!\n", sdpi->sdpid);

   sdpi->penalty = FALSE;
   sdpi->bestbound = -SCIPsdpiSolverInfinity(sdpi->sdpisolver);
   sdpi->solved = FALSE;
   sdpi->infeasible = FALSE;
   sdpi->allfixed = FALSE;
   sdpi->nsdpcalls = 0;
   sdpi->niterations = 0;
   sdpi->opttime = 0.0;
   sdpi->solvedonevarsdp = SCIP_ONEVAR_UNSOLVED;
   sdpi->onevarsdpobjval = SCIP_INVALID;
   sdpi->onevarsdpoptval = SCIP_INVALID;
   sdpi->onevarsdpidx = -1;
   sdpi->onevarsdpcertval = SCIP_INVALID;

   if ( timelimit <= 0.0 )
      return SCIP_OKAY;

   SDPIclockStart(sdpi->usedsdpitime);

   /* copy bounds */
   for (v = 0; v < sdpi->nvars && ! sdpi->infeasible; v++)
   {
      sdpi->sdpilb[v] = sdpi->lb[v];
      sdpi->sdpiub[v] = sdpi->ub[v];
      if ( sdpi->sdpiub[v] < sdpi->sdpilb[v] - sdpi->feastol )
         sdpi->infeasible = TRUE;
   }

   /* exit if infeasible */
   if ( sdpi->infeasible )
   {
      SCIPdebugMessage("SDP %d not given to solver, as infeasibility was detected during problem preparation!\n", sdpi->sdpid++);
      SCIP_CALL( SCIPsdpiSolverIncreaseCounter(sdpi->sdpisolver) );

      sdpi->solved = TRUE;
      sdpi->dualslater = SCIP_SDPSLATER_NOINFO;
      sdpi->primalslater = SCIP_SDPSLATER_NOINFO;
      ++sdpi->ninfeasible;

      SDPIclockStop(sdpi->usedsdpitime);

      return SCIP_OKAY;
   }
   assert( ! sdpi->infeasible );

   /* Compute the lplphs and lprhs, detect empty rows and check for additional variable fixings caused by boundchanges from
    * lp rows with a single active variable. Note that this changes sdpi->sdpilb and sdpi->sdpiub, but not sdpi->lb and sdpi->ub. */
   do
   {
      /* we expect that additional fixings are only found seldomly, so this function is usually called only once per solve */
      SCIP_CALL( prepareLPData(sdpi, sdpi->sdpilb, sdpi->sdpiub, sdpi->sdpilbrowidx, sdpi->sdpiubrowidx, &sdpi->nactivelpcons, sdpi->sdpilpindchanges, sdpi->sdpilplhs, sdpi->sdpilprhs,
            &sdpilpnnonz, sdpi->sdpilpbeg, sdpi->sdpilpind, sdpi->sdpilpval, &fixingfound) );

      SCIPdebugMessage("Number of active LP constraints: %d (original: %d); %d nonzeros.\n", sdpi->nactivelpcons, sdpi->nlpcons, sdpilpnnonz);
   }
   while ( fixingfound && ! sdpi->infeasible );

   /* exit if infeasible */
   if ( sdpi->infeasible )
   {
      SCIPdebugMessage("SDP %d not given to solver, since infeasibility was detected during problem preparation!\n", sdpi->sdpid++);
      SCIP_CALL( SCIPsdpiSolverIncreaseCounter(sdpi->sdpisolver) );

      sdpi->solved = TRUE;
      sdpi->dualslater = SCIP_SDPSLATER_NOINFO;
      sdpi->primalslater = SCIP_SDPSLATER_NOINFO;
      ++sdpi->ninfeasible;

      SDPIclockStop(sdpi->usedsdpitime);

      return SCIP_OKAY;
   }
   assert( ! sdpi->infeasible );

   /* Checks whether all variables are fixed; this cannot be done in prepareLPData() because not all variables need to be contained in LP-constraints. */
   for (v = 0; v < sdpi->nvars; v++)
   {
      if ( ! isFixed(sdpi, v) )
      {
         ++nactivevars;
         activevaridx = v;
      }
      else
         fixedvarsobjcontr += sdpi->obj[v] * sdpi->sdpilb[v];
   }

   if ( nactivevars == 0 )
      sdpi->allfixed = TRUE;

   /* check if all variables are fixed, if this is the case, check if the remaining solution for feasibility */
   if ( sdpi->allfixed )
   {
      /* check feasibility of SDP constraints - LP constraints have been checked in prepareLPData() */
      SCIP_CALL( checkFixedFeasibilitySdp(sdpi, sdpi->sdpilb, sdpi->sdpiub) );

      SCIPdebugMessage("SDP %d not given to solver, since all variables are fixed; problem is %sfeasible!\n", sdpi->sdpid++, sdpi->infeasible ? "in" : "");

      SCIP_CALL( SCIPsdpiSolverIncreaseCounter(sdpi->sdpisolver) );
      sdpi->solved = TRUE;
      sdpi->dualslater = SCIP_SDPSLATER_NOINFO;
      sdpi->primalslater = SCIP_SDPSLATER_NOINFO;
      ++sdpi->nallfixed;
      SDPIclockStop(sdpi->usedsdpitime);

      return SCIP_OKAY;
   }
   assert( ! sdpi->allfixed );
   assert( ! sdpi->infeasible );

   /* allocate memory for computing the constant matrix after fixings and finding empty rows and columns */
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &sdpconstnblocknonz, sdpi->nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &maxsdpconstnblocknonz, sdpi->nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &sdpconstrow, sdpi->nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &sdpconstcol, sdpi->nsdpblocks) );
   BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &sdpconstval, sdpi->nsdpblocks) );

   for (b = 0; b < sdpi->nsdpblocks; ++b)
   {
      maxsdpconstnblocknonz[b] = MIN(sdpi->sdpnnonz + sdpi->sdpconstnnonz, sdpi->sdpblocksizes[b] * (sdpi->sdpblocksizes[b] + 1) / 2);
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpconstrow[b]), maxsdpconstnblocknonz[b]) ); /*lint !e776*/
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpconstcol[b]), maxsdpconstnblocknonz[b]) ); /*lint !e776*/
      BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpconstval[b]), maxsdpconstnblocknonz[b]) ); /*lint !e776*/
      sdpconstnblocknonz[b] = maxsdpconstnblocknonz[b];
   }

   /* compute constant matrix after fixings */
   SCIP_CALL( compConstMatAfterFixings(sdpi, sdpi->sdpilb, sdpi->sdpiub, &sdpconstnnonz, sdpconstnblocknonz, sdpconstrow, sdpconstcol, sdpconstval) );
   assert( ! sdpi->allfixed );
   assert( ! sdpi->infeasible );

   /* check whether problem contains one variable and one SDP block */
   if ( nactivevars == 1 && sdpi->nsdpblocks <= 1 )
   {
      SCIP_Real objval;
      SCIP_Real optval;

      assert( sdpi->nactivelpcons == 0 ); /* all LP constraints should have been converted to variable bounds in preprocessing */
      assert( 0 <= activevaridx && activevaridx < sdpi->nvars );

      /* treat LPs */
      if ( sdpi->nsdpblocks == 0 )
      {
         /* If there are no SDP constraints, we have an LP with one variable. Preprocessing above has reduced the
          * problem to a variable and its corresponding bounds and no further constraints. */
         if ( ! SCIPsdpiIsInfinity(sdpi, sdpi->sdpilb[activevaridx]) && ! SCIPsdpiIsInfinity(sdpi, sdpi->sdpiub[activevaridx]) )
         {
            if ( sdpi->obj[activevaridx] >= 0.0 )
            {
               sdpi->onevarsdpoptval = sdpi->sdpilb[activevaridx];
               sdpi->onevarsdpobjval = sdpi->obj[activevaridx] * sdpi->sdpilb[activevaridx];
            }
            else
            {
               sdpi->onevarsdpoptval = sdpi->sdpiub[activevaridx];
               sdpi->onevarsdpobjval = sdpi->obj[activevaridx] * sdpi->sdpiub[activevaridx];
            }

            sdpi->solved = TRUE;
            sdpi->dualslater = SCIP_SDPSLATER_NOINFO;
            sdpi->primalslater = SCIP_SDPSLATER_NOINFO;
            sdpi->onevarsdpidx = activevaridx;
            sdpi->solvedonevarsdp = SCIP_ONEVAR_OPTIMAL;
            sdpi->onevarsdpobjval += fixedvarsobjcontr;
            sdpi->onevarsdpcertval = SCIP_INVALID;
            ++sdpi->nonevarsdp;
         }
      }
      else
      {
         /* search for matrix index corresponding to active variable */
         for (v = 0; v < sdpi->sdpnblockvars[0]; ++v)
         {
            if ( sdpi->sdpvar[0][v] == activevaridx )
               break;
         }

         if ( sdpi->onevarsdpcertvec == NULL )
         {
            BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &(sdpi->onevarsdpcertvec), sdpi->sdpblocksizes[0]) );
            sdpi->onevarsdpcertsize = sdpi->sdpblocksizes[0];
         }
         else if ( sdpi->onevarsdpcertsize != sdpi->sdpblocksizes[0] )
         {
            BMS_CALL( BMSreallocBlockMemoryArray(sdpi->blkmem, &(sdpi->onevarsdpcertvec), sdpi->sdpblocksizes[0], sdpi->onevarsdpcertsize) );
            sdpi->onevarsdpcertsize = sdpi->sdpblocksizes[0];
         }

         SCIP_CALL( SCIPsolveOneVarSDP(sdpi->bufmem, sdpi->obj[activevaridx], sdpi->sdpilb[activevaridx], sdpi->sdpiub[activevaridx], sdpi->sdpblocksizes[0],
               sdpconstnblocknonz[0], sdpconstrow[0], sdpconstcol[0], sdpconstval[0],
               sdpi->sdpnblockvarnonz[0][v], sdpi->sdprow[0][v], sdpi->sdpcol[0][v], sdpi->sdpval[0][v],
               SCIPsdpiInfinity(sdpi), sdpi->feastol, sdpi->onevarsdpcertvec, &sdpi->onevarsdpcertval, &objval, &optval) );

         if ( objval != SCIP_INVALID )  /*lint !e777*/
         {
            sdpi->solved = TRUE;
            sdpi->dualslater = SCIP_SDPSLATER_NOINFO;
            sdpi->primalslater = SCIP_SDPSLATER_NOINFO;
            sdpi->onevarsdpobjval = objval;
            sdpi->onevarsdpoptval = optval;
            sdpi->onevarsdpidx = activevaridx;

            if ( SCIPsdpiIsInfinity(sdpi, objval) )
               sdpi->solvedonevarsdp = SCIP_ONEVAR_INFEASIBLE;
            else
            {
               sdpi->solvedonevarsdp = SCIP_ONEVAR_OPTIMAL;
               sdpi->onevarsdpobjval += fixedvarsobjcontr;
            }
            ++sdpi->nonevarsdp;
         }
      }
   }

   /* solve SDP if not yet done */
   if ( ! sdpi->solved )
   {
      /* remove empty rows and columns */
      SCIP_CALL( findEmptyRowColsSDP(sdpi, sdpconstnblocknonz, sdpconstrow, sdpconstcol, sdpconstval) );

      if ( sdpi->slatercheck )
      {
         SCIP_CALL( checkSlaterCondition(sdpi, timelimit, sdpi->sdpilb, sdpi->sdpiub,
               sdpconstnblocknonz, sdpconstnnonz, sdpconstrow, sdpconstcol, sdpconstval,
               sdpi->indchanges, sdpi->nremovedinds, sdpi->nlpcons, sdpi->sdpilpindchanges,
               sdpi->sdpilplhs, sdpi->sdpilprhs, sdpilpnnonz, sdpi->sdpilpbeg, sdpi->sdpilpind, sdpi->sdpilpval,
               sdpi->blockindchanges, sdpi->nremovedblocks, FALSE) );
      }

      /* try to solve the problem */
      SCIP_CALL( SCIPsdpiSolverLoadAndSolve(sdpi->sdpisolver, sdpi->nvars, sdpi->obj, sdpi->sdpilb, sdpi->sdpiub,
            sdpi->nsdpblocks, sdpi->sdpblocksizes, sdpi->sdpnblockvars, sdpconstnnonz,
            sdpconstnblocknonz, sdpconstrow, sdpconstcol, sdpconstval,
            sdpi->sdpnnonz, sdpi->sdpnblockvarnonz, sdpi->sdpvar, sdpi->sdprow, sdpi->sdpcol,
            sdpi->sdpval, sdpi->indchanges, sdpi->nremovedinds, sdpi->blockindchanges, sdpi->nremovedblocks, sdpi->nlpcons, sdpi->sdpilpindchanges, sdpi->sdpilplhs, sdpi->sdpilprhs,
            sdpilpnnonz, sdpi->sdpilpbeg, sdpi->sdpilpind, sdpi->sdpilpval, starty, startZnblocknonz, startZrow, startZcol, startZval,
            startXnblocknonz, startXrow, startXcol, startXval, startsettings, timelimit, sdpi->usedsdpitime) );

      sdpi->solved = TRUE;

      /* add time, iterations and sdpcalls */
      addedopttime = 0.0;
      SCIP_CALL( SCIPsdpiSolverGetTime(sdpi->sdpisolver, &addedopttime) );
      sdpi->opttime += addedopttime;
      naddediterations = 0;
      SCIP_CALL( SCIPsdpiSolverGetIterations(sdpi->sdpisolver, &naddediterations) );
      sdpi->niterations += naddediterations;
      naddedsdpcalls = 0;
      SCIP_CALL( SCIPsdpiSolverGetSdpCalls(sdpi->sdpisolver, &naddedsdpcalls) );
      sdpi->nsdpcalls += naddedsdpcalls;

#if 0
      if ( SCIPsdpiSolverIsAcceptable(sdpi->sdpisolver) && SCIPsdpiSolverWasSolved(sdpi->sdpisolver) && solveonevarsdpobjval != SCIP_INVALID )
      {
         SCIP_Real objval;
         SCIP_Real* dualsol;

         BMS_CALL( BMSallocBlockMemoryArray(sdpi->blkmem, &dualsol, sdpi->nvars) );
         SCIP_CALL( SCIPsdpiGetSol(sdpi, &objval, dualsol) );
         printf("dual sol: %.15g\n", dualsol[activevaridx]);
         BMSfreeBlockMemoryArrayNull(sdpi->blkmem, &dualsol, sdpi->nvars);

         /* SCIP_CALL( SCIPsdpiSolverGetObjval(sdpi->sdpisolver, &objval) ); */
         assert( REALABS(objval - solveonevarsdpobjval)/MAX3(1.0, REALABS(objval), REALABS(solveonevarsdpobjval)) <= 2.0 * sdpi->gaptol );
      }
#endif

      /* if the solver didn't produce a satisfactory result, we have to try with a penalty formulation */
      if ( ! SCIPsdpiSolverIsAcceptable(sdpi->sdpisolver) && ! SCIPsdpiSolverIsTimelimExc(sdpi->sdpisolver) )
      {
         SCIP_Real penaltyparam;
         SCIP_Real penaltyparamfact;
         SCIP_Real gaptol;
         SCIP_Real gaptolfact;
         SCIP_Bool feasorig = FALSE;
         SCIP_Real objbound;
         SCIP_Real objval;

         /* first check feasibility using the penalty approach */
         SCIPdebugMessage("SDP %d returned inacceptable result, trying penalty formulation.\n", sdpi->sdpid);

         /* We solve the problem with a slack variable times identity added to the constraints and trying to minimize this slack variable r, if
          * the optimal objective is bigger than feastol, then we know that the problem is infeasible; the original objective is set to 0. */
         SCIP_CALL( SCIPsdpiSolverLoadAndSolveWithPenalty(sdpi->sdpisolver, 1.0, FALSE, FALSE, sdpi->nvars, sdpi->obj, sdpi->sdpilb, sdpi->sdpiub,
               sdpi->nsdpblocks, sdpi->sdpblocksizes, sdpi->sdpnblockvars, sdpconstnnonz,
               sdpconstnblocknonz, sdpconstrow, sdpconstcol, sdpconstval,
               sdpi->sdpnnonz, sdpi->sdpnblockvarnonz, sdpi->sdpvar, sdpi->sdprow, sdpi->sdpcol,
               sdpi->sdpval, sdpi->indchanges, sdpi->nremovedinds, sdpi->blockindchanges, sdpi->nremovedblocks, sdpi->nlpcons, sdpi->sdpilpindchanges, sdpi->sdpilplhs, sdpi->sdpilprhs,
               sdpilpnnonz, sdpi->sdpilpbeg, sdpi->sdpilpind, sdpi->sdpilpval, starty, startZnblocknonz, startZrow, startZcol, startZval,
               startXnblocknonz, startXrow, startXcol, startXval, SCIP_SDPSOLVERSETTING_UNSOLVED, timelimit, sdpi->usedsdpitime, &feasorig, NULL) );

         /* add time, iterations and sdpcalls */
         addedopttime = 0.0;
         SCIP_CALL( SCIPsdpiSolverGetTime(sdpi->sdpisolver, &addedopttime) );
         sdpi->opttime += addedopttime;
         naddediterations = 0;
         SCIP_CALL( SCIPsdpiSolverGetIterations(sdpi->sdpisolver, &naddediterations) );
         sdpi->niterations += naddediterations;
         naddedsdpcalls = 0;
         SCIP_CALL( SCIPsdpiSolverGetSdpCalls(sdpi->sdpisolver, &naddedsdpcalls) );
         sdpi->nsdpcalls += naddedsdpcalls;

         /* get objective value */
         if ( SCIPsdpiSolverWasSolved(sdpi->sdpisolver) )
         {
            SCIP_CALL( SCIPsdpiSolverGetObjval(sdpi->sdpisolver, &objval) );
         }
         else
            objval = -SCIPsdpiInfinity(sdpi);

         /* If the penalty formulation was successfully solved and has a strictly positive objective value, we know that
          * the problem is infeasible. Note that we need to check against the maximum of feastol and gaptol, since this
          * is the objective of an SDP which is only exact up to gaptol, and cutting a feasible node off is an error
          * while continuing with an infeasible problem only takes additional time until we found out again later.
          */
         if ( ( SCIPsdpiSolverIsOptimal(sdpi->sdpisolver) && objval > sdpi->peninfeasadjust * MAX(sdpi->feastol, sdpi->gaptol) ) ||
              ( SCIPsdpiSolverWasSolved(sdpi->sdpisolver) && SCIPsdpiSolverIsDualInfeasible(sdpi->sdpisolver) ) )
         {
            SCIPdebugMessage("SDP %d found infeasible using penalty formulation, maximum of smallest eigenvalue is %g.\n", sdpi->sdpid, -objval);
            sdpi->penalty = TRUE;
            sdpi->infeasible = TRUE;
         }
         else
         {
            SCIP_Bool penaltybound = TRUE;

            feasorig = FALSE;

            penaltyparam = sdpi->penaltyparam;

            SCIPdebugMessage("SDP %d not found infeasible using penalty formulation, maximum of smallest eigenvalue is %g.\n", sdpi->sdpid, -1.0 * objval);

            /* we compute the factor to increase with as n-th root of the total increase until the maximum, where n is the number of iterations
             * (for npenaltyincr = 0 we make sure that the parameter is too large after the first change) */
            gaptol = sdpi->gaptol;
            if ( sdpi->npenaltyincr > 0 )
            {
               penaltyparamfact = pow((sdpi->maxpenaltyparam / sdpi->penaltyparam), 1.0 / sdpi->npenaltyincr);
               gaptolfact = pow((MIN_GAPTOL / sdpi->gaptol), 1.0 / sdpi->npenaltyincr);
            }
            else
            {
               penaltyparamfact = 2 * sdpi->maxpenaltyparam / sdpi->penaltyparam;
               gaptolfact = 0.5 * MIN_GAPTOL / sdpi->gaptol;
            }

            /* increase penalty-param and decrease feasibility tolerance until we find a feasible solution or reach the final bound for either one of them */
            while ( ( ! SCIPsdpiSolverIsAcceptable(sdpi->sdpisolver) || ! feasorig ) &&
                  ( penaltyparam < sdpi->maxpenaltyparam + sdpi->epsilon ) && ( gaptol > 0.99 * MIN_GAPTOL ) && ( ! SCIPsdpiSolverIsTimelimExc(sdpi->sdpisolver) ))
            {
               SCIPdebugMessage("Solver did not produce an acceptable result, trying SDP %d again with penaltyparameter %g.\n", sdpi->sdpid, penaltyparam);

               SCIP_CALL( SCIPsdpiSolverLoadAndSolveWithPenalty(sdpi->sdpisolver, penaltyparam, TRUE, TRUE, sdpi->nvars, sdpi->obj,
                     sdpi->sdpilb, sdpi->sdpiub, sdpi->nsdpblocks, sdpi->sdpblocksizes, sdpi->sdpnblockvars, sdpconstnnonz,
                     sdpconstnblocknonz, sdpconstrow, sdpconstcol, sdpconstval,
                     sdpi->sdpnnonz, sdpi->sdpnblockvarnonz, sdpi->sdpvar, sdpi->sdprow, sdpi->sdpcol,
                     sdpi->sdpval, sdpi->indchanges, sdpi->nremovedinds, sdpi->blockindchanges, sdpi->nremovedblocks,
                     sdpi->nlpcons, sdpi->sdpilpindchanges, sdpi->sdpilplhs, sdpi->sdpilprhs,
                     sdpilpnnonz, sdpi->sdpilpbeg, sdpi->sdpilpind, sdpi->sdpilpval, starty, startZnblocknonz, startZrow, startZcol, startZval,
                     startXnblocknonz, startXrow, startXcol, startXval, startsettings, timelimit, sdpi->usedsdpitime, &feasorig, &penaltybound) );

               /* add time, iterations and sdpcalls */
               addedopttime = 0.0;
               SCIP_CALL( SCIPsdpiSolverGetTime(sdpi->sdpisolver, &addedopttime) );
               sdpi->opttime += addedopttime;
               naddediterations = 0;
               SCIP_CALL( SCIPsdpiSolverGetIterations(sdpi->sdpisolver, &naddediterations) );
               sdpi->niterations += naddediterations;
               naddedsdpcalls = 0;
               SCIP_CALL( SCIPsdpiSolverGetSdpCalls(sdpi->sdpisolver, &naddedsdpcalls) );
               sdpi->nsdpcalls += naddedsdpcalls;

               /* If the solver did not converge, we increase the penalty parameter */
               if ( ! SCIPsdpiSolverIsAcceptable(sdpi->sdpisolver) )
               {
                  penaltyparam *= penaltyparamfact;
                  SCIPdebugMessage("Solver did not converge even with penalty formulation, increasing penaltyparameter.\n");
                  continue;
               }

               /* if we succeeded to solve the problem, update the bound */
               SCIP_CALL( SCIPsdpiSolverGetObjval(sdpi->sdpisolver, &objbound) );
               if ( objbound > sdpi->bestbound + sdpi->gaptol )
                  sdpi->bestbound = objbound;

               /* If we don't get a feasible solution to our original problem we have to update either Gamma (if the penalty bound was active
                * in the primal problem) or gaptol (otherwise) */
               if ( ! feasorig )
               {
                  if ( penaltybound )
                  {
                     penaltyparam *= penaltyparamfact;
                     SCIPdebugMessage("Penalty formulation produced a result which is infeasible for the original problem, increasing penaltyparameter.\n");
                  }
                  else
                  {
                     gaptol *= gaptolfact;
                     SCIP_CALL_PARAM( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, SCIP_SDPPAR_GAPTOL, gaptol) );
                     SCIPdebugMessage("Penalty formulation produced a result which is infeasible for the original problem, even though primal penalty "
                           "bound was not reached, decreasing tolerance for duality gap in SDP-solver.\n");
                  }
               }
            }

            /* reset the tolerance in the SDP-solver */
            if ( gaptol > sdpi->gaptol )
            {
               SCIP_CALL_PARAM( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, SCIP_SDPPAR_GAPTOL, sdpi->gaptol) );
            }

            /* check if we were able to solve the problem in the end */
            if ( SCIPsdpiSolverIsAcceptable(sdpi->sdpisolver) && feasorig )
            {
               sdpi->penalty = TRUE;
               sdpi->solved = TRUE;
            }
#if 0 /* we don't really know if it is infeasible or just ill-posed (no KKT-point) */
            else if ( SCIPsdpiSolverIsAcceptable(sdpi->sdpisolver) && ! feasorig )
            {
               SCIPdebugMessage("Problem was found to be infeasible using a penalty formulation.\n");
               sdpi->infeasible = TRUE;
               sdpi->penalty = TRUE;
               sdpi->solved = TRUE;
            }
#endif
            else
            {
               SCIPdebugMessage("SDP-Solver could not solve the problem even after using a penalty formulation.\n");
               sdpi->solved = FALSE;
               sdpi->penalty = TRUE;
            }

            /* if we still didn't succeed and enforceslatercheck was set, we finally test for the Slater condition to give a reason for failure */
            if ( sdpi->solved == FALSE && enforceslatercheck )
            {
               SCIP_CALL( checkSlaterCondition(sdpi, timelimit, sdpi->sdpilb, sdpi->sdpiub,
                     sdpconstnblocknonz, sdpconstnnonz, sdpconstrow, sdpconstcol, sdpconstval,
                     sdpi->indchanges, sdpi->nremovedinds, sdpi->nlpcons, sdpi->sdpilpindchanges,
                     sdpi->sdpilplhs, sdpi->sdpilprhs, sdpilpnnonz, sdpi->sdpilpbeg, sdpi->sdpilpind, sdpi->sdpilpval,
                     sdpi->blockindchanges, sdpi->nremovedblocks, TRUE) );
            }
            else if ( sdpi->solved == FALSE )
            {
#if 0
               SCIPmessagePrintInfo(sdpi->messagehdlr, "Numerical trouble.\n");
#else
               SCIPdebugMessage("SDP-Interface was unable to solve SDP %d.\n", sdpi->sdpid);/*lint !e687*/
#endif
            }
         }
      }
   }

   /* free memory */
   for (b = sdpi->nsdpblocks - 1; b >= 0; --b)
   {
      BMSfreeBlockMemoryArray(sdpi->blkmem, &(sdpconstval[b]), maxsdpconstnblocknonz[b]);/*lint !e737*/
      BMSfreeBlockMemoryArray(sdpi->blkmem, &(sdpconstcol[b]), maxsdpconstnblocknonz[b]);/*lint !e737*/
      BMSfreeBlockMemoryArray(sdpi->blkmem, &(sdpconstrow[b]), maxsdpconstnblocknonz[b]);/*lint !e737*/
   }
   BMSfreeBlockMemoryArray(sdpi->blkmem, &sdpconstval, sdpi->nsdpblocks);/*lint !e737*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &sdpconstcol, sdpi->nsdpblocks);/*lint !e737*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &sdpconstrow, sdpi->nsdpblocks);/*lint !e737*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &maxsdpconstnblocknonz, sdpi->nsdpblocks);/*lint !e737*/
   BMSfreeBlockMemoryArray(sdpi->blkmem, &sdpconstnblocknonz, sdpi->nsdpblocks);/*lint !e737*/

   sdpi->sdpid++;

   SDPIclockStop(sdpi->usedsdpitime);

   return SCIP_OKAY;
}
/**@} */



/*
 * Solution Information Methods
 */

/**@name Solution Information Methods */
/**@{ */

/** returns whether a solve method was successfully called after the last modification of the SDP */
SCIP_Bool SCIPsdpiWasSolved(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );

   return sdpi->solved;
}

/** returns whether the original problem was solved, if SCIPsdpiWasSolved = true and SCIPsdpiSolvedOrig = false, then a penalty formulation was solved */
SCIP_Bool SCIPsdpiSolvedOrig(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );

   return ( sdpi->solved && ! sdpi->penalty );
}

/** returns whether a primal solution or ray is available */
SCIP_Bool SCIPsdpiHavePrimalSol(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   if ( ! sdpi->solved )
      return FALSE;
   else if ( sdpi->allfixed )
      return TRUE;
   else if ( sdpi->infeasible )
      return FALSE;
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      return TRUE;
   else if ( SCIPsdpiSolverIsPrimalInfeasible(sdpi->sdpisolver) )
      return FALSE;

   return TRUE;
}

/** returns true if the solver could determine whether the problem is feasible, so it returns true if the
 *  solver knows that the problem is feasible/infeasible/unbounded, it returns false if the solver does not know
 *  anything about the feasibility status and thus the functions IsPrimalFeasible etc. should not be used
 */
SCIP_Bool SCIPsdpiFeasibilityKnown(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible || sdpi->allfixed || sdpi->onevarsdpobjval > SCIP_ONEVAR_UNSOLVED )
      return TRUE;

   return SCIPsdpiSolverFeasibilityKnown(sdpi->sdpisolver);
}

/** gets information about proven primal and dual feasibility of the current SDP-solution */
SCIP_RETCODE SCIPsdpiGetSolFeasibility(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Bool*            primalfeasible,     /**< pointer to store the proven primal feasibility status */
   SCIP_Bool*            dualfeasible        /**< pointer to store the proven dual feasibility status */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing dual problem, primal problem is feasible if all variables are fixed,
       * otherwise primal feasibility status cannot be determined */
      if ( sdpi->allfixed )
         *primalfeasible = TRUE;
      else
         *primalfeasible = FALSE;
      *dualfeasible = FALSE;
      return SCIP_OKAY;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and dual problem is feasible, primal problem is feasible as well */
      *primalfeasible = TRUE;
      *dualfeasible = TRUE;
      return SCIP_OKAY;
   }
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_OPTIMAL )
   {
      *dualfeasible = TRUE;
      *primalfeasible = TRUE;
      return SCIP_OKAY;
   }
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_INFEASIBLE )
   {
      *dualfeasible = FALSE;
      *primalfeasible = TRUE; /* one variable SDP is only solved for bounded problems, for which the primal is feasible */
      return SCIP_OKAY;
   }

   SCIP_CALL( SCIPsdpiSolverGetSolFeasibility(sdpi->sdpisolver, primalfeasible, dualfeasible) );

   return SCIP_OKAY;
}

/** returns TRUE iff SDP is proven to be primal unbounded */
SCIP_Bool SCIPsdpiIsPrimalUnbounded(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing dual problem, primal problem always has a ray and is feasible if all
       * variables are fixed (else it is not necessarily feasible) */
      if ( sdpi->allfixed )
         return TRUE;
      else
         return FALSE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and dual problem is feasible, primal problem is feasible and bounded */
      return FALSE;
   }
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_OPTIMAL )
      return FALSE;
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_INFEASIBLE )
      return TRUE;   /* primal is always feasible, since dual is bounded */

   return SCIPsdpiSolverIsPrimalUnbounded(sdpi->sdpisolver);
}

/** returns TRUE iff SDP is proven to be primal infeasible */
SCIP_Bool SCIPsdpiIsPrimalInfeasible(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing dual problem, primal status is feasible if all variables are fixed
       * and unknown else */
      return FALSE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and dual problem is feasible, primal problem is feasible as well */
      return FALSE;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      return FALSE; /* primal is always feasible */

   return SCIPsdpiSolverIsPrimalInfeasible(sdpi->sdpisolver);
}

/** returns TRUE iff SDP is proven to be primal feasible */
SCIP_Bool SCIPsdpiIsPrimalFeasible(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert(sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing dual problem, primal status is feasible if all variables are fixed
       * and unknown else */
      if ( sdpi->allfixed )
         return TRUE;
      else
         return FALSE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and dual problem is feasible, primal problem is feasible as well */
      return TRUE;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      return TRUE; /* primal is always feasible */

   return SCIPsdpiSolverIsPrimalFeasible(sdpi->sdpisolver);
}

/** returns TRUE iff SDP is proven to be dual unbounded */
SCIP_Bool SCIPsdpiIsDualUnbounded(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing dual problem */
      return FALSE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and dual problem is feasible */
      return FALSE;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      return FALSE;

   return SCIPsdpiSolverIsDualUnbounded(sdpi->sdpisolver);
}

/** returns TRUE iff SDP is proven to be dual infeasible */
SCIP_Bool SCIPsdpiIsDualInfeasible(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing dual problem */
      return TRUE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and dual problem is feasible */
      return FALSE;
   }
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_OPTIMAL )
      return FALSE;
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_INFEASIBLE )
      return TRUE;

   return SCIPsdpiSolverIsDualInfeasible(sdpi->sdpisolver);
}

/** returns TRUE iff SDP is proven to be dual feasible */
SCIP_Bool SCIPsdpiIsDualFeasible(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing dual problem */
      return FALSE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and dual problem is feasible */
      return TRUE;
   }
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_OPTIMAL )
      return TRUE;
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_INFEASIBLE )
      return FALSE;

   return SCIPsdpiSolverIsDualFeasible(sdpi->sdpisolver);
}

/** returns TRUE iff the solver converged */
SCIP_Bool SCIPsdpiIsConverged(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing dual problem - this counts as converged */
      return TRUE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and dual problem is feasible - this counts as converged */
      return TRUE;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      return TRUE;

   return SCIPsdpiSolverIsConverged(sdpi->sdpisolver);
}

/** returns TRUE iff the objective limit was reached */
SCIP_Bool SCIPsdpiIsObjlimExc(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing dual problem - objective limit was not reached */
      return FALSE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and dual problem is feasible - objective limit was not reached */
      return FALSE;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      return FALSE;

   return SCIPsdpiSolverIsObjlimExc(sdpi->sdpisolver);
}

/** returns TRUE iff the iteration limit was reached */
SCIP_Bool SCIPsdpiIsIterlimExc(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing problem - iteration limit was not reached */
      return FALSE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and problem is feasible - iteration limit was not reached */
      return FALSE;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      return FALSE;

   return SCIPsdpiSolverIsIterlimExc(sdpi->sdpisolver);
}

/** returns TRUE iff the time limit was reached */
SCIP_Bool SCIPsdpiIsTimelimExc(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing problem - time limit was not reached */
      return FALSE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and problem is feasible - time limit was not reached */
      return FALSE;
   }
   else if ( ! sdpi->solved )
   {
      SCIPdebugMessage("Problem was not solved, time limit not exceeded.\n");
      return FALSE;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      return FALSE;

   return SCIPsdpiSolverIsTimelimExc(sdpi->sdpisolver);
}

/** returns the internal solution status of the solver, which has the following meaning:<br>
 * -1: solver was not started<br>
 *  0: converged<br>
 *  1: infeasible start<br>
 *  2: numerical problems<br>
 *  3: objective limit reached<br>
 *  4: iteration limit reached<br>
 *  5: time limit reached<br>
 *  6: user termination<br>
 *  7: other
 */
int SCIPsdpiGetInternalStatus(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );

   if ( ! sdpi->solved )
   {
      SCIPdebugMessage("Problem wasn't solved yet.\n");
      return -1;
   }
   else if ( sdpi->infeasible )
   {
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no internal status available.\n");
      return 0;
   }
   else if ( sdpi->allfixed )
   {
      SCIPdebugMessage("All variables are fixed, no internal status available.\n");
      return 0;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      SCIPdebugMessage("Solved one variable SDP, no internal status available.\n");
      return 0;
   }

   return SCIPsdpiSolverGetInternalStatus(sdpi->sdpisolver);
}

/** returns TRUE iff SDP was solved to optimality, meaning the solver converged and returned primal and dual feasible solutions */
SCIP_Bool SCIPsdpiIsOptimal(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED_BOOL(sdpi);

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing problem */
      return FALSE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and problem is feasible */
      return TRUE;
   }
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_OPTIMAL )
      return TRUE;
   else if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_INFEASIBLE )
      return FALSE;

   return SCIPsdpiSolverIsOptimal(sdpi->sdpisolver);
}

/** returns TRUE iff SDP was solved to optimality or some other status was reached that is still acceptable inside a
 *  Branch & Bound framework
 */
SCIP_Bool SCIPsdpiIsAcceptable(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL );

   if ( sdpi->infeasible )
   {
      /* infeasibility was detected while preparing problem - this is acceptable */
      return TRUE;
   }
   else if ( sdpi->allfixed )
   {
      /* all variables are fixed and problem is feasible - this is acceptable */
      return TRUE;
   }
   else if ( ! sdpi->solved )
   {
      SCIPdebugMessage("Problem not solved succesfully, this is not acceptable in a B&B context.\n");
      return FALSE;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      return TRUE;

   return SCIPsdpiSolverIsAcceptable(sdpi->sdpisolver);
}

/** gets objective value of solution */
SCIP_RETCODE SCIPsdpiGetObjval(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Real*            objval              /**< pointer to store the objective value */
   )
{
   assert( sdpi != NULL );
   assert( objval != NULL );

   CHECK_IF_SOLVED(sdpi);

   if ( sdpi->infeasible )
      *objval = SCIPsdpiInfinity(sdpi); /* we are minimizing */
   else if ( sdpi->allfixed )
   {
      int v;

      /* since all variables were fixed during preprocessing, we have to compute it ourselves here */
      *objval = 0;

      for (v = 0; v < sdpi->nvars; v++)
         *objval += sdpi->sdpilb[v] * sdpi->obj[v];
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      assert( sdpi->onevarsdpobjval != SCIP_INVALID );
      *objval = sdpi->onevarsdpobjval;
   }
   else
   {
      SCIP_CALL( SCIPsdpiSolverGetObjval(sdpi->sdpisolver, objval) );
   }

   return SCIP_OKAY;
}

/** gets the best lower bound on the objective (this is equal to objval, if the problem was solved successfully, but can also give a bound
 *  if we did not get a feasible solution using the penalty approach)
 */
SCIP_RETCODE SCIPsdpiGetLowerObjbound(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Real*            objlb               /**< pointer to store the lower bound on the objective value */
   )
{
   assert( sdpi != NULL );
   assert( objlb != NULL );

   /* if we could successfully solve the problem, the best bound is the optimal objective */
   if ( sdpi->solved )
   {
      if ( sdpi->infeasible )
         *objlb = SCIPsdpiInfinity(sdpi); /* we are minimizing */
      else if ( sdpi->allfixed )
      {
         int v;

         /* since all variables were fixed during preprocessing, we have to compute bound ourselves here */
         *objlb = 0;

         for (v = 0; v < sdpi->nvars; v++)
            *objlb += sdpi->sdpilb[v] * sdpi->obj[v];
      }
      else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
      {
         assert( sdpi->onevarsdpobjval != SCIP_INVALID );
         *objlb = sdpi->onevarsdpobjval;
      }
      else
      {
         SCIP_CALL( SCIPsdpiSolverGetObjval(sdpi->sdpisolver, objlb) );
      }

      return SCIP_OKAY;
   }

   /* if we could not solve it, but tried the penalty formulation, we take the best bound computed by the penalty approach */
   if ( sdpi->penalty )
   {
      *objlb = sdpi->bestbound;
      return SCIP_OKAY;
   }

   /* if we could not solve it and did not use the penalty formulation (e.g. because the time limit was reached), we have no information */
   *objlb = -SCIPsdpiInfinity(sdpi);

   return SCIP_OKAY;
}

/** gets dual solution vector for feasible SDPs */
SCIP_RETCODE SCIPsdpiGetDualSol(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Real*            objval,             /**< pointer to store the objective value (or NULL) */
   SCIP_Real*            dualsol             /**< array of length nvars to store the dual solution vector (or NULL) */
   )
{
   assert( sdpi != NULL );
   CHECK_IF_SOLVED(sdpi);

   if ( sdpi->infeasible )
   {
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no solution available.\n");
      *objval = SCIPsdpiInfinity(sdpi); /* we are minimizing */
   }
   else if ( sdpi->allfixed )
   {
      int v;

      if ( objval != NULL )
      {
         SCIP_CALL( SCIPsdpiGetObjval(sdpi, objval) );
      }

      /* we give the fixed values as the solution */
      for (v = 0; v < sdpi->nvars; v++)
         dualsol[v] = sdpi->sdpilb[v];
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      int v;

      if ( objval != NULL )
      {
         SCIP_CALL( SCIPsdpiGetObjval(sdpi, objval) );
      }

      /* we give the fixed values as the solution */
      for (v = 0; v < sdpi->nvars; v++)
         dualsol[v] = sdpi->sdpilb[v];

      /* fill in value for one variable */
      assert( 0 <= sdpi->onevarsdpidx && sdpi->onevarsdpidx < sdpi->nvars );
      dualsol[sdpi->onevarsdpidx] = sdpi->onevarsdpoptval;
   }
   else
   {
      SCIP_CALL( SCIPsdpiSolverGetDualSol(sdpi->sdpisolver, objval, dualsol) );
   }

   return SCIP_OKAY;
}

/** return number of nonzeros for each block of the primal solution matrix X for the preoptimal solution */
SCIP_RETCODE SCIPsdpiGetPreoptimalPrimalNonzeros(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   int                   nblocks,            /**< length of startXnblocknonz (should be nsdpblocks + 1) */
   int*                  startXnblocknonz    /**< pointer to store number of nonzeros for row/col/val-arrays in each block
                                              *   or first entry -1 if no primal solution is available */
   )
{
   assert( sdpi != NULL );
   assert( nblocks >= 0 );
   assert( startXnblocknonz != NULL );

   if ( sdpi->infeasible )
   {
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no preoptimal solution available.\n");
      startXnblocknonz[0] = -1;
   }
   else if ( sdpi->allfixed )
   {
      SCIPdebugMessage("All variables are fixed, no solution available.\n");
      startXnblocknonz[0] = -1;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      SCIPdebugMessage("One variable SDP solved, no solution available.\n");
      startXnblocknonz[0] = -1;
   }
   else
   {
      SCIP_CALL( SCIPsdpiSolverGetPreoptimalPrimalNonzeros(sdpi->sdpisolver, nblocks, startXnblocknonz) );
   }

   return SCIP_OKAY;
}

/** gets preoptimal dual solution vector and primal matrix for warmstarting purposes
 *
 *  @note last block will be the LP block (if one exists) with indices lhs(row0), rhs(row0), lhs(row1), ..., lb(var1), ub(var1), lb(var2), ...
 *  independent of some lhs/rhs being infinity
 */
SCIP_RETCODE SCIPsdpiGetPreoptimalSol(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Bool*            success,            /**< could a preoptimal solution be returned ? */
   SCIP_Real*            dualsol,            /**< pointer to store the dual solution vector, may be NULL if not needed */
   int                   nblocks,            /**< length of startXnblocknonz (should be nsdpblocks + 1) or -1 if no primal matrix should be returned */
   int*                  startXnblocknonz,   /**< input: allocated memory for row/col/val-arrays in each block (or NULL if nblocks = -1)
                                              *   output: number of nonzeros in each block or first entry -1 if no primal solution is available */
   int**                 startXrow,          /**< pointer to store row indices of X (or NULL if nblocks = -1) */
   int**                 startXcol,          /**< pointer to store column indices of X (or NULL if nblocks = -1) */
   SCIP_Real**           startXval           /**< pointer to store values of X (or NULL if nblocks = -1) */
   )
{
   assert( sdpi != NULL );
   assert( success != NULL );
   assert( dualsol != NULL );
   assert( startXnblocknonz != NULL || nblocks == -1 );
   assert( startXrow != NULL || nblocks == -1 );
   assert( startXcol != NULL || nblocks == -1 );
   assert( startXval != NULL || nblocks == -1 );

   if ( sdpi->infeasible )
   {
      *success = FALSE;
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no preoptimal solution available.\n");
      assert( startXnblocknonz != NULL );
      startXnblocknonz[0] = -1;
   }
   else if ( sdpi->allfixed )
   {
      int v;

      assert( dualsol != NULL );

      *success = FALSE;

      /* we give the fixed values as the solution */
      for (v = 0; v < sdpi->nvars; v++)
         dualsol[v] = sdpi->sdpilb[v];

      if ( nblocks > -1 )
      {
         SCIPdebugMessage("No primal solution available, as problem was solved during preprocessing\n");
         assert( startXnblocknonz != NULL );
         startXnblocknonz[0] = -1;
      }

      return SCIP_OKAY;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      int v;

      assert( dualsol != NULL );

      *success = FALSE;

      /* we give the fixed values as the solution */
      for (v = 0; v < sdpi->nvars; v++)
         dualsol[v] = sdpi->sdpilb[v];

      /* fill in value for one variable */
      assert( 0 <= sdpi->onevarsdpidx && sdpi->onevarsdpidx < sdpi->nvars );
      dualsol[sdpi->onevarsdpidx] = sdpi->onevarsdpoptval;

      if ( nblocks > -1 )
      {
         SCIPdebugMessage("No primal solution available, since one variable SDP was solved.\n");
         assert( startXnblocknonz != NULL );
         startXnblocknonz[0] = -1;
      }

      return SCIP_OKAY;
   }
   else
   {
      SCIP_CALL( SCIPsdpiSolverGetPreoptimalSol(sdpi->sdpisolver, success, dualsol, nblocks, startXnblocknonz,
            startXrow, startXcol, startXval) );
   }

   return SCIP_OKAY;
}

/** gets the primal solution corresponding to the lower and upper variable-bounds in the primal problem
 *
 *  The arrays should have size nvars.
 *
 *  @note If a variable is either fixed or unbounded in the dual problem, a zero will be returned for the non-existent primal variable.
 */
SCIP_RETCODE SCIPsdpiGetPrimalBoundVars(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   SCIP_Real*            lbvals,             /**< array to store the values of the variables corresponding to lower bounds in the primal problem */
   SCIP_Real*            ubvals,             /**< array to store the values of the variables corresponding to upper bounds in the primal problem */
   SCIP_Bool*            success             /**< pointer to store whether values could be retrieved */
   )
{
   assert( sdpi != NULL );
   assert( lbvals != NULL );
   assert( ubvals != NULL );
   assert( success != NULL );

   *success = FALSE;
   if ( ! sdpi->solved )
   {
      SCIPdebugMessage("Problem not solved, no primal solution available.\n");
   }
   else if ( sdpi->allfixed )
   {
      int i;

      /* if all variables are fixed, we return 0 as primal solution */
      for (i = 0; i < sdpi->nvars; i++)
      {
         lbvals[i] = 0.0;
         ubvals[i] = 0.0;
      }
      *success = TRUE;
   }
   else if ( sdpi->infeasible )
   {
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no primal solution available.\n");
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      int i;

      /* initialize variables to be 0 */
      for (i = 0; i < sdpi->nvars; i++)
      {
         lbvals[i] = 0.0;
         ubvals[i] = 0.0;
      }

      if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_INFEASIBLE )
      {
         /* fill in dual variables for single constraint */
         if ( sdpi->onevarsdpcertval > sdpi->feastol )
            ubvals[sdpi->onevarsdpidx] = sdpi->onevarsdpcertval;
         else if ( sdpi->onevarsdpcertval < - sdpi->feastol )
            lbvals[sdpi->onevarsdpidx] = - sdpi->onevarsdpcertval;
      }
      else
      {
         assert( sdpi->solvedonevarsdp == SCIP_ONEVAR_OPTIMAL );

         /* we only solve 1-d SDPs for nonnegative objective coefficient */
         assert( sdpi->obj[sdpi->onevarsdpidx] >= - sdpi->feastol );

         /* if the optimal value is equal to the lower bound */
         if ( REALABS(sdpi->onevarsdpoptval - sdpi->sdpilb[sdpi->onevarsdpidx]) < sdpi->feastol )
         {
            /* the primal variable is equal to the objective */
            lbvals[sdpi->onevarsdpidx] = sdpi->obj[sdpi->onevarsdpidx];
         }
      }

      *success = TRUE;
   }
   /* If the primal is infeasible, we usually do not have a dual solution nor a primal ray. */
   else if ( SCIPsdpiSolverIsPrimalInfeasible(sdpi->sdpisolver) )
   {
      SCIPdebugMessage("Primal problem is infeasible, no primal solution available.\n");
   }
   else
   {
      SCIP_RETCODE retcode;

      /* We assume that there exists a primal ray if the dual is infeasible and it is returned by the same function. */
      assert ( SCIPsdpiSolverIsDualInfeasible(sdpi->sdpisolver) || SCIPsdpiSolverIsDualFeasible(sdpi->sdpisolver) );

      retcode = SCIPsdpiSolverGetPrimalBoundVars(sdpi->sdpisolver, lbvals, ubvals);
      if ( retcode == SCIP_OKAY )
         *success = TRUE;
   }

   return SCIP_OKAY;
}

/** gets the primal solution corresponding to the left- and right-hand sides of the LP rows
 *
 *  @note If an LP row was removed, we return a value of 0.0. This can happen if the row is redundant, e.g., all
 *  involved variables are fixed, or it contains variable a single variable only.
 */
SCIP_RETCODE SCIPsdpiGetPrimalLPSides(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   SCIP_Real*            lhsvals,            /**< array to store the values of the variables corresponding to LP lhs */
   SCIP_Real*            rhsvals,            /**< array to store the values of the variables corresponding to LP rhs */
   SCIP_Bool*            success             /**< pointer to store whether values could be retrieved */
   )
{
   int i;

   assert( sdpi != NULL );
   assert( lhsvals != NULL );
   assert( rhsvals != NULL );
   assert( success != NULL );

   *success = FALSE;
   if ( ! sdpi->solved )
   {
      SCIPdebugMessage("Problem not solved, no primal solution available.\n");
   }
   else if ( sdpi->allfixed )
   {
      /* if all variables are fixed, we return 0 as a primal solution */
      for (i = 0; i < sdpi->nlpcons; ++i)
      {
         lhsvals[i] = 0.0;
         rhsvals[i] = 0.0;
      }
      *success = TRUE;
   }
   else if ( sdpi->infeasible )
   {
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no primal solution available.\n");
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      /* for SDP constraint with one variable, no LP rows can exist, so return 0.0 for all (the LP rows might have been preprocesssed away) */
      for (i = 0; i < sdpi->nlpcons; ++i)
      {
         lhsvals[i] = 0.0;
         rhsvals[i] = 0.0;
      }
      *success = TRUE;
   }
   /* If the primal is infeasible, we usually do not have a dual solution nor a primal ray. */
   else if ( SCIPsdpiSolverIsPrimalInfeasible(sdpi->sdpisolver) )
   {
      SCIPdebugMessage("Primal problem is infeasible, no primal solution available.\n");
   }
   else
   {
      SCIP_Real* sdpilhsvals;
      SCIP_Real* sdpirhsvals;
      SCIP_Real* sdpilbvals;
      SCIP_Real* sdpiubvals;
      SCIP_RETCODE retcode;

      /* We assume that there exists a primal ray if the dual is infeasible and it is returned by the same function. */
      assert ( SCIPsdpiSolverIsDualInfeasible(sdpi->sdpisolver) || SCIPsdpiSolverIsDualFeasible(sdpi->sdpisolver) );

      assert( 0 <= sdpi->nactivelpcons && sdpi->nactivelpcons <= sdpi->nlpcons );
      BMS_CALL( BMSallocBufferMemoryArray(sdpi->bufmem, &sdpilhsvals, sdpi->nlpcons) );
      BMS_CALL( BMSallocBufferMemoryArray(sdpi->bufmem, &sdpirhsvals, sdpi->nlpcons) );
      BMS_CALL( BMSallocBufferMemoryArray(sdpi->bufmem, &sdpilbvals, sdpi->nvars) );
      BMS_CALL( BMSallocBufferMemoryArray(sdpi->bufmem, &sdpiubvals, sdpi->nvars) );

      retcode = SCIPsdpiSolverGetPrimalLPSides(sdpi->sdpisolver, sdpi->nlpcons, sdpi->sdpilpindchanges, sdpi->sdpilplhs, sdpi->sdpilprhs, sdpilhsvals, sdpirhsvals);

      if ( retcode == SCIP_OKAY )
      {
         /* also get primal values for variables bounds to set values for LP rows that were replaced by variable bounds */
         retcode = SCIPsdpiSolverGetPrimalBoundVars(sdpi->sdpisolver, sdpilbvals, sdpiubvals);

         if ( retcode == SCIP_OKAY )
         {
            /* initialize values to 0.0 */
            for (i = 0; i < sdpi->nlpcons; ++i)
            {
               lhsvals[i] = 0.0;
               rhsvals[i] = 0.0;
            }

            for (i = 0; i < sdpi->nvars; ++i)
            {
               int idx;

               if ( sdpi->sdpilbrowidx[i] != 0 )
               {
                  idx = sdpi->sdpilbrowidx[i];
                  assert( -sdpi->nlpcons - 1 < idx && idx < sdpi->nlpcons + 1 );
                  if ( idx > 0 )
                  {
                     assert( sdpi->sdpilpindchanges[idx - 1] < 0 );
                     rhsvals[idx - 1] = sdpilbvals[i];
                  }
                  else
                  {
                     assert( sdpi->sdpilpindchanges[- idx - 1] < 0 );
                     lhsvals[- idx - 1] = sdpilbvals[i];
                  }
               }

               if ( sdpi->sdpiubrowidx[i] != 0 )
               {
                  idx = sdpi->sdpiubrowidx[i];
                  assert( -sdpi->nlpcons - 1 < idx && idx < sdpi->nlpcons + 1 );
                  if ( idx > 0 )
                  {
                     assert( sdpi->sdpilpindchanges[idx - 1] < 0 );
                     rhsvals[idx - 1] = sdpiubvals[i];
                  }
                  else
                  {
                     assert( sdpi->sdpilpindchanges[- idx - 1] < 0 );
                     lhsvals[- idx - 1] = sdpiubvals[i];
                  }
               }
            }

            /* fill in data */
            for (i = 0; i < sdpi->nlpcons; ++i)
            {
               if ( sdpi->sdpilpindchanges[i] >= 0 )
               {
                  lhsvals[i] = sdpilhsvals[i];
                  rhsvals[i] = sdpirhsvals[i];
               }
            }
            *success = TRUE;
         }
      }
      BMSfreeBufferMemoryArray(sdpi->bufmem, &sdpiubvals);
      BMSfreeBufferMemoryArray(sdpi->bufmem, &sdpilbvals);
      BMSfreeBufferMemoryArray(sdpi->bufmem, &sdpirhsvals);
      BMSfreeBufferMemoryArray(sdpi->bufmem, &sdpilhsvals);
   }

   return SCIP_OKAY;
}

/** return number of nonzeros for each block of the primal solution matrix X */
SCIP_RETCODE SCIPsdpiGetPrimalNonzeros(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   int                   nblocks,            /**< length of startXnblocknonz (should be nsdpblocks + 1) */
   int*                  startXnblocknonz    /**< pointer to store number of nonzeros for row/col/val-arrays in each block */
   )
{
   assert( sdpi != NULL );

   if ( sdpi->infeasible )
   {
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no primal solution available.\n");
      startXnblocknonz[0] = -1;
   }
   else if ( sdpi->allfixed )
   {
      SCIPdebugMessage("All variables fixed during preprocessing, no primal solution available.\n");
      startXnblocknonz[0] = -1;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      SCIPdebugMessage("Solved one variable SDP, no primal solution available.\n");
      startXnblocknonz[0] = -1;
   }
   else
   {
      SCIP_CALL( SCIPsdpiSolverGetPrimalNonzeros(sdpi->sdpisolver, nblocks, startXnblocknonz) );
   }

   return SCIP_OKAY;
}

/** returns the primal matrix X
 *
 *  @note last block will be the LP block (if one exists) with indices lhs(row0), rhs(row0), lhs(row1), ..., lb(var1), ub(var1), lb(var2), ...
 *  independent of some lhs/rhs being infinity
 *
 *  @note If the allocated memory for row/col/val is insufficient, a debug message will be thrown and the neccessary amount is returned in startXnblocknonz
 */
SCIP_RETCODE SCIPsdpiGetPrimalMatrix(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   int                   nblocks,            /**< length of startXnblocknonz (should be nsdpblocks + 1) */
   int*                  startXnblocknonz,   /**< input: allocated memory for row/col/val-arrays in each block
                                              *   output: number of nonzeros in each block */
   int**                 startXrow,          /**< pointer to store row indices of X */
   int**                 startXcol,          /**< pointer to store column indices of X */
   SCIP_Real**           startXval           /**< pointer to store values of X */
   )
{  /* TODO: should also set startXnblocknonz[0] = -1 in case the problem was solved in presolving */
   assert( sdpi != NULL );

   if ( sdpi->infeasible )
   {
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no primal solution available.\n");
   }
   else if ( sdpi->allfixed )
   {
      SCIPdebugMessage("All variables fixed during preprocessing, no primal solution available.\n");
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      SCIPdebugMessage("Solved one variable SDP, no primal solution available.\n");
   }
   else
   {
      SCIP_CALL( SCIPsdpiSolverGetPrimalMatrix(sdpi->sdpisolver, nblocks, startXnblocknonz, startXrow, startXcol, startXval) );
   }

   return SCIP_OKAY;
}

/** returns the primal solution matrix (without LP rows) */
SCIP_RETCODE SCIPsdpiGetPrimalSolutionMatrix(
   SCIP_SDPI*            sdpi,               /**< pointer to an SDP-interface structure */
   SCIP_Real**           primalmatrices,     /**< pointer to store values of the primal matrix */
   SCIP_Bool*            success             /**< pointer to store whether the call was successfull */
   )
{
   assert( success != NULL );

   *success = FALSE;
   if ( ! sdpi->solved )
   {
      SCIPdebugMessage("Problem was not solved, no primal solution available.\n");
   }
   else if ( sdpi->allfixed )
   {
      int b;

      /* if the eigenvectors have not been stored, we return */
      if ( sdpi->allfixedeigenvecs == NULL )
         return SCIP_OKAY;

      /* loop over all SDP blocks */
      for (b = 0; b < sdpi->nsdpblocks; b++)
      {
         int blocksize;
         int i;
         int j;

         assert( primalmatrices[b] != NULL );

         blocksize = sdpi->sdpblocksizes[b];

         if ( sdpi->infeasible )
         {
            /* construct rank1 matrix */
            for (i = 0; i < blocksize; ++i)
            {
               for (j = 0; j < blocksize; ++j)
                  primalmatrices[b][i * blocksize + j] = sdpi->allfixedeigenvecs[b][i] * sdpi->allfixedeigenvecs[b][j];
            }
         }
         else
         {
            /* the 0-matrix s optimal if we are feasible */
            for (j = 0; j < blocksize * blocksize; ++j)
               primalmatrices[b][j] = 0.0;
         }
      }
      *success = TRUE;
   }
   else if ( sdpi->infeasible )
   {
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no primal solution available.\n");
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      SCIP_Real s = 1.0;
      int blocksize;
      int i;
      int j;

      assert( 0 <= sdpi->onevarsdpidx && sdpi->onevarsdpidx < sdpi->nvars );
      assert( sdpi->onevarsdpcertvec != NULL && sdpi->onevarsdpcertval != SCIP_INVALID );
      assert( primalmatrices[0] != NULL );
      assert( sdpi->nsdpblocks == 1 );

      blocksize = sdpi->sdpblocksizes[0];

      /* We can only treat the optimal case in which the supergradient is positive (use eigenvector nevertheless,
       * because it results in a psd matrix. */
      if ( sdpi->solvedonevarsdp == SCIP_ONEVAR_OPTIMAL && sdpi->onevarsdpcertval > sdpi->feastol )
         s = sdpi->obj[sdpi->onevarsdpidx] / sdpi->onevarsdpcertval;

      /* fill in rank-1 solution matrix */
      for (i = 0; i < blocksize; ++i)
      {
         for (j = 0; j < blocksize; ++j)
            primalmatrices[0][i * blocksize + j] = s * sdpi->onevarsdpcertvec[i] * sdpi->onevarsdpcertvec[j];
      }
      *success = TRUE;
   }
   /* If the primal is infeasible, we usually do not have a dual solution nor a primal ray. */
   else if ( SCIPsdpiSolverIsPrimalInfeasible(sdpi->sdpisolver) )
   {
      SCIPdebugMessage("Primal problem is infeasible, no primal solution available.\n");
   }
   else
   {
      SCIP_RETCODE retcode;

      /* At this point the SDP is either optimally solved or dual infeasible. In the latter case, We assume that there
       * exists a primal ray if the dual is infeasible and it is returned by the same function. */
      retcode = SCIPsdpiSolverGetPrimalSolutionMatrix(sdpi->sdpisolver, sdpi->nsdpblocks, sdpi->sdpblocksizes, sdpi->indchanges, sdpi->nremovedinds, sdpi->blockindchanges, primalmatrices);
      if ( retcode == SCIP_OKAY )
         *success = TRUE;
   }

   return SCIP_OKAY;
}

/** return the maximal absolute value of the optimal primal matrix */
SCIP_Real SCIPsdpiGetMaxPrimalEntry(
   SCIP_SDPI*            sdpi                /**< pointer to an SDP-interface structure */
   )
{
   assert( sdpi != NULL );

   return SCIPsdpiSolverGetMaxPrimalEntry(sdpi->sdpisolver);
}

/** gets the time for the last SDP optimization call of solver */
SCIP_RETCODE SCIPsdpiGetTime(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Real*            opttime             /**< pointer to store the time for optimization of the solver */
   )
{
   assert( sdpi != NULL );
   assert( opttime != NULL );

   *opttime = sdpi->opttime;

   return SCIP_OKAY;
}

/** gets the number of SDP-iterations of the last solve call */
SCIP_RETCODE SCIPsdpiGetIterations(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  iterations          /**< pointer to store the number of iterations of the last solve call */
   )
{
   assert( sdpi != NULL );
   assert( iterations != NULL );

   *iterations = sdpi->niterations;

   return SCIP_OKAY;
}

/** gets the number of calls to the SDP-solver for the last solve call */
SCIP_RETCODE SCIPsdpiGetSdpCalls(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  calls               /**< pointer to store the number of calls to the SDP-solver for the last solve call */
   )
{
   assert( sdpi != NULL );
   assert( calls != NULL );

   *calls = sdpi->nsdpcalls;

   return SCIP_OKAY;
}

/** returns which settings the SDP-solver used in the last solve call */
SCIP_RETCODE SCIPsdpiSettingsUsed(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_SDPSOLVERSETTING* usedsetting        /**< the setting used by the SDP-solver */
   )
{
   assert( sdpi != NULL );
   assert( usedsetting != NULL );

   if ( ! sdpi->solved )
   {
      SCIPdebugMessage("Problem was not solved successfully.\n");
      *usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
   }
   else if ( sdpi->infeasible && ! sdpi->penalty ) /* if we solved the penalty formulation, we may also set infeasible if it is infeasible for the original problem */
   {
      SCIPdebugMessage("Infeasibility was detected while preparing the problem, no settings used.\n");
      *usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
   }
   else if ( sdpi->allfixed )
   {
      SCIPdebugMessage("All varialbes fixed during preprocessing, no settings used.\n");
      *usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      SCIPdebugMessage("Solved one variable SDP, no settings used.\n");
      *usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
   }
   else if ( sdpi->penalty )
   {
      *usedsetting = SCIP_SDPSOLVERSETTING_PENALTY;
   }
   else
   {
      SCIP_CALL( SCIPsdpiSolverSettingsUsed(sdpi->sdpisolver, usedsetting) );
   }

   return SCIP_OKAY;
}

/** returns which settings the SDP-solver used in the last solve call and whether primal and dual Slater condition were fulfilled */
SCIP_RETCODE SCIPsdpiSlaterSettings(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_SDPSLATERSETTING* slatersetting      /**< the combination of Slater conditions and successfull settings */
   )
{
   SCIP_SDPSOLVERSETTING usedsetting;

   assert( sdpi != NULL );
   assert( slatersetting != NULL );

   if ( ! sdpi->solved )
   {
      SCIPdebugMessage("Problem was not solved successfully.\n");
      if ( sdpi->bestbound > -SCIPsdpiSolverInfinity(sdpi->sdpisolver) )
      {
         SCIPdebugMessage("But we could at least compute a lower bound.\n");
         if ( sdpi->dualslater == SCIP_SDPSLATER_INF)
            *slatersetting = SCIP_SDPSLATERSETTING_BOUNDEDINFEASIBLE;
         else
         {
            switch( sdpi->primalslater )/*lint --e{788}*/
            {
            case SCIP_SDPSLATER_NOINFO:
               if ( sdpi->dualslater == SCIP_SDPSLATER_NOT )
                  *slatersetting = SCIP_SDPSLATERSETTING_BOUNDEDNOSLATER;
               else
                  *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
               break;
            case SCIP_SDPSLATER_NOT:
               *slatersetting = SCIP_SDPSLATERSETTING_BOUNDEDNOSLATER;
               break;
            case SCIP_SDPSLATER_HOLDS:
               switch( sdpi->dualslater )/*lint --e{788}*/
               {
               case SCIP_SDPSLATER_NOINFO:
                  *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
                  break;
               case SCIP_SDPSLATER_NOT:
                  *slatersetting = SCIP_SDPSLATERSETTING_BOUNDEDNOSLATER;
                  break;
               case SCIP_SDPSLATER_HOLDS:
                  *slatersetting = SCIP_SDPSLATERSETTING_BOUNDEDWSLATER;
                  break;
               default:
                  *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
                  break;
               }
               break;
            default:
               *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
               break;
            }
         }
      }
      else
      {
         if ( sdpi->dualslater == SCIP_SDPSLATER_INF)
            *slatersetting = SCIP_SDPSLATERSETTING_UNSOLVEDINFEASIBLE;
         else
         {
            switch( sdpi->primalslater )/*lint --e{788}*/
            {
            case SCIP_SDPSLATER_NOINFO:
               if ( sdpi->dualslater == SCIP_SDPSLATER_NOT )
                  *slatersetting = SCIP_SDPSLATERSETTING_UNSOLVEDNOSLATER;
               else
                  *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
               break;
            case SCIP_SDPSLATER_NOT:
               *slatersetting = SCIP_SDPSLATERSETTING_UNSOLVEDNOSLATER;
               break;
            case SCIP_SDPSLATER_HOLDS:
               switch( sdpi->dualslater )/*lint --e{788}*/
               {
               case SCIP_SDPSLATER_NOINFO:
                  *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
                  break;
               case SCIP_SDPSLATER_NOT:
                  *slatersetting = SCIP_SDPSLATERSETTING_UNSOLVEDNOSLATER;
                  break;
               case SCIP_SDPSLATER_HOLDS:
                  *slatersetting = SCIP_SDPSLATERSETTING_UNSOLVEDWSLATER;
                  break;
               default:
                  *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
                  break;
               }
               break;
            default:
               *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
               break;
            }
         }
      }
      return SCIP_OKAY;
   }
   else if ( sdpi->infeasible && ( ! sdpi->penalty ) ) /* if we solved the penalty formulation, we may also set infeasible if it is infeasible for the original problem */
   {
      SCIPdebugMessage("Infeasibility was detected while preparing problem, no settings used.\n");
      *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
      return SCIP_OKAY;
   }
   else if ( sdpi->allfixed )
   {
      SCIPdebugMessage("All varialbes fixed during preprocessing, no settings used.\n");
      *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
      return SCIP_OKAY;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      SCIPdebugMessage("Solved one variable SDP, no settings used.\n");
      *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
      return SCIP_OKAY;
   }
   else if ( sdpi->penalty )
   {
      switch( sdpi->primalslater )/*lint --e{788}*/
      {
      case SCIP_SDPSLATER_NOINFO:
         if ( sdpi->dualslater == SCIP_SDPSLATER_NOT )
            *slatersetting = SCIP_SDPSLATERSETTING_PENALTYNOSLATER;
         else if ( sdpi->dualslater == SCIP_SDPSLATER_INF )
            *slatersetting = SCIP_SDPSLATERSETTING_PENALTYINFEASIBLE;
         else
            *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
         break;
      case SCIP_SDPSLATER_NOT:
         if ( sdpi->dualslater == SCIP_SDPSLATER_INF )
            *slatersetting = SCIP_SDPSLATERSETTING_PENALTYINFEASIBLE;
         else
            *slatersetting = SCIP_SDPSLATERSETTING_PENALTYNOSLATER;
         break;
      case SCIP_SDPSLATER_HOLDS:
         switch( sdpi->dualslater )/*lint --e{788}*/
         {
         case SCIP_SDPSLATER_NOINFO:
            *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
            break;
         case SCIP_SDPSLATER_NOT:
            *slatersetting = SCIP_SDPSLATERSETTING_PENALTYNOSLATER;
            break;
         case SCIP_SDPSLATER_HOLDS:
            *slatersetting = SCIP_SDPSLATERSETTING_PENALTYWSLATER;
            break;
         case SCIP_SDPSLATER_INF:
            *slatersetting = SCIP_SDPSLATERSETTING_PENALTYINFEASIBLE;
            break;
         default:
            *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
            break;
         }
         break;
      default:
         *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
         break;
      }
      return SCIP_OKAY;
   }

   switch( sdpi->primalslater )/*lint --e{788}*/
   {
   case SCIP_SDPSLATER_NOINFO:
      if ( sdpi->dualslater == SCIP_SDPSLATER_NOT )
      {
         usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
         SCIP_CALL( SCIPsdpiSolverSettingsUsed(sdpi->sdpisolver, &usedsetting) );
         switch( usedsetting )/*lint --e{788}*/
         {
         case SCIP_SDPSOLVERSETTING_FAST:
            *slatersetting = SCIP_SDPSLATERSETTING_STABLENOSLATER;
            break;
         case SCIP_SDPSOLVERSETTING_MEDIUM:
         case SCIP_SDPSOLVERSETTING_STABLE:
            *slatersetting = SCIP_SDPSLATERSETTING_UNSTABLENOSLATER;
            break;
         default:
            *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
            break;
         }
      }
      if ( sdpi->dualslater == SCIP_SDPSLATER_INF )
      {
         usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
         SCIP_CALL( SCIPsdpiSolverSettingsUsed(sdpi->sdpisolver, &usedsetting) );
         switch( usedsetting )/*lint --e{788}*/
         {
         case SCIP_SDPSOLVERSETTING_FAST:
            *slatersetting = SCIP_SDPSLATERSETTING_STABLEINFEASIBLE;
            break;
         case SCIP_SDPSOLVERSETTING_MEDIUM:
         case SCIP_SDPSOLVERSETTING_STABLE:
            *slatersetting = SCIP_SDPSLATERSETTING_UNSTABLEINFEASIBLE;
            break;
         default:
            *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
            break;
         }
      }
      else
         *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
      break;
   case SCIP_SDPSLATER_NOT:
      if ( sdpi->dualslater == SCIP_SDPSLATER_INF )
      {
         usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
         SCIP_CALL( SCIPsdpiSolverSettingsUsed(sdpi->sdpisolver, &usedsetting) );
         switch( usedsetting )/*lint --e{788}*/
         {
         case SCIP_SDPSOLVERSETTING_FAST:
            *slatersetting = SCIP_SDPSLATERSETTING_STABLEINFEASIBLE;
            break;
         case SCIP_SDPSOLVERSETTING_MEDIUM:
         case SCIP_SDPSOLVERSETTING_STABLE:
            *slatersetting = SCIP_SDPSLATERSETTING_UNSTABLEINFEASIBLE;
            break;
         default:
            *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
            break;
         }
      }
      else
      {
         usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
         SCIP_CALL( SCIPsdpiSolverSettingsUsed(sdpi->sdpisolver, &usedsetting) );
         switch( usedsetting )/*lint --e{788}*/
         {
         case SCIP_SDPSOLVERSETTING_FAST:
            *slatersetting = SCIP_SDPSLATERSETTING_STABLENOSLATER;
            break;
         case SCIP_SDPSOLVERSETTING_MEDIUM:
         case SCIP_SDPSOLVERSETTING_STABLE:
            *slatersetting = SCIP_SDPSLATERSETTING_UNSTABLENOSLATER;
            break;
         default:
            *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
            break;
         }
      }
      break;
   case SCIP_SDPSLATER_HOLDS:
      switch( sdpi->dualslater )/*lint --e{788}*/
      {
      case SCIP_SDPSLATER_NOINFO:
         *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
         break;
      case SCIP_SDPSLATER_NOT:
         usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
         SCIP_CALL( SCIPsdpiSolverSettingsUsed(sdpi->sdpisolver, &usedsetting) );
         switch( usedsetting )/*lint --e{788}*/
         {
         case SCIP_SDPSOLVERSETTING_FAST:
            *slatersetting = SCIP_SDPSLATERSETTING_STABLENOSLATER;
            break;
         case SCIP_SDPSOLVERSETTING_MEDIUM:
         case SCIP_SDPSOLVERSETTING_STABLE:
            *slatersetting = SCIP_SDPSLATERSETTING_UNSTABLENOSLATER;
            break;
         default:
            *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
            break;
         }
         break;
         case SCIP_SDPSLATER_INF:
            usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
            SCIP_CALL( SCIPsdpiSolverSettingsUsed(sdpi->sdpisolver, &usedsetting) );
            switch( usedsetting )/*lint --e{788}*/
            {
            case SCIP_SDPSOLVERSETTING_FAST:
               *slatersetting = SCIP_SDPSLATERSETTING_STABLEINFEASIBLE;
               break;
            case SCIP_SDPSOLVERSETTING_MEDIUM:
            case SCIP_SDPSOLVERSETTING_STABLE:
               *slatersetting = SCIP_SDPSLATERSETTING_UNSTABLEINFEASIBLE;
               break;
            default:
               *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
               break;
            }
            break;
         case SCIP_SDPSLATER_HOLDS:
            usedsetting = SCIP_SDPSOLVERSETTING_UNSOLVED;
            SCIP_CALL( SCIPsdpiSolverSettingsUsed(sdpi->sdpisolver, &usedsetting) );
            switch( usedsetting )/*lint --e{788}*/
            {
            case SCIP_SDPSOLVERSETTING_FAST:
               *slatersetting = SCIP_SDPSLATERSETTING_STABLEWSLATER;
               break;
            case SCIP_SDPSOLVERSETTING_MEDIUM:
            case SCIP_SDPSOLVERSETTING_STABLE:
               *slatersetting = SCIP_SDPSLATERSETTING_UNSTABLEWSLATER;
               break;
            default:
               *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
               break;
            }
            break;
      default:
         *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
         break;
      }
      break;
   default:
      *slatersetting = SCIP_SDPSLATERSETTING_NOINFO;
      break;
   }

   return SCIP_OKAY;
}

/** returns whether primal and dual Slater condition held for last solved SDP */
SCIP_RETCODE SCIPsdpiSlater(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_SDPSLATER*       primalslater,       /**< pointer to save whether primal Slater condition held */
   SCIP_SDPSLATER*       dualslater          /**< pointer to save whether dual Slater condition held */
   )
{
   assert( sdpi != NULL );
   assert( primalslater != NULL );
   assert( dualslater != NULL );

   if ( sdpi->infeasible )
   {
      *primalslater = SCIP_SDPSLATER_NOINFO;
      *dualslater = sdpi->dualslater;
      return SCIP_OKAY;
   }
   else if ( sdpi->allfixed )
   {
      *primalslater = SCIP_SDPSLATER_NOINFO;
      *dualslater = SCIP_SDPSLATER_NOINFO;
      return SCIP_OKAY;
   }
   else if ( sdpi->solvedonevarsdp > SCIP_ONEVAR_UNSOLVED )
   {
      *primalslater = SCIP_SDPSLATER_NOINFO;
      *dualslater = SCIP_SDPSLATER_NOINFO;
      return SCIP_OKAY;
   }

   *primalslater = sdpi->primalslater;
   *dualslater = sdpi->dualslater;

   return SCIP_OKAY;
}

/** returns some statistcs */
SCIP_RETCODE SCIPsdpiGetStatistics(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int*                  ninfeasible,        /**< pointer to store the total number of times infeasibility was detected in presolving */
   int*                  nallfixed,          /**< pointer to store the total number of times all variables were fixed */
   int*                  nonevarsdp          /**< pointer to store the total number of times a one variable SDP was solved */
   )
{
   assert( sdpi != NULL );
   assert( ninfeasible != NULL );
   assert( nallfixed != NULL );
   assert( nonevarsdp != NULL );

   *ninfeasible = sdpi->ninfeasible;
   *nallfixed = sdpi->nallfixed;
   *nonevarsdp = sdpi->nonevarsdp;

   return SCIP_OKAY;
}

/**@} */




/*
 * Numerical Methods
 */

/**@name Numerical Methods */
/**@{ */

/** returns value treated as infinity in the SDP-solver */
SCIP_Real SCIPsdpiInfinity(
   SCIP_SDPI*            sdpi                /**< SDP-interface structure */
   )
{
   assert( sdpi != NULL  );

   return SCIPsdpiSolverInfinity(sdpi->sdpisolver);
}

/** checks if given value is treated as (plus or minus) infinity in the SDP-solver */
SCIP_Bool SCIPsdpiIsInfinity(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Real             val                 /**< value to be checked for infinity */
   )
{
   assert( sdpi != NULL );

   return ((val <= -SCIPsdpiInfinity(sdpi)) || (val >= SCIPsdpiInfinity(sdpi)));
}

/** gets floating point parameter of SDP-interface */
SCIP_RETCODE SCIPsdpiGetRealpar(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_SDPPARAM         type,               /**< parameter number */
   SCIP_Real*            dval                /**< pointer to store the parameter value */
   )
{
   assert( sdpi != NULL );
   assert( sdpi->sdpisolver != NULL );
   assert( dval != NULL );

   switch( type )/*lint --e{788}*/
   {
   case SCIP_SDPPAR_EPSILON:
      *dval = sdpi->epsilon;
      break;
   case SCIP_SDPPAR_GAPTOL:
      *dval = sdpi->gaptol;
      break;
   case SCIP_SDPPAR_FEASTOL:
      *dval = sdpi->feastol;
      break;
   case SCIP_SDPPAR_SDPSOLVERFEASTOL:
      SCIP_CALL_PARAM( SCIPsdpiSolverGetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_OBJLIMIT:
      SCIP_CALL_PARAM( SCIPsdpiSolverGetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_PENALTYPARAM:
      *dval = sdpi->penaltyparam;
      break;
   case SCIP_SDPPAR_MAXPENALTYPARAM:
      *dval = sdpi->maxpenaltyparam;
      break;
   case SCIP_SDPPAR_LAMBDASTAR:
      SCIP_CALL_PARAM( SCIPsdpiSolverGetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_WARMSTARTPOGAP:
      SCIP_CALL_PARAM( SCIPsdpiSolverGetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_PENINFEASADJUST:
      *dval = sdpi->peninfeasadjust;
      break;
   default:
      return SCIP_PARAMETERUNKNOWN;
   }

   return SCIP_OKAY;
}

/** sets floating point parameter of SDP-interface */
SCIP_RETCODE SCIPsdpiSetRealpar(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_SDPPARAM         type,               /**< parameter number */
   SCIP_Real             dval                /**< parameter value */
   )
{
   assert( sdpi != NULL );

   switch( type )/*lint --e{788}*/
   {
   case SCIP_SDPPAR_EPSILON:
      sdpi->epsilon = dval;
      SCIP_CALL_PARAM( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_GAPTOL:
      sdpi->gaptol = dval;
      SCIP_CALL_PARAM( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_FEASTOL:
      sdpi->feastol = dval;
      SCIP_CALL_PARAM( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_SDPSOLVERFEASTOL:
      SCIP_CALL_PARAM( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_OBJLIMIT:
      SCIP_CALL_PARAM( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_PENALTYPARAM:
      sdpi->penaltyparam = dval;
      SCIP_CALL_PARAM_IGNORE_UNKNOWN( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_MAXPENALTYPARAM:
      sdpi->maxpenaltyparam = dval;
      break;
   case SCIP_SDPPAR_LAMBDASTAR:
      SCIP_CALL_PARAM( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_WARMSTARTPOGAP:
      SCIP_CALL_PARAM( SCIPsdpiSolverSetRealpar(sdpi->sdpisolver, type, dval) );
      break;
   case SCIP_SDPPAR_PENINFEASADJUST:
      sdpi->peninfeasadjust = dval;
      break;
   default:
      return SCIP_PARAMETERUNKNOWN;
   }

   return SCIP_OKAY;
}

/** gets integer parameter of SDP-interface */
SCIP_RETCODE SCIPsdpiGetIntpar(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_SDPPARAM         type,               /**< parameter number */
   int*                  ival                /**< pointer to store the parameter value */
   )
{
   assert( sdpi != NULL );
   assert( sdpi->sdpisolver != NULL );
   assert( ival != NULL );

   switch( type )/*lint --e{788}*/
   {
   case SCIP_SDPPAR_SDPINFO:
   case SCIP_SDPPAR_NTHREADS:
   case SCIP_SDPPAR_USEPRESOLVING:
   case SCIP_SDPPAR_USESCALING:
   case SCIP_SDPPAR_SCALEOBJ:
      SCIP_CALL_PARAM( SCIPsdpiSolverGetIntpar(sdpi->sdpisolver, type, ival) );
      break;
   case SCIP_SDPPAR_SLATERCHECK:
      *ival = sdpi->slatercheck;
      break;
   case SCIP_SDPPAR_NPENALTYINCR:
      *ival = sdpi->npenaltyincr;
      break;
   default:
      return SCIP_PARAMETERUNKNOWN;
   }

   return SCIP_OKAY;
}

/** sets integer parameter of SDP-interface */
SCIP_RETCODE SCIPsdpiSetIntpar(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_SDPPARAM         type,               /**< parameter number */
   int                   ival                /**< parameter value */
   )
{
   assert( sdpi != NULL );
   assert( sdpi->sdpisolver != NULL );

   switch( type )/*lint --e{788}*/
   {
   case SCIP_SDPPAR_SDPINFO:
   case SCIP_SDPPAR_USEPRESOLVING:
   case SCIP_SDPPAR_USESCALING:
   case SCIP_SDPPAR_SCALEOBJ:
      assert( ival == 0 || ival == 1 ); /* this is a boolean parameter */
      SCIP_CALL_PARAM( SCIPsdpiSolverSetIntpar(sdpi->sdpisolver, type, ival) );
      break;
   case SCIP_SDPPAR_NTHREADS:
      SCIP_CALL_PARAM( SCIPsdpiSolverSetIntpar(sdpi->sdpisolver, type, ival) );
      break;
   case SCIP_SDPPAR_SLATERCHECK:
      sdpi->slatercheck = ival;
      break;
   case SCIP_SDPPAR_NPENALTYINCR:
      sdpi->npenaltyincr = ival;
      break;
   default:
      return SCIP_PARAMETERUNKNOWN;
   }

   return SCIP_OKAY;
}

/** compute and set lambdastar (only used for SDPA) */
SCIP_RETCODE SCIPsdpiComputeLambdastar(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Real             maxguess            /**< maximal guess for lambda star of all SDP-constraints */
   )
{
   return SCIPsdpiSolverComputeLambdastar(sdpi->sdpisolver, maxguess);
}

/** compute and set the penalty parameter */
SCIP_RETCODE SCIPsdpiComputePenaltyparam(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Real             maxcoeff,           /**< maximal objective coefficient */
   SCIP_Real*            penaltyparam        /**< the computed penalty parameter */
   )
{
   SCIP_CALL( SCIPsdpiSolverComputePenaltyparam(sdpi->sdpisolver, maxcoeff, penaltyparam) );

   sdpi->penaltyparam = *penaltyparam;

   return SCIP_OKAY;
}

/** compute and set the maximal penalty parameter */
SCIP_RETCODE SCIPsdpiComputeMaxPenaltyparam(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   SCIP_Real             penaltyparam,       /**< the initial penalty parameter */
   SCIP_Real*            maxpenaltyparam     /**< the computed maximal penalty parameter */
   )
{
   SCIP_CALL( SCIPsdpiSolverComputeMaxPenaltyparam(sdpi->sdpisolver, penaltyparam, maxpenaltyparam) );

   sdpi->maxpenaltyparam = *maxpenaltyparam;

   /* if the initial penalty parameter is smaller than the maximal one, we decrease the initial correspondingly */
   /* if the maximal penalty parameter is smaller than the initial penalty paramater, we decrease the initial one correspondingly */
   if ( sdpi->penaltyparam > *maxpenaltyparam )
   {
      SCIPdebugMessage("Decreasing penaltyparameter of %g to maximal penalty paramater of %g.\n", sdpi->penaltyparam, *maxpenaltyparam);
      sdpi->penaltyparam = *maxpenaltyparam;
   }

   return SCIP_OKAY;
}

/** sets the type of the clock */
void SCIPsdpiClockSetType(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   int                   clocktype           /**< type of clock (1 = CPU, 2 = Wall) */
   )
{
   assert( sdpi != NULL );
   assert( clocktype == 1 || clocktype == 2 );
   assert( sdpi->usedsdpitime != NULL );

   SDPIclockSetType(sdpi->usedsdpitime, (SDPI_CLOCKTYPE) clocktype);
}

/**@} */




/*
 * File Interface Methods
 */

/**@name File Interface Methods */
/**@{ */

/** reads SDP from a file */
SCIP_RETCODE SCIPsdpiReadSDP(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   const char*           fname               /**< file name */
   )
{
   assert( sdpi != NULL );
   assert( fname != NULL );

   SCIPerrorMessage("Not implemented yet.\n");

   return SCIP_NOTIMPLEMENTED;
}

/** writes SDP to a file */
SCIP_RETCODE SCIPsdpiWriteSDP(
   SCIP_SDPI*            sdpi,               /**< SDP-interface structure */
   const char*           fname               /**< file name */
   )
{
   assert( sdpi != NULL );
   assert( fname != NULL );

   SCIPerrorMessage("Not implemented yet.\n");

   return SCIP_NOTIMPLEMENTED;
}

/**@} */
