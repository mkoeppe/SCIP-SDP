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

/**@file   SdpVarmapper.c
 * @brief  maps SCIP variables to SDP indices (the SCIP variables are given SDP indices in the order in which they were inserted)
 * @author Tristan Gally
 */

#include "scip/scip.h"
#include "scip/type_misc.h" /* for SCIP Hashmap */
#include "SdpVarmapper.h"

/* check SCIP version */
#if ( SCIP_VERSION < 702 )
#error Need SCIP version at least 7.0.2.
#endif

/* turn off lint warnings for whole file: */
/*lint --e{788,818}*/

struct Sdpvarmapper
{
   SCIP_VAR**            sdptoscip;          /**< array of SCIP variables indexed by their SDP indices */
   SCIP_HASHMAP*         sciptosdp;          /**< hashmap that maps SCIP variables to their SDP indices */
   int                   nvars;              /**< number of variables saved in this varmapper */
};

/** creates the SDP varmapper */
SCIP_RETCODE SCIPsdpVarmapperCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper**        varmapper,          /**< Pointer to the varmapper that should be created */
   int                   size                /**< initial size of the sciptosdp-hashmap */
   )
{
   assert ( scip != NULL );
   assert ( varmapper != NULL );
   assert ( size >= 0 );

   SCIP_CALL( SCIPallocBlockMemory(scip, varmapper) );
   (*varmapper)->nvars = 0;
   (*varmapper)->sdptoscip = NULL;

   if ( size == 0 )
   {
      SCIPdebugMsg(scip, "SCIPsdpVarmapperCreate called for size 0!\n");

      return SCIP_OKAY;
   }

   SCIP_CALL( SCIPhashmapCreate(&((*varmapper)->sciptosdp), SCIPblkmem(scip), size) );

   return SCIP_OKAY;
}

/** frees the SDP varmapper */
SCIP_RETCODE SCIPsdpVarmapperFree(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper**        varmapper           /**< Pointer to the varmapper that should be freed */
   )
{
   int i;

   SCIPdebugMsg(scip, "Freeing SdpVarmapper \n");

   assert ( scip != NULL );
   assert ( varmapper != NULL );

   /* release all vars */
   for (i = 0; i < (*varmapper)->nvars; i++)
   {
      SCIP_CALL( SCIPreleaseVar(scip, &((*varmapper)->sdptoscip[i])) );
   }

   if ( (*varmapper)->nvars )
      SCIPhashmapFree(&((*varmapper)->sciptosdp));

   SCIPfreeBlockMemoryArrayNull(scip, &(*varmapper)->sdptoscip, (*varmapper)->nvars);
   SCIPfreeBlockMemory(scip, varmapper);

   return SCIP_OKAY;
}

/** adds the given variables (if not already existent) to the end of the varmapper */
SCIP_RETCODE SCIPsdpVarmapperAddVars(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         varmapper,          /**< varmapper to add variables to */
   int                   nvars,              /**< number of variables to add to the varmapper */
   SCIP_VAR**            vars                /**< SCIP variables to add to the varmapper */
   )
{  /*lint --e{818}*/
   int i;
   SCIP_Bool reallocneeded; /* we allocate memory to add nvars variables, but if some of them already existed in the varmapper, we don't add them and
                             * should reallocate later */
   int allocsize;

   if ( nvars == 0 )
      return SCIP_OKAY;

   assert ( scip != NULL );
   assert ( varmapper != NULL );
   assert ( nvars >= 0 );
   assert ( vars != NULL );

   allocsize = varmapper->nvars + nvars;
   SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &(varmapper->sdptoscip), varmapper->nvars, allocsize) );

   reallocneeded = FALSE;

   for (i = 0; i < nvars; i++)
   {
      if ( ! (SCIPhashmapExists(varmapper->sciptosdp, vars[i])) ) /* make sure, that there are no duplicates in the lists */
      {
         varmapper->sdptoscip[varmapper->nvars] = vars[i];
         SCIP_CALL( SCIPhashmapInsertInt(varmapper->sciptosdp, (void*) vars[i], varmapper->nvars) );
         varmapper->nvars++;
         SCIP_CALL( SCIPcaptureVar(scip, vars[i]) );
      }
      else
      {
         SCIPdebugMsg(scip, "variable %s was not added to the varmapper as it was already part of it \n", SCIPvarGetName(vars[i]));
         reallocneeded = TRUE;
      }
   }

   if ( reallocneeded )
   {
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &(varmapper->sdptoscip), allocsize, varmapper->nvars) );
   }

   return SCIP_OKAY;
}

/** adds the given variable (if not already existent) to the varmapper at the given position */
SCIP_RETCODE SCIPsdpVarmapperInsertVar(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         varmapper,          /**< varmapper to add variable to */
   SCIP_VAR*             var,                /**< SCIP variable to add to the varmapper */
   int                   pos                 /**< position where the variable should be added */
   )
{
   int i;

   assert ( scip != NULL );
   assert ( varmapper != NULL );
   assert ( var != NULL );
   assert ( pos >= 0 );
   assert ( pos <= varmapper->nvars );

   if ( ! SCIPhashmapExists(varmapper->sciptosdp, var) ) /* make sure, that there are no duplicates in the lists */
   {
      if ( pos == varmapper->nvars )   /* add it to the end */
      {
         SCIP_CALL(SCIPsdpVarmapperAddVars(scip, varmapper, 1, &var));
      }
      else
      {
         SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &varmapper->sdptoscip, varmapper->nvars, varmapper->nvars + 1) );

         /* move all variables after pos one spot to the right to make room for the new one */
         for (i = varmapper->nvars - 1; i >= pos; i--)
         {
            varmapper->sdptoscip[i + 1] = varmapper->sdptoscip[i]; /*lint !e679*/
            SCIP_CALL( SCIPhashmapSetImageInt(varmapper->sciptosdp, varmapper->sdptoscip[i + 1], i + 1) );
         }

         varmapper->sdptoscip[pos] = var;
         SCIP_CALL( SCIPhashmapInsertInt(varmapper->sciptosdp, var, pos) );
         varmapper->nvars++;
         SCIP_CALL( SCIPcaptureVar(scip, var) );
      }
   }
   else
      SCIPdebugMsg(scip, "variable %s was not added to the varmapper as it was already part of it.\n", SCIPvarGetName(var));

   return SCIP_OKAY;
}

