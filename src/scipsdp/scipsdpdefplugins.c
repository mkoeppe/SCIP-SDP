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

/**@file   scipsdpdefplugins.c
 * @brief  default SCIP-SDP plugins
 * @author Marc Pfetsch
 */

/*--+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "scipsdp/scipsdpdef.h"


#include "scipsdp/scipsdpdefplugins.h"
#include "scip/scipdefplugins.h"
#include "scip/scipshell.h"

#include "cons_sdp.h"
#include "scipsdp/cons_savesdpsol.h"
#include "cons_savedsdpsettings.h"
#include "relax_sdp.h"
#include "reader_cbf.h"
#include "reader_sdpa.h"
#include "prop_sdpredcost.h"
#include "disp_sdpiterations.h"
#include "disp_sdpavgiterations.h"
#include "disp_sdpfastsettings.h"
#include "disp_sdppenalty.h"
#include "disp_sdpunsolved.h"
#include "branch_sdpmostfrac.h"
#include "branch_sdpmostinf.h"
#include "branch_sdpobjective.h"
#include "branch_sdpinfobjective.h"
#include "heur_sdpfracdiving.h"
#include "heur_sdpfracround.h"
#include "heur_sdpinnerlp.h"
#include "heur_sdprand.h"
#include "prop_sdpobbt.h"
#include "prop_sdpsymmetry.h"
#include "prop_companalcent.h"
#include "scipsdpgithash.c"
#include "table_relaxsdp.h"
#include "table_slater.h"

/* hack to allow to change the name of the dialog without needing to copy everything */
#include "scip/struct_dialog.h"

/* hack to change default parameter values*/
#include "scip/struct_paramset.h"

#define SCIPSDP_DEFAULT_READ_REMOVESMALLVAL  TRUE      /**< Should small values in the constraints be removed when reading CBF or SDPA-files? */

/* The functions SCIPparamSetDefaultBool() and SCIPparamSetDefaultInt() are internal functions of SCIP. To nevertheless
 * change the default parameters, we add our own locate methods below. */

/** local function to change default value of SCIP_Bool parameter */
static
void paramSetDefaultBool(
   SCIP_PARAM*           param,              /**< parameter */
   SCIP_Bool             defaultvalue        /**< new default value */
   )
{
   assert(param != NULL);
   assert(param->paramtype == SCIP_PARAMTYPE_BOOL);

   param->data.boolparam.defaultvalue = defaultvalue;
}

/** local function to change default value of int parameter */
static
void paramSetDefaultInt(
   SCIP_PARAM*           param,              /**< parameter */
   int                   defaultvalue        /**< new default value */
   )
{
   assert(param != NULL);
   assert(param->paramtype == SCIP_PARAMTYPE_INT);

   assert(param->data.intparam.minvalue <= defaultvalue && param->data.intparam.maxvalue >= defaultvalue);

   param->data.intparam.defaultvalue = defaultvalue;
}

/** local function to change default value of real parameter */
static
void paramSetDefaultReal(
   SCIP_PARAM*           param,              /**< parameter */
   SCIP_Real             defaultvalue        /**< new default value */
   )
{
   assert(param != NULL);
   assert(param->paramtype == SCIP_PARAMTYPE_REAL);

   assert(param->data.realparam.minvalue <= defaultvalue && param->data.realparam.maxvalue >= defaultvalue);

   param->data.realparam.defaultvalue = defaultvalue;
}


/** reset some default parameter values */
static
SCIP_RETCODE SCIPSDPsetDefaultParams(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_PARAM* param;

   /* change default feastol and dualfeastol */
   param = SCIPgetParam(scip, "numerics/feastol");
   paramSetDefaultReal(param, 1e-5);

   param = SCIPgetParam(scip, "numerics/dualfeastol");
   paramSetDefaultReal(param, 1e-5);

   /* turn off LP solving - note that the SDP relaxator is on by default */
   param = SCIPgetParam(scip, "lp/solvefreq");
   paramSetDefaultInt(param, -1);

   param = SCIPgetParam(scip, "lp/cleanuprows");
   paramSetDefaultBool(param, FALSE);

   param = SCIPgetParam(scip, "lp/cleanuprowsroot");
   paramSetDefaultBool(param, FALSE);

   /* Because in the SDP-world there are no warmstarts as for LPs, the main advantage for DFS (that the change in the
    * problem is minimal and therefore the Simplex can continue with the current Basis) is lost and best first search, which
    * provably needs the least number of nodes (see the Dissertation of Tobias Achterberg, the node selection rule with
    * the least number of nodes, allways has to be a best first search), is the optimal choice
    */
   param = SCIPgetParam(scip, "nodeselection/hybridestim/stdpriority");
   paramSetDefaultInt(param, 1000000);

   param = SCIPgetParam(scip, "nodeselection/hybridestim/maxplungedepth");
   paramSetDefaultInt(param, 0);

   param = SCIPgetParam(scip, "nodeselection/hybridestim/estimweight");
   paramSetDefaultReal(param, 0.0);

   /* change display */
   param = SCIPgetParam(scip, "display/lpiterations/active");
   paramSetDefaultInt(param, 0);

   param = SCIPgetParam(scip, "display/lpavgiterations/active");
   paramSetDefaultInt(param, 0);

   param = SCIPgetParam(scip, "display/nfrac/active");
   paramSetDefaultInt(param, 0);

   param = SCIPgetParam(scip, "display/curcols/active");
   paramSetDefaultInt(param, 0);

   param = SCIPgetParam(scip, "display/strongbranchs/active");
   paramSetDefaultInt(param, 0);

   /* oneopt might run into an infinite loop during SDP-solving */
   param = SCIPgetParam(scip, "heuristics/oneopt/freq");
   paramSetDefaultInt(param, -1);

   /* deactivate conflict analysis by default, since it has no effect for SDP solving and a negative influence on LP-solving */
   param = SCIPgetParam(scip, "conflict/enable");
   paramSetDefaultBool(param, FALSE);

   /* turn off symmetry handling of default SCIP because it is superseeded by the local one */
   param = SCIPgetParam(scip, "misc/usesymmetry");
   paramSetDefaultInt(param, 0);

   /* now set parameters to their default value */
   SCIP_CALL( SCIPresetParams(scip) );

   SCIP_CALL( SCIPaddBoolParam(scip, "reading/removesmallval",
         "Should small values in the constraints be removed when reading CBF or SDPA-files?",
         NULL, FALSE, SCIPSDP_DEFAULT_READ_REMOVESMALLVAL, NULL, NULL) );

   return SCIP_OKAY;
}


