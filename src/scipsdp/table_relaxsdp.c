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

/**@file   table_relaxsdp.c
 * @brief  advanced SDP relaxator statistics table
 * @author Tristan Gally
 * @author Marc Pfetsch
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>

#include "table_relaxsdp.h"
#include "relax_sdp.h"


#define TABLE_NAME              "relaxsdp"
#define TABLE_DESC              "advanced SDP relaxator statistics table"
#define TABLE_ACTIVE            TRUE
#define TABLE_POSITION          17100              /**< the position of the statistics table */
#define TABLE_EARLIEST_STAGE    SCIP_STAGE_SOLVING /**< output of the statistics table is only printed from this stage onwards */


/*
 * Data structures
 */

/** statistics table data */
struct SCIP_TableData
{
   SCIP_RELAX*           relaxSDP;           /**< pointer to the SDP relaxator whose iterations should be displayed */
   SCIP_Bool             absolute;           /**< Should statistics be printed in absolute numbers (true) or percentages (false)? */
};


/*
 * Callback methods of statistics table
 */

/** copy method for statistics table plugins (called when SCIP copies plugins) */
static
SCIP_DECL_TABLECOPY(tableCopyRelaxSdp)
{  /*lint --e{715}*/
   assert( scip != NULL );
   assert( table != NULL );

   SCIP_CALL( SCIPincludeTableRelaxSdp(scip) );

   return SCIP_OKAY;
}


/** destructor of statistics table to free user data (called when SCIP is exiting) */
static
SCIP_DECL_TABLEFREE(tableFreeRelaxSdp)
{  /*lint --e{715}*/
   SCIP_TABLEDATA* tabledata;

   assert( scip != NULL );
   assert( table != NULL );
   tabledata = SCIPtableGetData(table);
   assert( tabledata != NULL );

   SCIPfreeMemory(scip, &tabledata);
   SCIPtableSetData(table, NULL);

   return SCIP_OKAY;
}


/** solving process initialization method of statistics table (called when branch and bound process is about to begin) */
static
SCIP_DECL_TABLEINITSOL(tableInitsolRelaxSdp)
{  /*lint --e{715}*/
   SCIP_TABLEDATA* tabledata;

   assert( table != NULL );
   tabledata = SCIPtableGetData(table);
   assert( tabledata != NULL );

   tabledata->relaxSDP = SCIPfindRelax(scip, "SDP");
   assert( tabledata->relaxSDP != NULL );

   return SCIP_OKAY;
}