/** gets the number of variables */
int SCIPsdpVarmapperGetNVars(
   SdpVarmapper*         varmapper           /**< varmapper to get number of variables for */
   )
{
   assert ( varmapper != NULL );

   return varmapper->nvars;
}

/** Is the given SCIP variable included in the varmapper? */
SCIP_Bool SCIPsdpVarmapperExistsSCIPvar(
   SdpVarmapper*         varmapper,          /**< varmapper to search in */
   SCIP_VAR*             var                 /**< SCIP variable to search for */
   )
{
   assert ( varmapper != NULL );
   assert ( var != NULL );

   return SCIPhashmapExists(varmapper->sciptosdp, var);
}

/** gets the SDP-index for the given SCIP variable */
int SCIPsdpVarmapperGetSdpIndex(
   SdpVarmapper*         varmapper,          /**< varmapper to get variable index for */
   SCIP_VAR*             var                 /**< SCIP variable to get SDP-index for */
   )
{
   assert ( varmapper != NULL );
   assert ( var != NULL );

   return SCIPhashmapGetImageInt(varmapper->sciptosdp, (void*) var);
}

/** gets the corresponding SCIP variable for the given SDP variable-index */
SCIP_VAR* SCIPsdpVarmapperGetSCIPvar(
   SdpVarmapper*         varmapper,          /**< varmapper to extract variable from */
   int                   ind                 /**< index of the SDP-variable */
   )
{
   assert ( varmapper != NULL );
   assert ( 0 <= ind && ind < varmapper->nvars );

   return varmapper->sdptoscip[ind];
}

/** removes the variable for the given SDP-index from the varmapper, decreasing the indices of all later variables by 1 */
SCIP_RETCODE SCIPsdpVarmapperRemoveSdpIndex(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         varmapper,          /**< varmapper to remove variable from */
   int                   ind                 /**< index of the SDP-variable */
   )
{
   SCIP_VAR* var;
   int i;

   assert ( scip != NULL );
   assert ( varmapper != NULL );
   assert ( 0 <= ind && ind < varmapper->nvars );

   var = varmapper->sdptoscip[ind];

   assert ( SCIPhashmapExists(varmapper->sciptosdp, var) );

   SCIP_CALL( SCIPhashmapRemove(varmapper->sciptosdp, var) );
   SCIP_CALL( SCIPreleaseVar(scip, &(varmapper)->sdptoscip[ind]) );

   /* shift all entries of the sdptoscip-array behind ind one to the left and update their sciptosdp-entries */
   for (i = ind + 1; i < varmapper->nvars; i++)
   {
      varmapper->sdptoscip[i - 1] = varmapper->sdptoscip[i];
      SCIP_CALL( SCIPhashmapSetImageInt(varmapper->sciptosdp, varmapper->sdptoscip[i - 1], i - 1) );
   }

   /* reallocate memory */
   SCIP_CALL( SCIPreallocBlockMemoryArray(scip, &varmapper->sdptoscip, varmapper->nvars, varmapper->nvars - 1) );

   varmapper->nvars--;

   return SCIP_OKAY;
}

/** swaps all SCIP variables for their transformed counterparts */
SCIP_RETCODE SCIPsdpVarmapperTransform(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         varmapper           /**< pointer to the varmapper that should be transformed */
   )
{
   SCIP_VAR* var;
   int k;

   assert ( scip != NULL );
   assert ( varmapper != NULL );

   for (k = 0; k < varmapper->nvars; ++k)
   {
      SCIP_CALL( SCIPgetTransformedVar(scip, varmapper->sdptoscip[k], &var) );
      SCIP_CALL( SCIPcaptureVar(scip, var) );

      SCIP_CALL( SCIPhashmapRemove(varmapper->sciptosdp, varmapper->sdptoscip[k]) );
      SCIP_CALL( SCIPhashmapInsertInt(varmapper->sciptosdp, var, k) );
      SCIP_CALL( SCIPreleaseVar(scip, &varmapper->sdptoscip[k]) );

      varmapper->sdptoscip[k] = var;
   }

   return SCIP_OKAY;
}

/** clones the varmapper in the second argument to the varmapper in the third argument */
SCIP_RETCODE SCIPsdpVarmapperClone(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         oldmapper,          /**< pointer to the varmapper that should be cloned */
   SdpVarmapper*         newmapper           /**< pointer to the varmapper that should become a clone of the other one */
   )
{
   int nvars;
   int i;

   nvars = oldmapper->nvars;

   newmapper->nvars = nvars;

   /* allocate memory */
   SCIP_CALL( SCIPallocBlockMemory(scip, &newmapper) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &newmapper->sdptoscip, nvars) );

   /* copy entries */
   for (i = 0; i < nvars; i++)
   {
      newmapper->sdptoscip[i] = oldmapper->sdptoscip[i];
      SCIP_CALL( SCIPhashmapInsertInt(newmapper->sciptosdp, oldmapper->sdptoscip[i], i) );
      SCIP_CALL( SCIPcaptureVar(scip, newmapper->sdptoscip[i]) );
   }

   return SCIP_OKAY;
}
