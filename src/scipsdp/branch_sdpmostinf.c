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

/**@file   branch_sdpmostinf.c
 * @brief  most infeasible branching rule for SCIP-SDP
 * @author Tristan Gally
 *
 * Branch on the most infeasible variable in the current SDP-relaxation, i.e. the variable maximizing \f$\min\{x - \lfloor x \rfloor, \lceil x \rceil - x\} \f$.
 *
 * Will do nothing for continuous variables, since these are what the external callbacks of the SCIP branching rules are for.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

/*#define SCIP_DEBUG*/

#include <assert.h>
#include <string.h>

#include "branch_sdpmostinf.h"

/* turn off lint warnings for whole file: */
/*lint --e{788,818}*/

#define BRANCHRULE_NAME            "sdpmostinf"
#define BRANCHRULE_DESC            "branch on the most infeasible variable of the SDP"
#define BRANCHRULE_PRIORITY        1000000
#define BRANCHRULE_MAXDEPTH        -1
#define BRANCHRULE_MAXBOUNDDIST    1.0


/*
 * Data structures
 */

/*
 * Local methods
 */

/* put your local methods here, and declare them static */


/*
 * Callback methods of branching rule
 */

/** copy method for branchrule plugins (called when SCIP copies plugins) */
static
SCIP_DECL_BRANCHCOPY(branchCopySdpmostinf)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(branchrule != NULL);
   assert(strcmp(SCIPbranchruleGetName(branchrule), BRANCHRULE_NAME) == 0);

   /* call inclusion method of branchrule */
   SCIP_CALL( SCIPincludeBranchruleSdpmostinf(scip) );

   return SCIP_OKAY;
}

/** branching execution method for external candidates */
static
SCIP_DECL_BRANCHEXECEXT(branchExecextSdpmostinf)
{/*lint --e{715}*/
   int i;
   int ncands;
   SCIP_VAR** cands = NULL;
   SCIP_Real* candssol; /* solution values of all candidates */
   SCIP_Real* candsscore; /* scores of all candidates */
   SCIP_Real currentfrac; /* fractionality of the current candidate */
   SCIP_Real currentinf; /* infeasibility of the current candidate */
   SCIP_Real mostinfinf; /* infeasibility of the current most infeasible variable */
   SCIP_Real mostinfscore; /* score of the current most infeasible variable */
   SCIP_Real mostinfobj; /* objective of the current most infeasible variable */
   SCIP_Real mostinfval; /* value of the current most infeasible variable */
   SCIP_VAR* mostinfvar = NULL; /* variable with the highest current infeasibility */

   assert( scip != NULL );
   assert( result != NULL );

   SCIPdebugMsg(scip, "Executing External Branching method of SDP-mostinf!\n");

   /* get the external candidates, as we use the score only as a tiebreaker, we aren't interested in the number of
    * variables of different types with maximal score, so these return values are set to NULL */
   SCIP_CALL( SCIPgetExternBranchCands(scip, &cands, &candssol, &candsscore, &ncands, NULL, NULL, NULL, NULL) );

   assert( ncands > 0 ); /* branchExecext should only be called if the list of external branching candidates is non-empty */

#ifdef SCIP_DEBUG
   SCIPdebugMsg(scip, "branching candidates for SDP-mostinf:\n");
   for (i = 0; i < ncands; i++)
      SCIPdebugMsg(scip, "%s, value = %f, score = %f\n", SCIPvarGetName(cands[i]), candssol[i], candsscore[i]);
#endif

   mostinfinf = -1.0;
   mostinfscore = 0.0;
   mostinfval = 0.0;
   mostinfobj = -1.0;

   /* iterate over all solution candidates to find the one with the highest infeasibility */
   for (i = 0; i < ncands; i++)
   {
      /* we skip all continuous variables, since we first want to branch on integral variables */
      if ( SCIPvarGetType(cands[i]) == SCIP_VARTYPE_CONTINUOUS )
      {
         SCIPdebugMsg(scip, "skipping continuous variable %s\n", SCIPvarGetName(cands[i]));
         continue;
      }

      currentfrac = SCIPfeasFrac(scip, candssol[i]);
      currentinf = (currentfrac <= 0.5) ? currentfrac : 1 - currentfrac;
      /* a candidate is better than the current one if:
       * - the infeasibility is (feastol-)bigger than before or
       * - the infeasibility is (feastol-)equal and the score is (epsilon-)bigger or
       * - the infeasibility and score are (feastol-/epsilon-)equal and the objective is (epsilon-)bigger than before
       * - add three above are (feastol-/epsilon-)equal and the index is smaller */
      if ( SCIPisFeasGT(scip, currentinf, mostinfinf) ||
          (SCIPisFeasEQ(scip, currentinf, mostinfinf) && SCIPisGT(scip, candsscore[i], mostinfscore)) ||
          (SCIPisFeasEQ(scip, currentinf, mostinfinf) && SCIPisEQ(scip, candsscore[i], mostinfscore) && SCIPisGT(scip, SCIPvarGetObj(cands[i]), mostinfobj)) ||
          (SCIPisFeasEQ(scip, currentinf, mostinfinf) && SCIPisEQ(scip, candsscore[i], mostinfscore) && SCIPisEQ(scip, SCIPvarGetObj(cands[i]), mostinfobj) &&
                SCIPvarGetIndex(cands[i]) < SCIPvarGetIndex(mostinfvar)) )
      {
         /* update the current best candidate */
         mostinfinf = currentinf;
         mostinfval = candssol[i];
         mostinfobj = REALABS(SCIPvarGetObj(cands[i]));
         mostinfscore = candsscore[i];
         mostinfvar = cands[i];
      }
   }

   /* if all variables were continuous, we return DIDNOTRUN and let one of the SCIP branching rules decide */
   if ( mostinfinf == -1.0 )
   {
      SCIPdebugMsg(scip, "Skipping SDP-mostinf branching rule since all branching variables are continuous\n");
      *result = SCIP_DIDNOTFIND;
      return SCIP_OKAY;
   }

   assert( mostinfvar != NULL );
   assert( SCIPisFeasGT(scip, mostinfinf, 0.0) ); /* otherwise all variables are fixed and there is nothing to branch */

   /* branch */
   SCIPdebugMsg(scip, "branching on variable %s with value %f and score %f\n", SCIPvarGetName(mostinfvar), mostinfval, mostinfscore);
   SCIP_CALL( SCIPbranchVarVal(scip, mostinfvar, mostinfval, NULL, NULL, NULL) );

   *result = SCIP_BRANCHED;

   return SCIP_OKAY;
}

/*
 * branching rule specific interface methods
 */

/** creates the SDP most infeasible branching rule and includes it in SCIP */
SCIP_RETCODE SCIPincludeBranchruleSdpmostinf(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_BRANCHRULEDATA* branchruledata;
   SCIP_BRANCHRULE* branchrule;

   /* create empty branching rule data */
   branchruledata = NULL;

   branchrule = NULL;

   /* include branching rule */
   SCIP_CALL( SCIPincludeBranchruleBasic(scip, &branchrule, BRANCHRULE_NAME, BRANCHRULE_DESC, BRANCHRULE_PRIORITY,
         BRANCHRULE_MAXDEPTH, BRANCHRULE_MAXBOUNDDIST, branchruledata) );

   assert(branchrule != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetBranchruleCopy(scip, branchrule, branchCopySdpmostinf) );
   SCIP_CALL( SCIPsetBranchruleExecExt(scip, branchrule, branchExecextSdpmostinf) );

   return SCIP_OKAY;
}
