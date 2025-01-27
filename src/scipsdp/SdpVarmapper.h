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

/**@file   SdpVarmapper.h
 * @brief  maps SCIP variables to SDP indices (the SCIP variables are given SDP indices in the order in which they were inserted)
 * @author Tristan Gally
 */

#ifndef __SDPVARMAPPER_H__
#define __SDPVARMAPPER_H__

#include "scip/scip.h"
#include "scip/type_misc.h" /* for SCIP Hashmap */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sdpvarmapper SdpVarmapper;

/** creates the SDP varmapper */
SCIP_EXPORT
SCIP_RETCODE SCIPsdpVarmapperCreate(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper**        varmapper,          /**< Pointer to the varmapper that should be created */
   int                   size                /**< initial size of the sciptosdp-hashmap */
   );

/** frees the SDP varmapper */
SCIP_EXPORT
SCIP_RETCODE SCIPsdpVarmapperFree(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper**        varmapper           /**< Pointer to the varmapper that should be freed */
   );

/** adds the given variables (if not already existent) to the end of the varmapper */
SCIP_EXPORT
SCIP_RETCODE SCIPsdpVarmapperAddVars(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         varmapper,          /**< varmapper to add variables to */
   int                   nvars,              /**< number of variables to add to the varmapper */
   SCIP_VAR**            vars                /**< SCIP variables to add to the varmapper */
   );

/** adds the given variable (if not already existent) to the varmapper at the given position */
SCIP_EXPORT
SCIP_RETCODE SCIPsdpVarmapperInsertVar(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         varmapper,          /**< varmapper to add variables to */
   SCIP_VAR*             var,                /**< SCIP variable to add to the varmapper */
   int                   pos                 /**< position where the variable should be added */
   );

/** gets the number of variables */
SCIP_EXPORT
int SCIPsdpVarmapperGetNVars(
   SdpVarmapper*         varmapper           /**< varmapper to get number of variables for */
   );

/** Is the given SCIP variable included in the varmapper? */
SCIP_EXPORT
SCIP_Bool SCIPsdpVarmapperExistsSCIPvar(
   SdpVarmapper*         varmapper,          /**< varmapper to search in */
   SCIP_VAR*             var                 /**< SCIP variable to search for */
   );

/** gets the SDP-index for the given SCIP variable */
SCIP_EXPORT
int SCIPsdpVarmapperGetSdpIndex(
   SdpVarmapper*         varmapper,          /**< varmapper to get variable index for */
   SCIP_VAR*             var                 /**< SCIP variable to get SDP-index for */
   );

/** gets the corresponding SCIP variable for the given SDP variable-index */
SCIP_EXPORT
SCIP_VAR* SCIPsdpVarmapperGetSCIPvar(
   SdpVarmapper*         varmapper,          /**< varmapper to extract variable from */
   int                   ind                 /**< index of the SDP-variable */
   );

/** removes the variable for the given SDP-index from the varmapper, decreasing the indices of all later variables by 1 */
SCIP_EXPORT
SCIP_RETCODE SCIPsdpVarmapperRemoveSdpIndex(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         varmapper,          /**< varmapper to remove variable from */
   int                   ind                 /**< index of the SDP-variable */
   );

/** swaps all SCIP variables for their transformed counterparts */
SCIP_EXPORT
SCIP_RETCODE SCIPsdpVarmapperTransform(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         varmapper           /**< pointer to the varmapper that should be transformed */
   );

/** clones the varmapper in the second argument to the varmapper in the third argument */
SCIP_EXPORT
SCIP_RETCODE SCIPsdpVarmapperClone(
   SCIP*                 scip,               /**< SCIP data structure */
   SdpVarmapper*         oldmapper,          /**< pointer to the varmapper that should be cloned */
   SdpVarmapper*         newmapper           /**< pointer to the varmapper that should become a clone of the other one */
   );

#ifdef __cplusplus
}
#endif

#endif