/** output method of statistics table to output file stream 'file' */
static
SCIP_DECL_TABLEOUTPUT(tableOutputRelaxSdp)
{  /*lint --e{715}*/
   SCIP_TABLEDATA* tabledata;
   SCIP_RELAX* relaxsdp;
   int nsdpcalls;
   int nintercalls;
   int ninfeasible;
   int nallfixed;
   int nonevarsdp;

   assert( scip != NULL );
   assert( table != NULL );

   tabledata = SCIPtableGetData(table);
   assert( tabledata != NULL );

   relaxsdp = tabledata->relaxSDP;
   assert( relaxsdp != NULL );

   SCIP_CALL( SCIPrelaxSdpGetStatistics(relaxsdp, &ninfeasible, &nallfixed, &nonevarsdp) );
   nintercalls = SCIPrelaxSdpGetNSdpInterfaceCalls(relaxsdp);
   nsdpcalls = SCIPrelaxSdpGetNSdpCalls(relaxsdp);

   if ( strcmp(SCIPsdpiGetSolverName(), "SDPA") == 0 )
   {
      SCIPinfoMessage(scip, file, "    SDP-Solvers    :       Time    Opttime     Solves Iterations  Iter/call       Fast     Medium     Stable    Penalty   Unsolved     Infeas   Allfixed  OnevarSDP\n");
      if ( nintercalls > 0 )
      {
         if ( tabledata->absolute )
         {
            if ( nsdpcalls > 0 )
            {
               SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10d %10d %10.2f %10d %10d %10d %10d %10d %10d %10d %10d\n",
                  SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
                  nsdpcalls, SCIPrelaxSdpGetNIterations(relaxsdp), (SCIP_Real) SCIPrelaxSdpGetNIterations(relaxsdp) / (SCIP_Real) nsdpcalls,
                  SCIPrelaxSdpGetNSdpFast(relaxsdp), SCIPrelaxSdpGetNSdpMedium(relaxsdp), SCIPrelaxSdpGetNSdpStable(relaxsdp), SCIPrelaxSdpGetNSdpPenalty(relaxsdp),
                  SCIPrelaxSdpGetNSdpUnsolved(relaxsdp), ninfeasible, nallfixed, nonevarsdp);
            }
            else
            {
               SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10d %10d %10s %10d %10d %10d %10d %10d %10d %10d %10d\n",
                  SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
                  nsdpcalls, SCIPrelaxSdpGetNIterations(relaxsdp), "-",
                  SCIPrelaxSdpGetNSdpFast(relaxsdp), SCIPrelaxSdpGetNSdpMedium(relaxsdp), SCIPrelaxSdpGetNSdpStable(relaxsdp), SCIPrelaxSdpGetNSdpPenalty(relaxsdp),
                  SCIPrelaxSdpGetNSdpUnsolved(relaxsdp), ninfeasible, nallfixed, nonevarsdp);
            }
         }
         else
         {
            if ( nsdpcalls > 0 )
            {
               SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10d %10d %10.2f %8.2f %% %8.2f %% %8.2f %% %8.2f %% %8.2f %% %10d %10d %10d\n",
                  SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
                  nsdpcalls, SCIPrelaxSdpGetNIterations(relaxsdp), (SCIP_Real) SCIPrelaxSdpGetNIterations(relaxsdp) / (SCIP_Real) nsdpcalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpFast(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpMedium(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpStable(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpPenalty(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpUnsolved(relaxsdp) / (SCIP_Real) nintercalls,
                  ninfeasible, nallfixed, nonevarsdp);
            }
            else
            {
               SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10d %10d %10s %8.2f %% %8.2f %% %8.2f %% %8.2f %% %8.2f %% %10d %10d %10d\n",
                  SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
                  nsdpcalls, SCIPrelaxSdpGetNIterations(relaxsdp), "-",
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpFast(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpMedium(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpStable(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpPenalty(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpUnsolved(relaxsdp) / (SCIP_Real) nintercalls,
                  ninfeasible, nallfixed, nonevarsdp);

            }
         }
      }
      else
      {
         SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s\n",
            SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
            "-", "-", "-", "-", "-", "-", "-", "-", "-", "-", "-");
      }
   }
   else
   {
      SCIPinfoMessage(scip, file, "    SDP-Solvers    :       Time    Opttime     Solves Iterations  Iter/call    Default    Penalty   Unsolved     Infeas   Allfixed  OnevarSDP\n");
      if ( nintercalls > 0 )
      {
         if ( tabledata->absolute )
         {
            if ( nsdpcalls > 0 )
            {
               SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10d %10d %10.2f %10d %10d %10d %10d %10d %10d\n",
                  SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
                  nsdpcalls, SCIPrelaxSdpGetNIterations(relaxsdp), (SCIP_Real) SCIPrelaxSdpGetNIterations(relaxsdp) / (SCIP_Real) nsdpcalls,
                  SCIPrelaxSdpGetNSdpFast(relaxsdp), SCIPrelaxSdpGetNSdpPenalty(relaxsdp), SCIPrelaxSdpGetNSdpUnsolved(relaxsdp),
                  ninfeasible, nallfixed, nonevarsdp);
            }
            else
            {
               SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10d %10d %10s %10d %10d %10d %10d %10d %10d\n",
                  SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
                  nsdpcalls, SCIPrelaxSdpGetNIterations(relaxsdp), "-",
                  SCIPrelaxSdpGetNSdpFast(relaxsdp), SCIPrelaxSdpGetNSdpPenalty(relaxsdp), SCIPrelaxSdpGetNSdpUnsolved(relaxsdp),
                  ninfeasible, nallfixed, nonevarsdp);
            }
         }
         else
         {
            if ( nsdpcalls > 0 )
            {
               SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10d %10d %10.2f %8.2f %% %8.2f %% %8.2f %% %10d %10d %10d\n",
                  SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
                  nsdpcalls, SCIPrelaxSdpGetNIterations(relaxsdp), (SCIP_Real) SCIPrelaxSdpGetNIterations(relaxsdp) / (SCIP_Real) nsdpcalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpFast(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpPenalty(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpUnsolved(relaxsdp) / (SCIP_Real) nintercalls,
                  ninfeasible, nallfixed, nonevarsdp);
            }
            else
            {
               SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10d %10d %10s %8.2f %% %8.2f %% %8.2f %% %10d %10d %10d\n",
                  SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
                  nsdpcalls, SCIPrelaxSdpGetNIterations(relaxsdp), "-",
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpFast(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpPenalty(relaxsdp) / (SCIP_Real) nintercalls,
                  100.0 * (SCIP_Real) SCIPrelaxSdpGetNSdpUnsolved(relaxsdp) / (SCIP_Real) nintercalls,
                  ninfeasible, nallfixed, nonevarsdp);
            }
         }
      }
      else
      {
         SCIPinfoMessage(scip, file, "     %-14.14s: %10.2f %10.2f %10s %10s %10s %10s %10s %10s %10s %10s %10s %10s\n",
            SCIPsdpiGetSolverName(), SCIPrelaxSdpGetSolvingTime(scip, relaxsdp), SCIPrelaxSdpGetOptTime(relaxsdp),
            "-", "-", "-", "-", "-", "-", "-", "-", "-", "-");
      }
   }

   return SCIP_OKAY;
}


/*
 * statistics table specific interface methods
 */

/** creates the advanced SDP relaxator statistics table and includes it in SCIP */
SCIP_RETCODE SCIPincludeTableRelaxSdp(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_TABLEDATA* tabledata;

   assert( scip != NULL );

   /* create statistics table data */
   SCIP_CALL( SCIPallocMemory(scip, &tabledata) );

   /* include statistics table */
   SCIP_CALL( SCIPincludeTable(scip, TABLE_NAME, TABLE_DESC, TABLE_ACTIVE,
         tableCopyRelaxSdp, tableFreeRelaxSdp, NULL, NULL,
         tableInitsolRelaxSdp, NULL, tableOutputRelaxSdp,
         tabledata, TABLE_POSITION, TABLE_EARLIEST_STAGE) );

   /* add "absolute" parameter */
   SCIP_CALL( SCIPaddBoolParam( scip, "table/relaxsdp/absolute", "Should statistics be printed in absolute numbers (true) or percentages (false)?",
         &(tabledata->absolute), FALSE, TRUE, NULL, NULL) );

   return SCIP_OKAY;
}