/** includes default SCIP-SDP plugins */
SCIP_RETCODE SCIPSDPincludeDefaultPlugins(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   char scipsdpname[SCIP_MAXSTRLEN];
   char scipsdpdesc[SCIP_MAXSTRLEN];
   SCIP_DIALOG* dialog;

   /* add description */
   (void) SCIPsnprintf(scipsdpname, SCIP_MAXSTRLEN, "SCIP-SDP %d.%d.%d", SCIPSDPmajorVersion, SCIPSDPminorVersion, SCIPSDPtechVersion); /*lint !e169, !e778*/
   (void) SCIPsnprintf(scipsdpdesc, SCIP_MAXSTRLEN, "Mixed Integer Semidefinite Programming Plugin for SCIP "
         "[GitHash: %s] (www.opt.tu-darmstadt.de/scipsdp/)", SCIPSDP_GITHASH);
   SCIP_CALL( SCIPincludeExternalCodeInformation(scip, scipsdpname, scipsdpdesc) );

   /* include default SCIP plugins */
   SCIP_CALL( SCIPincludeDefaultPlugins(scip) );

   /* change default parameter settings */
   SCIP_CALL( SCIPSDPsetDefaultParams(scip) );

   /* include new plugins */
   SCIP_CALL( SCIPincludeReaderCbf(scip) );
   SCIP_CALL( SCIPincludeReaderSdpa(scip) );
   SCIP_CALL( SCIPincludeConshdlrSdp(scip) );
   SCIP_CALL( SCIPincludeConshdlrSdpRank1(scip) );
   SCIP_CALL( SCIPincludeConshdlrSavesdpsol(scip) );
   SCIP_CALL( SCIPincludeConshdlrSavedsdpsettings(scip) );
   SCIP_CALL( SCIPincludeRelaxSdp(scip) );
   SCIP_CALL( SCIPincludePropSdpredcost(scip) );
   SCIP_CALL( SCIPincludeBranchruleSdpmostfrac(scip) );
   SCIP_CALL( SCIPincludeBranchruleSdpmostinf(scip) );
   SCIP_CALL( SCIPincludeBranchruleSdpobjective(scip) );
   SCIP_CALL( SCIPincludeBranchruleSdpinfobjective(scip) );
   SCIP_CALL( SCIPincludeHeurSdpFracdiving(scip) );
   SCIP_CALL( SCIPincludeHeurSdpFracround(scip) );
   SCIP_CALL( SCIPincludeHeurSdpInnerlp(scip) );
   SCIP_CALL( SCIPincludeHeurSdpRand(scip) );
   SCIP_CALL( SCIPincludePropSdpObbt(scip) );
   SCIP_CALL( SCIPincludePropSdpSymmetry(scip) );
   SCIP_CALL( SCIPincludePropCompAnalCent(scip) );

   /* change name of dialog */
   dialog = SCIPgetRootDialog(scip);
   BMSfreeMemoryArrayNull(&dialog->name);
   SCIP_ALLOC( BMSallocMemoryArray(&dialog->name, 9) );
   (void) SCIPstrncpy(dialog->name, "SCIP-SDP", 9);

   /* include displays */
   SCIP_CALL( SCIPincludeDispSdpiterations(scip) );
   SCIP_CALL( SCIPincludeDispSdpavgiterations(scip) );
   SCIP_CALL( SCIPincludeDispSdpfastsettings(scip) );
   SCIP_CALL( SCIPincludeDispSdppenalty(scip) );
   SCIP_CALL( SCIPincludeDispSdpunsolved(scip) );

   /* include tables */
   SCIP_CALL( SCIPincludeTableRelaxSdp(scip) );
   SCIP_CALL( SCIPincludeTableSlater(scip) );

   return SCIP_OKAY;
}
