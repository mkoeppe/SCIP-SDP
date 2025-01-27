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

/* #define SCIP_MORE_DEBUG */
/* #define SCIP_DEBUG */

/**@file   reader_sdpa.c
 * @brief  file reader for mixed-integer semidefinite programs in SDPA format
 * @author Tim Schmidt
 * @author Frederic Matter
 * @author Marc Pfetsch
 *
 * @todo Allow to write varbounds other than -infinity/infinity as linear constraints.
 * @todo Allow to write a transformed problem.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>                      /* for strcmp */
#include <ctype.h>                       /* for isspace/isdigit */

#include "scipsdp/reader_sdpa.h"
#include "scipsdp/cons_sdp.h"
#include "scip/cons_linear.h"
#include "scip/cons_indicator.h" /* for SCIPcreateConsIndicatorLinCons */

#define READER_NAME             "sdpareader"
#define READER_DESC             "file reader and writer for MISDPs in sdpa format"
#define READER_EXTENSION        "dat-s"

#define SDPA_MIN_BUFFERLEN 65536   /* minimal size of buffer */

/** SDPA reading data */
struct SCIP_ReaderData
{
   SCIP_Bool             removesmallval;     /**< Should small values in the constraints be removed? */
};


struct SDPA_Data
{
   SCIP_Bool*            sdpblockrank1;      /**< rank-1 information for each SDP block (TRUE = should be rank 1) */
   int                   nsdpblocksrank1;    /**< number of SDP constraints/blocks that should be rank 1 */
   int                   nvars;              /**< number of variables and length of createdvars-array */
   SCIP_VAR**            createdvars;        /**< array of variables created by the SDPA reader */
   int                   nlinconss;          /**< number of constraints and length of createdconss-array */
   SCIP_CONS**           createdconss;       /**< array of constraints created by the SDPA reader */
   int                   nsdpblocks;         /**< number of SDP constraints/blocks */
   int*                  sdpblocksizes;      /**< sizes of the SDP blocks */
   int*                  sdpnblocknonz;      /**< number of nonzeros for each SDP block */
   int*                  sdpnblockvars;      /**< number of variables for each SDP block */
   int**                 nvarnonz;           /**< number of nonzeros for each block and each variable */
   SCIP_VAR***           sdpblockvars;       /**< SCIP variables appearing in each block */
   int**                 sdprow;             /**< array of all row indices for each SDP block */
   int**                 sdpcol;             /**< array of all column indices for each SDP block */
   SCIP_Real**           sdpval;             /**< array of all values of SDP nonzeros for each SDP block */
   int***                rowpointer;         /**< array of pointers to first entries in row-array for each block and variable */
   int***                colpointer;         /**< array of pointers to first entries in row-array for each block and variable */
   SCIP_Real***          valpointer;         /**< array of pointers to first entries in row-array for each block and variable */
   int*                  sdpconstnblocknonz; /**< number of nonzeros for each variable in the constant part, also the i-th entry gives the
                                              *   number of entries of sdpconst row/col/val [i] */
   int**                 sdpconstrow;        /**< pointers to row-indices for each block */
   int**                 sdpconstcol;        /**< pointers to column-indices for each block */
   SCIP_Real**           sdpconstval;        /**< pointers to the values of the nonzeros for each block */
   int                   nconsblocks;        /**< number of constraint blocks specified in the input file */
   int*                  sdpmemsize;         /**< size of memory allocated for the nonconstant part of each SDP constraint */
   int*                  sdpconstmemsize;    /**< size of memory allocated for the constant part of each SDP constraint */
   int                   idxlinconsblock;    /**< the index of the linear constraint block */
   char*                 buffer;             /**< input buffer */
   int                   bufferlen;          /**< length of buffer */
};

typedef struct SDPA_Data SDPA_DATA;

/*
 * Local methods
 */

/** reads the next line from the input file into the line buffer, possibly reallocating buffer */
static
SCIP_RETCODE readLine(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file */
   char**                buffer,             /**< buffer to read into */
   int*                  bufferlen,          /**< buffer length */
   SCIP_Bool*            success             /**< pointer to store whether reading was successful (no error or EOF occured) */
   )
{
   assert( scip != NULL );
   assert( file != NULL );
   assert( buffer != NULL );
   assert( bufferlen != NULL );
   assert( success != NULL );

   /* possibly allocate buffer space */
   if ( *buffer == NULL )
   {
      assert( *bufferlen == 0 );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, buffer, SDPA_MIN_BUFFERLEN) );
      *bufferlen = SDPA_MIN_BUFFERLEN;
   }
   assert( *bufferlen >= 2 );

   /* mark position near end (-2 because *bufferlen possibly will be \0) */
   (*buffer)[*bufferlen-2] = '\0';

   /* read line */
   if ( SCIPfgets(*buffer, *bufferlen, file) == NULL )
   {
      *success = FALSE;
      return SCIP_OKAY;
   }

   /* while buffer is not large enough (this will happen rarely) */
   while ( (*buffer)[*bufferlen-2] != '\0' )
   {
      int newsize;

      assert( (*buffer)[*bufferlen-1] == '\0' );

      /* increase size */
      newsize = SCIPcalcMemGrowSize(scip, *bufferlen + 1);
      SCIP_CALL( SCIPreallocBlockMemoryArray(scip, buffer, *bufferlen, newsize) );
      assert( newsize >= *bufferlen + 1 );

      /* mark position near end */
      (*buffer)[newsize-2] = '\0';

      /* read next part into buffer following already read part; -1/+1, because \0 char at end can be used */
      if ( SCIPfgets(*buffer + *bufferlen - 1, newsize - *bufferlen + 1, file) == NULL )
      {
         *success = FALSE;
         return SCIP_OKAY;
      }
      *bufferlen = newsize;
   }
   *success = TRUE;

   return SCIP_OKAY;
}


/** reads next non-commentary line in given file */
static
SCIP_RETCODE readNextLine(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file to read from */
   char**                buffer,             /**< buffer to read into */
   int*                  bufferlen,          /**< buffer length */
   SCIP_Longint*         linecount,          /**< current linecount */
   SCIP_Bool*            success             /**< pointer to store whether reading was successful (no error or EOF occured) */
   )
{
   char* comment;

   assert( buffer != NULL );
   assert( bufferlen != NULL );
   assert( linecount != NULL );
   assert( success != NULL );

   do
   {
      /* read next line */
      SCIP_CALL( readLine(scip, file, buffer, bufferlen, success) );
      if ( ! *success )
         return SCIP_OKAY;
      ++(*linecount);

      /* special treatment of integer and rank1 section, because they are inside a comment */
      if ( strncmp(*buffer, "*INTEGER", 8) == 0 || strncmp(*buffer, "*RANK1", 5) == 0 )
         return SCIP_OKAY;
   }
   while ( **buffer == '*' || **buffer == '"' || **buffer == '\n' );   /* repeat reading if the line is a comment line */
   assert( *success );

   /* remove comments */
   comment = strpbrk(*buffer, "*\"=");
   if ( comment != NULL )
      *comment = '\0';

   return SCIP_OKAY;
}


/** reads next line in given file, each line starting with '*' */
static
SCIP_RETCODE readNextLineStar(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file to read from */
   char**                buffer,             /**< buffer to read into */
   int*                  bufferlen,          /**< buffer length */
   SCIP_Longint*         linecount,          /**< current linecount */
   SCIP_Bool*            success             /**< pointer to store whether reading was successful (no error or EOF occured) */
   )
{
   assert( buffer != NULL );
   assert( bufferlen != NULL );
   assert( linecount != NULL );
   assert( success != NULL );

   do
   {
      /* read next line */
      SCIP_CALL( readLine(scip, file, buffer, bufferlen, success) );
      if ( ! *success )
         return SCIP_OKAY;
      ++(*linecount);

      /* special treatment of integer and rank1 section, because they are inside a comment */
      if ( strncmp(*buffer, "*INTEGER", 8) == 0 || strncmp(*buffer, "*RANK1", 5) == 0 )
         return SCIP_OKAY;
   }
   while ( **buffer == '\n' );   /* repeat reading for empty lines */
   assert( *success );

   return SCIP_OKAY;
}


/** method for reading a given list of double numbers from file */
static
SCIP_RETCODE readLineDoubles(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file to read from */
   char**                buffer,             /**< pointer to buffer */
   int*                  bufferlen,          /**< pointer to bufferlne */
   SCIP_Longint*         linecount,          /**< current linecount */
   int                   nvals,              /**< number of values to read */
   SCIP_Real*            values,             /**< values that have been read */
   int*                  nread               /**< pointer to store the number of read doubles */
   )
{
   SCIP_Real val;
   SCIP_Bool success;
   char* endptr;
   char* str;
   int cnt = 0;

   assert( nread != NULL );
   *nread = 0;

   SCIP_CALL( readLine(scip, file, buffer, bufferlen, &success) );
   if ( ! success )
      return SCIP_OKAY;
   ++(*linecount);

   str = *buffer;
   while ( *str != '\0' )
   {
      /* skip whitespace */
      while ( isspace(*str) )
         ++str;

      /* if we reached a number */
      if ( isdigit(*str) || *str == '-' || *str == '+' )
      {
         if ( cnt >= nvals )
         {
            SCIPwarningMessage(scip, "Warning: Already read %d values in line %" SCIP_LONGINT_FORMAT ", dropping following numbers in the same line.\n", cnt, *linecount);
            break;
         }

         /* read number itself */
         val = strtod(str, &endptr);
         if ( endptr == NULL )
         {
            SCIPerrorMessage("Could not read number in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
            *nread = -1;
            return SCIP_READERROR;
         }

         if ( SCIPisInfinity(scip, val) || SCIPisInfinity(scip, -val) )
         {
            SCIPerrorMessage("Given value in line %" SCIP_LONGINT_FORMAT " for variable %d is infinity, which is not allowed.\n",
               *linecount, cnt+1);
            *nread = -1;
            return SCIP_READERROR;
         }

         values[cnt++] = val;
         /* advance string to after number */
         str = endptr;
      }
      else if ( *str == '*' || *str == '\"' || *str == '=' )
      {
         /* stop or read next line if we reached a comment */
         if ( cnt < nvals )
         {
            SCIP_CALL( readLine(scip, file, buffer, bufferlen, &success) );
            if ( ! success )
               return SCIP_OKAY;
            ++(*linecount);
            str = *buffer;
         }
         else
            break;
      }
      else
      {
         if ( *str != '\0' )
         {
            SCIPerrorMessage("Found invalid symbol in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
            *nread = -1;
            return SCIP_READERROR;
         }
      }
   }

   *nread = cnt;
   return SCIP_OKAY;
}


/** method for reading a list of integer numbers from a file */
static
SCIP_RETCODE readLineInts(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file to read from */
   char**                buffer,             /**< pointer to buffer */
   int*                  bufferlen,          /**< pointer to bufferlne */
   SCIP_Longint*         linecount,          /**< current linecount */
   int                   nvals,              /**< number of values to read */
   int*                  values,             /**< values that have been read */
   int*                  nread               /**< pointer to store the number of read ints */
   )
{
   SCIP_Bool success;
   char* endptr;
   char* str;
   int cnt = 0;
   int val;

   assert( nread != NULL );
   *nread = 0;

   SCIP_CALL( readLine(scip, file, buffer, bufferlen, &success) );
   if ( ! success )
      return SCIP_OKAY;
   ++(*linecount);

   str = *buffer;
   while ( *str != '\0' )
   {
      /* skip whitespace */
      while ( isspace(*str) )
         ++str;

      /* if we reached a number */
      if ( isdigit(*str) || *str == '-' || *str == '+' )
      {
         if ( cnt >= nvals )
         {
            SCIPwarningMessage(scip, "Warning: Already read %d values in line %" SCIP_LONGINT_FORMAT ", dropping following numbers in the same line.\n", cnt, *linecount);
            break;
         }

         /* read number itself */
         val = (int) strtol(str, &endptr, 10);
         if ( endptr == NULL )
         {
            SCIPerrorMessage("Could not read number in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
            *nread = -1;
            return SCIP_READERROR;
         }
         values[cnt++] = val;
         /* advance string to after number */
         str = endptr;
      }
      else if ( *str == '*' || *str == '\"' || *str == '=' )
      {
         /* stop or read next line if we reached a comment */
         if ( cnt < nvals )
         {
            SCIP_CALL( readLine(scip, file, buffer, bufferlen, &success) );
            if ( ! success )
               return SCIP_OKAY;
            ++(*linecount);
            str = *buffer;
         }
         else
            break;
      }
      else
      {
         if ( *str != '\0' )
         {
            SCIPerrorMessage("Found invalid symbol in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
            *nread = -1;
            return SCIP_READERROR;
         }
      }
   }

   *nread = cnt;
   return SCIP_OKAY;
}


/** frees all data allocated for the SDPA-data-struct */
static
SCIP_RETCODE SDPAfreeData(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file */
   SDPA_DATA*            data                /**< data pointer to save the results in */
   )
{
   int b = 0;

   assert( scip != NULL );
   assert( data != NULL );

   SCIPfreeBlockMemoryArrayNull(scip, &data->buffer, data->bufferlen);
   data->bufferlen = 0;

   if ( data->nsdpblocks > 0 )
   {
      assert( data->nvars > 0 );

      if ( data->sdprow != NULL )
      {
         for (b = 0; b < data->nsdpblocks; b++)
         {
            assert( data->sdpmemsize[b] > 0);
            assert( data->sdpconstmemsize[b] > 0);

            SCIPfreeBlockMemoryArrayNull(scip, &(data->valpointer[b]), data->nvars);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->colpointer[b]), data->nvars);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->rowpointer[b]), data->nvars);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->nvarnonz[b]), data->nvars);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->sdpblockvars[b]), data->nvars);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->sdpconstval[b]), data->sdpconstmemsize[b]);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->sdpconstcol[b]), data->sdpconstmemsize[b]);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->sdpconstrow[b]), data->sdpconstmemsize[b]);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->sdpval[b]), data->sdpmemsize[b]);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->sdpcol[b]), data->sdpmemsize[b]);
            SCIPfreeBlockMemoryArrayNull(scip, &(data->sdprow[b]), data->sdpmemsize[b]);
         }
      }
      SCIPfreeBlockMemoryArrayNull(scip, &data->valpointer, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->colpointer, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->rowpointer, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->nvarnonz, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpblockvars, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpnblockvars, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpconstval, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpconstcol, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpconstrow, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpval, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpcol, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdprow, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpconstnblocknonz, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpnblocknonz, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpconstmemsize, data->nsdpblocks);
      SCIPfreeBlockMemoryArrayNull(scip, &data->sdpmemsize, data->nsdpblocks);
   }

   SCIPfreeBlockMemoryArrayNull(scip, &data->createdconss, data->nlinconss);
   SCIPfreeBlockMemoryArrayNull(scip, &data->sdpblockrank1, data->nsdpblocks);
   SCIPfreeBlockMemoryArrayNull(scip, &data->sdpblocksizes, data->nsdpblocks);
   SCIPfreeBlockMemoryArrayNull(scip, &data->createdvars, data->nvars);

   SCIPfreeBufferNull(scip, &data);

   /* close the file (and make sure SCIPfclose returns 0) */
   if ( SCIPfclose(file) )
      return SCIP_READERROR;

   return SCIP_OKAY;
}


/** reads the number of variables from given SDPA-file */
static
SCIP_RETCODE SDPAreadNVars(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file to read from */
   SCIP_Longint*         linecount,          /**< current linecount */
   SDPA_DATA*            data                /**< data pointer to save the results in */
   )
{
   SCIP_VAR* var;
   SCIP_Bool success;
   char varname[SCIP_MAXSTRLEN];
   int cnt = 0;
   int v;
#ifndef NDEBUG
   int snprintfreturn;
#endif

   assert( scip != NULL );
   assert( file != NULL );
   assert( linecount != NULL );
   assert( data != NULL );

   SCIP_CALL( readNextLine(scip, file, &data->buffer, &data->bufferlen, linecount, &success) );
   if ( ! success )
   {
      SCIPerrorMessage("Unexpected end of file in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
      goto TERMINATE;
   }

   if ( sscanf(data->buffer, "%i", &data->nvars) != 1 )
   {
      SCIPerrorMessage("Could not read number of scalar variables in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
      goto TERMINATE;
   }

   if ( data->nvars < 0 )
   {
      SCIPerrorMessage("Number of scalar variables %d in line %" SCIP_LONGINT_FORMAT " should be non-negative!\n",
         data->nvars, *linecount);
      goto TERMINATE;
   }

   assert( data->nvars >= 0 );

   /* loop through different variable types */
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->createdvars), data->nvars) );

   /* create corresponding variables */
   for (v = 0; v < data->nvars; v++)
   {
#ifndef NDEBUG
      snprintfreturn = SCIPsnprintf(varname, SCIP_MAXSTRLEN, "x_%d", cnt);
      assert( snprintfreturn < SCIP_MAXSTRLEN );
#else
      (void)SCIPsnprintf(varname, SCIP_MAXSTRLEN, "x_%d", cnt);
#endif

      SCIP_CALL( SCIPcreateVar(scip, &var, varname, -SCIPinfinity(scip), SCIPinfinity(scip), 0.0, SCIP_VARTYPE_CONTINUOUS, TRUE, FALSE, NULL, NULL, NULL,
            NULL, NULL) );

      SCIP_CALL( SCIPaddVar(scip, var) );
      data->createdvars[cnt++] = var; /*lint !e732*//*lint !e747*/

      /* release variable for the reader */
      SCIP_CALL( SCIPreleaseVar(scip, &var) );
   }

   return SCIP_OKAY;

 TERMINATE:
   SCIP_CALL( SDPAfreeData(scip, file, data) );
   return SCIP_READERROR;
}


/** reads the number of constraint blocks from given SDPA-file */
static
SCIP_RETCODE SDPAreadNBlocks(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file to read from */
   SCIP_Longint*         linecount,          /**< current linecount */
   SDPA_DATA*            data                /**< data pointer to save the results in */
   )
{
   SCIP_Bool success;

   assert( scip != NULL );
   assert( file != NULL );
   assert( linecount != NULL );
   assert( data != NULL );

   SCIP_CALL( readNextLine(scip, file, &data->buffer, &data->bufferlen, linecount, &success) );
   if ( ! success )
   {
      SCIPerrorMessage("Unexpected end of file in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
      goto TERMINATE;
   }

   if ( sscanf(data->buffer, "%i", &data->nconsblocks) != 1 )
   {
      SCIPerrorMessage("Could not read number of SDP blocks in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
      goto TERMINATE;
   }

   if ( data->nconsblocks < 0 )
   {
      SCIPerrorMessage("Number of SDP blocks %d in line %" SCIP_LONGINT_FORMAT " should be non-negative!\n",
         data->nconsblocks, *linecount);
      goto TERMINATE;
   }

   assert( data->nconsblocks >= 0 );

   return SCIP_OKAY;

 TERMINATE:
   SCIP_CALL( SDPAfreeData(scip, file, data) );
   return SCIP_READERROR;
}


/** reads SDP-constraint sizes and number of linear constraints from given SDPA-file */
static
SCIP_RETCODE SDPAreadBlockSize(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file to read from */
   SCIP_Longint*         linecount,          /**< current linecount */
   SDPA_DATA*            data                /**< data pointer to save the results in */
   )
{
   SCIP_CONS* cons;
   char consname[SCIP_MAXSTRLEN];
   int* sdpblocksizes;
   int* blocksizes;
   int nsdpblocks = 0;
   int nblocks;
   int cnt = 0;
   int b;
   int c;
   int i;

#ifndef NDEBUG
   int snprintfreturn;
#endif

   SCIP_CALL( SCIPallocBufferArray(scip, &blocksizes, data->nconsblocks) );
   SCIP_CALL( SCIPallocBufferArray(scip, &sdpblocksizes, data->nconsblocks) );

   assert( scip != NULL );
   assert( file != NULL );
   assert( linecount != NULL );
   assert( data != NULL );

   SCIP_CALL( readLineInts(scip, file, &data->buffer, &data->bufferlen, linecount, data->nconsblocks, blocksizes, &nblocks) );

   if ( nblocks == -1 )
      goto TERMINATE;
   else if ( data->nconsblocks != nblocks )
   {
      SCIPerrorMessage("Number of specified blocksizes %d in line %" SCIP_LONGINT_FORMAT
         " does not match number of blocks %d.\n", nblocks, *linecount, data->nconsblocks);
      goto TERMINATE;
   }

   assert( nblocks == data->nconsblocks );

   for (i = 0; i < nblocks; i++)
   {
      /* if the entry is less than zero it describes the LP blocks */
      if ( *(blocksizes + i) < 0 )
      {
         if ( data->idxlinconsblock == -1 )
            data->idxlinconsblock = i;
         else
         {
            SCIPerrorMessage("Only one LP block can be defined in line %" SCIP_LONGINT_FORMAT
               " but at least two blocksizes are negative.\n", *linecount);
            goto TERMINATE;
         }
         data->nlinconss = - *(blocksizes + i);
      }
      else
      {
         if ( *(blocksizes + i) == 0 )
         {
            SCIPerrorMessage("Encountered a block size of 0 in line %" SCIP_LONGINT_FORMAT " which is not valid.\n",
               *linecount);
               goto TERMINATE;
         }
         *(sdpblocksizes + nsdpblocks) = *(blocksizes + i);
         ++nsdpblocks;
      }
   }

   assert( data->idxlinconsblock < 0 || data->nlinconss > 0 );
   assert( data->idxlinconsblock >= 0 || data->nlinconss == 0 );

   if ( data->nlinconss < 0 )
   {
      SCIPerrorMessage("Number of linear constraints %d in line %" SCIP_LONGINT_FORMAT " should be non-negative!\n",
         data->nlinconss, *linecount);
      goto TERMINATE;
   }

   assert( data->nlinconss >= 0 );
   assert( nsdpblocks >= 0 );

   data->nsdpblocks = nsdpblocks;

   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpblocksizes), data->nsdpblocks) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpblockrank1), data->nsdpblocks) );
   SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->createdconss), data->nlinconss) );

   for (b = 0; b < nsdpblocks; b++)
   {
      assert( sdpblocksizes[b] > 0 );

      data->sdpblocksizes[b] = *(sdpblocksizes + b);

      /* initialize rank-1 information to FALSE, will eventually be changed in SDPAreadRank1 */
      data->sdpblockrank1[b] = FALSE;
   }

   /* create corresponding constraints */
   for (c = 0; c < data->nlinconss; c++)
   {
#ifndef NDEBUG
      snprintfreturn = SCIPsnprintf(consname, SCIP_MAXSTRLEN, "LP_%d", cnt);
      assert( snprintfreturn < SCIP_MAXSTRLEN );
#else
      (void)SCIPsnprintf(consname, SCIP_MAXSTRLEN, "linear_%d", cnt);
#endif
      /* linear constraints are specified as 0 <= cons <= SCIPinfinity(scip) */
      SCIP_CALL( SCIPcreateConsLinear(scip, &cons, consname, 0, NULL, NULL, 0.0, SCIPinfinity(scip), TRUE, TRUE, TRUE, TRUE, TRUE,
            FALSE, FALSE, FALSE, FALSE, FALSE) );

      SCIP_CALL( SCIPaddCons(scip, cons) );
      data->createdconss[cnt++] = cons;

      /* release constraint for the reader. */
      SCIP_CALL( SCIPreleaseCons(scip, &cons) );
   }

   assert( cnt == data->nlinconss );

   SCIPfreeBufferArray(scip, &sdpblocksizes);
   SCIPfreeBufferArray(scip, &blocksizes);

   return SCIP_OKAY;

 TERMINATE:
   SCIPfreeBufferArray(scip, &sdpblocksizes);
   SCIPfreeBufferArray(scip, &blocksizes);

   SCIP_CALL( SDPAfreeData(scip, file, data) );
   return SCIP_READERROR;
}


/** reads objective values for scalar variables from given SDPA-file */
static
SCIP_RETCODE SDPAreadObjVals(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_READERDATA*      readerdata,         /**< reader data */
   SCIP_FILE*            file,               /**< file to read from */
   SCIP_Longint*         linecount,          /**< current linecount */
   SDPA_DATA*            data                /**< data pointer to save the results in */
   )
{  /*lint --e{818}*/
   SCIP_Real* objvals;
   int v;
   int nreadvals;
   int nzerocoef = 0;
   int nsmallcoef = 0;

   assert( scip != NULL );
   assert( file != NULL );
   assert( linecount != NULL );
   assert( data != NULL );

   SCIP_CALL( SCIPallocBufferArray(scip, &objvals, data->nvars ) );

   if ( data->createdvars == NULL )
   {
      SCIPerrorMessage("Number of variables needs to be specified before objective values!\n");
      goto TERMINATE;
   }
   assert( data->nvars >= 0 );

   SCIP_CALL( readLineDoubles(scip, file, &data->buffer, &data->bufferlen, linecount, data->nvars, objvals, &nreadvals) );

   if ( nreadvals == -1 )
      goto TERMINATE;
   else if ( nreadvals != data->nvars )
   {
      SCIPerrorMessage("Number of objective coefficients %i in line %" SCIP_LONGINT_FORMAT
         " does not match the number of variables %d.\n", nreadvals, *linecount, data->nvars);
      goto TERMINATE;
   }

   assert( data->nvars == nreadvals );

   for (v = 0; v < data->nvars; v++)
   {
      if ( readerdata->removesmallval && SCIPisZero(scip, *(objvals + v)) )
      {
         if ( *(objvals + v) != 0.0 )
            ++nsmallcoef;
         else
            ++nzerocoef;
      }
      else
         SCIP_CALL( SCIPchgVarObj(scip, data->createdvars[v], *(objvals + v)) );
   }

   if ( nsmallcoef > 0 )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_HIGH, NULL, "Remove %d objective coefficients with absolute value less than epsilon = %g.\n",
         nsmallcoef, SCIPepsilon(scip));
   }
   if ( nzerocoef > 0 )
   {
      SCIPverbMessage(scip, SCIP_VERBLEVEL_HIGH, NULL, "Remove %d zero objective coefficients.\n", nzerocoef);
   }

   SCIPfreeBufferArray(scip, &objvals);

   return SCIP_OKAY;

 TERMINATE:
   /* free memory */
   SCIPfreeBufferArray(scip, &objvals);

   SCIP_CALL( SDPAfreeData(scip, file, data) );
   return SCIP_READERROR;
}


/** reads the SDP-constraint blocks and the linear constraint block */
static
SCIP_RETCODE SDPAreadBlocks(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_READERDATA*      readerdata,         /**< reader data */
   SCIP_FILE*            file,               /**< file to read from */
   SCIP_Longint*         linecount,          /**< current linecount */
   SDPA_DATA*            data                /**< data pointer to save the results in */
   )
{
   SCIP_VAR* indvar = 0;
   SCIP_Real** sdpval_local = NULL;          /* array of all values of SDP nonzeros for each SDP block */
   SCIP_Real** sdpconstval_local = NULL;
   SCIP_Real val;
   SCIP_Bool infeasible;
   SCIP_Bool success;
   int** sdprow_local = NULL;                /* array of all row indices for each SDP block */
   int** sdpcol_local = NULL;                /* array of all column indices for each SDP block */
   int** sdpconstrow_local = NULL;           /* pointers to row-indices for each block */
   int** sdpconstcol_local = NULL;           /* pointers to column-indices for each block */
   int** sdpvar = NULL;
   int* nentriessdp = NULL;
   int* nentriessdpconst = NULL;
   int* nentrieslincon = NULL;
   int firstindforvar;
   int nextindaftervar;
   int nzerocoef = 0;
   int nsmallcoef = 0;
   int emptysdpblocks = 0;
   int emptylinconsblocks = 0;
   int nindcons = 0;
   int blockidxoffset = 0;
   int row;
   int col;
   int b;                                    /* current block */
   int v;                                    /* current variable */
   int c;                                    /* current linear constraint */

   assert( scip != NULL );
   assert( file != NULL );
   assert( linecount != NULL );
   assert( data != NULL );

   assert( data->nvars >= 0 );

   if ( data->nvars < 0 || data-> createdvars == NULL )
   {
      SCIPerrorMessage("Number of variables needs to be specified before entries of the blocks!\n");
      goto TERMINATE;
   }

   SCIP_CALL( SCIPallocBufferArray(scip, &nentrieslincon, data->nlinconss) );

   if ( data->nsdpblocks > 0 )
   {
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpmemsize), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpconstmemsize), data->nsdpblocks) );

      SCIP_CALL( SCIPallocBufferArray(scip, &nentriessdp, data->nsdpblocks) );
      SCIP_CALL( SCIPallocBufferArray(scip, &nentriessdpconst, data->nsdpblocks) );

      /* set initial memory size*/
      for (b = 0; b < data->nsdpblocks; b++)
      {
         data->sdpmemsize[b] = 8;
         data->sdpconstmemsize[b] = 8;
         nentriessdpconst[b] = 0;
         nentriessdp[b] = 0;
      }

      for (c = 0; c < data->nlinconss; c++)
         nentrieslincon[c] = 0;

      if ( data->nsdpblocks < 0 )
      {
         SCIPerrorMessage("Number of blocks needs to be specified before entries of the blocks!\n");
         goto TERMINATE;
      }

      if ( data->sdpblocksizes == NULL )
      {
         SCIPerrorMessage("Sizes of the SDP blocks need to be specified before entries of the blocks!\n");
         goto TERMINATE;
      }
      assert( data->nlinconss >= 0 );

      /* initialize sdpnblocknonz with 0 */
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpnblocknonz), data->nsdpblocks) );
      for (b = 0; b < data->nsdpblocks; b++)
         data->sdpnblocknonz[b] = 0;

      /* allocate memory */
      SCIP_CALL( SCIPallocBufferArray(scip, &(sdpvar), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBufferArray(scip, &(sdprow_local), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBufferArray(scip, &(sdpcol_local), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBufferArray(scip, &(sdpval_local), data->nsdpblocks) );

      for (b = 0; b < data->nsdpblocks; b++)
      {
         SCIP_CALL( SCIPallocBufferArray(scip, &(sdpvar[b]), data->sdpmemsize[b]) );
         SCIP_CALL( SCIPallocBufferArray(scip, &(sdprow_local[b]), data->sdpmemsize[b]) );
         SCIP_CALL( SCIPallocBufferArray(scip, &(sdpcol_local[b]), data->sdpmemsize[b]) );
         SCIP_CALL( SCIPallocBufferArray(scip, &(sdpval_local[b]), data->sdpmemsize[b]) );
      }

      /* initialize sdpconstnblocknonz with 0 */
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpconstnblocknonz), data->nsdpblocks) );
      for (b = 0; b < data->nsdpblocks; b++)
         data->sdpconstnblocknonz[b] = 0;

      /* allocate memory (constnnonz for each block, since we do not yet know the distribution) */
      SCIP_CALL( SCIPallocBufferArray(scip, &(sdpconstrow_local), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBufferArray(scip, &(sdpconstcol_local), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBufferArray(scip, &(sdpconstval_local), data->nsdpblocks) );

      for (b = 0; b < data->nsdpblocks; b++)
      {
         SCIP_CALL( SCIPallocBufferArray(scip, &(sdpconstrow_local[b]), data->sdpconstmemsize[b]) );
         SCIP_CALL( SCIPallocBufferArray(scip, &(sdpconstcol_local[b]), data->sdpconstmemsize[b]) );
         SCIP_CALL( SCIPallocBufferArray(scip, &(sdpconstval_local[b]), data->sdpconstmemsize[b]) );
      }
   }

   /* read data */
   SCIP_CALL( readNextLine(scip, file, &data->buffer, &data->bufferlen, linecount, &success) );
   if ( ! success )
   {
      SCIPerrorMessage("Unexpected end of file in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
      goto TERMINATE;
   }

   while ( success )
   {
      if ( strncmp(data->buffer, "*INTEGER", 8) == 0 || strncmp(data->buffer, "*RANK1", 5) == 0 )
         break;

      if ( sscanf(data->buffer, "%i %i %i %i %lf", &v, &b, &row, &col, &val) != 5 )
      {
         SCIPerrorMessage("Could not read block entry in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
         goto TERMINATE;
      }

      /* switch from SDPA counting (starting from 1) to SCIP counting (starting from 0) */
      --v;
      --b;
      --row;
      --col;

      /* reset LP block offset */
      blockidxoffset = 0;

      /* check if this entry belongs to the LP block (FALSE) or to an SDP block (TRUE)*/
      if ( b != data->idxlinconsblock )
      {
      	 /* check if the LP block was already read and adjust the counter as well as the offset for error messages */
         if ( b > data->idxlinconsblock && data->idxlinconsblock >= 0 )
         {
            --b;
            blockidxoffset = 1;
         }

         if ( v < - 1 || v >= data->nvars )
         {
            SCIPerrorMessage("Given coefficient in line %" SCIP_LONGINT_FORMAT " for variable %d which does not exist!\n",
               *linecount, v+1);
            goto TERMINATE;
         }

         if ( b < 0 || b >= data->nsdpblocks )
         {
            SCIPerrorMessage("Given coefficient in line %" SCIP_LONGINT_FORMAT " for SDP block %d which does not exist!\n",
               *linecount, b + 1 + blockidxoffset);
            goto TERMINATE;
         }
         assert( 0 <= b && b < data->nsdpblocks );

         if ( row < 0 || row >= data->sdpblocksizes[b] )
         {
            SCIPerrorMessage("Row index %d of given coefficient in line %" SCIP_LONGINT_FORMAT " is negative or larger than blocksize %d!\n",
               row +1, *linecount, data->sdpblocksizes[b]);
            goto TERMINATE;
         }

         if ( col < 0 || col >= data->sdpblocksizes[b] )
         {
            SCIPerrorMessage("Column index %d of given coefficient in line %" SCIP_LONGINT_FORMAT " is negative or larger than blocksize %d!\n",
               col + 1, *linecount, data->sdpblocksizes[b]);
            goto TERMINATE;
         }

         /* check if this entry belongs to the constant part of the SDP block (v = -1) or not (v >= 0) */
         if ( v >= 0 )
         {
            if ( readerdata->removesmallval && SCIPisZero(scip, val) )
            {
               if ( val != 0.0 )
                  ++nsmallcoef;
               else
                  ++nzerocoef;
            }
            else
            {
               /* for lint: */
               assert( nentriessdp != NULL );
               assert( sdpvar != NULL );
               assert( sdprow_local != NULL );
               assert( sdpcol_local != NULL );
               assert( sdpval_local != NULL );

               if ( SCIPisInfinity(scip, val) ||  SCIPisInfinity(scip, -val) )
               {
                  SCIPerrorMessage("Given coefficient in line %" SCIP_LONGINT_FORMAT " for variable %d is infinity, which is not allowed.\n",
                     *linecount, v+1);
                  goto TERMINATE;
               }

               nentriessdp[b]++;

               /* if the current memory is not sufficient reallocate*/
               if ( nentriessdp[b] >= data->sdpmemsize[b] )
               {
                  int newsize = SCIPcalcMemGrowSize(scip, data->sdpmemsize[b] + 1);
                  assert( newsize > data->sdpmemsize[b] );
                  assert( newsize > nentriessdp[b] );

                  SCIP_CALL( SCIPreallocBufferArray(scip, &(sdpvar[b]), newsize) );
                  SCIP_CALL( SCIPreallocBufferArray(scip, &(sdprow_local[b]), newsize) );
                  SCIP_CALL( SCIPreallocBufferArray(scip, &(sdpcol_local[b]), newsize) );
                  SCIP_CALL( SCIPreallocBufferArray(scip, &(sdpval_local[b]), newsize) );
                  data->sdpmemsize[b] = newsize;
               }

               sdpvar[b][data->sdpnblocknonz[b]] = v;

               /* make sure matrix is in lower triangular form */
               if ( col > row )
               {
                  sdprow_local[b][data->sdpnblocknonz[b]] = col;
                  sdpcol_local[b][data->sdpnblocknonz[b]] = row;
               }
               else
               {
                  sdprow_local[b][data->sdpnblocknonz[b]] = row;
                  sdpcol_local[b][data->sdpnblocknonz[b]] = col;
               }

               sdpval_local[b][data->sdpnblocknonz[b]] = val;
               data->sdpnblocknonz[b]++;
            }
         }
         else /* constant part of SDP block*/
         {
            assert( v == -1 );

            if ( readerdata->removesmallval && SCIPisZero(scip, val) )
            {
               if ( val != 0.0 )
                  ++nsmallcoef;
               else
                  ++nzerocoef;
            }
            else
            {
               /* for lint: */
               assert( nentriessdpconst != NULL );
               assert( sdpconstrow_local != NULL );
               assert( sdpconstcol_local != NULL );
               assert( sdpconstval_local != NULL );

               if ( SCIPisInfinity(scip, val) ||  SCIPisInfinity(scip, -val) )
               {
                  SCIPerrorMessage("Given constant part in line %" SCIP_LONGINT_FORMAT " of block %d is infinity, which is not allowed.\n",
                     *linecount, b+1);
                  goto TERMINATE;
               }

               nentriessdpconst[b]++;

               /* if the current memory is not sufficient reallocate*/
               if ( nentriessdpconst[b] >= data->sdpconstmemsize[b] )
               {
                  int newsize = SCIPcalcMemGrowSize(scip, data->sdpconstmemsize[b] + 1);
                  assert( newsize > data->sdpconstmemsize[b] );
                  assert( newsize > nentriessdpconst[b] );

                  SCIP_CALL( SCIPreallocBufferArray(scip, &(sdpconstrow_local[b]), newsize) );
                  SCIP_CALL( SCIPreallocBufferArray(scip, &(sdpconstcol_local[b]), newsize) );
                  SCIP_CALL( SCIPreallocBufferArray(scip, &(sdpconstval_local[b]), newsize) );

                  data->sdpconstmemsize[b] = newsize;
               }

               /* make sure matrix is in lower triangular form */
               if ( col > row )
               {
                  sdpconstrow_local[b][data->sdpconstnblocknonz[b]] = col;
                  sdpconstcol_local[b][data->sdpconstnblocknonz[b]] = row;
               }
               else
               {
                  sdpconstrow_local[b][data->sdpconstnblocknonz[b]] = row;
                  sdpconstcol_local[b][data->sdpconstnblocknonz[b]] = col;
               }
               sdpconstval_local[b][data->sdpconstnblocknonz[b]] = val;
               data->sdpconstnblocknonz[b]++;
            }
         }
      }
      else /* LP block */
      {
         /* indicator variables have a negative variable index */
         if ( v >= data->nvars )
         {
            SCIPerrorMessage("Given linear coefficient in line %" SCIP_LONGINT_FORMAT " for variable %d which does not exist!\n",
               *linecount, v + 1);
            goto TERMINATE;
         }

         /* linear constraints are specified on the diagonal of the LP block */
         if ( row != col )
         {
            SCIPerrorMessage("Given linear coefficient in line %" SCIP_LONGINT_FORMAT " is not located on the diagonal!\n",
               *linecount);
            goto TERMINATE;
         }

         assert( row == col );

         if ( row < 0 || row >= data->nlinconss )
         {
            SCIPerrorMessage("Given linear coefficient in line %" SCIP_LONGINT_FORMAT " for linear constraint %d which does not exist!\n",
               *linecount, row + 1);
            goto TERMINATE;
         }

         /* check if this entry belongs to the constant part of the LP block (v = -1) or not (v >= 0 || v < -1) the latter for indicator variables  */
         if ( v >= 0 )
         {
            if ( SCIPisInfinity(scip, val) ||  SCIPisInfinity(scip, -val) )
            {
               SCIPerrorMessage("Given linear coefficient in line %" SCIP_LONGINT_FORMAT " for variable %d is infinity, which is not allowed.\n",
                  *linecount, v+1);
               goto TERMINATE;
            }

            if ( readerdata->removesmallval && SCIPisZero(scip, val) )
            {
               if ( val != 0.0 )
                  ++nsmallcoef;
               else
                  ++nzerocoef;
            }
            else
            {
               SCIP_CALL( SCIPaddCoefLinear(scip, data->createdconss[row], data->createdvars[v],val) );/*lint !e732*//*lint !e747*/
               nentrieslincon[row]++;
            }
         }
         else /* constant part or indicator constraint*/
         {
            if ( v < -1 )  /* indicator constraint*/
            {
               SCIP_CONS* indcons;
               SCIP_VAR* slackvar = 0;
               char name[SCIP_MAXSTRLEN];
#ifndef NDEBUG
               int snprintfreturn;
#endif
               /* adjust variable index to be positive */
               v = -v - 2;

#ifndef NDEBUG
               snprintfreturn = SCIPsnprintf(name, SCIP_MAXSTRLEN, "indslack_cons_indicator_%d", nindcons);
               assert( snprintfreturn < SCIP_MAXSTRLEN);
#else
               (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "indslack_cons_indicator_%d", nindcons);
#endif
               /* create slack variable and add it to the constraint */
               SCIP_CALL( SCIPcreateVar(scip, &slackvar, name, 0.0, SCIPinfinity(scip), 0.0,
                     SCIP_VARTYPE_CONTINUOUS, TRUE, FALSE, 0, 0, 0, 0, 0));
               SCIP_CALL( SCIPaddVar(scip, slackvar) );

               SCIP_CALL( SCIPaddCoefLinear(scip,data->createdconss[row] , slackvar, +1.0) );/*lint !e732*//*lint !e747*/

#ifndef NDEBUG
               snprintfreturn = SCIPsnprintf(name, SCIP_MAXSTRLEN, "indlin_cons_indicator_%d", nindcons);
               assert( snprintfreturn < SCIP_MAXSTRLEN);
#else
               (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "indlin_cons_indicator_%d", nindcons);
#endif
               /* change name of the corresponding linear constraint */
               SCIP_CALL( SCIPchgConsName(scip, data->createdconss[row], name) );

#ifndef NDEBUG
               snprintfreturn = SCIPsnprintf(name, SCIP_MAXSTRLEN, "cons_indicator_%d", nindcons);
               assert( snprintfreturn < SCIP_MAXSTRLEN);
#else
               (void) SCIPsnprintf(name, SCIP_MAXSTRLEN, "cons_indicator_%d", nindcons);
#endif
               indvar = data->createdvars[v];
               SCIP_CALL( SCIPchgVarLbGlobal(scip, indvar, 0.0) );
               SCIP_CALL( SCIPchgVarUbGlobal(scip, indvar, 1.0) );
               SCIP_CALL( SCIPchgVarType(scip, indvar, SCIP_VARTYPE_BINARY, &infeasible) );
               SCIP_CALL( SCIPcreateConsIndicatorLinCons( scip, &indcons, name, indvar,data->createdconss[row], slackvar,
                     TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE) );
               SCIP_CALL( SCIPaddCons(scip, indcons) );

               /* release both conss and the slackvar */
               SCIP_CALL( SCIPreleaseCons(scip, &indcons) );
               SCIP_CALL( SCIPreleaseVar(scip, &slackvar) );

               nindcons++;
            }
            else /* constant part */
            {
               assert( v == -1 );

               if ( SCIPisInfinity(scip, val) ||  SCIPisInfinity(scip, -val))
               {
                  SCIPerrorMessage("Given constant part in line %" SCIP_LONGINT_FORMAT " of block %d is infinity, which is not allowed.\n",
                     *linecount, b+1);
                  goto TERMINATE;
               }

               if ( readerdata->removesmallval && SCIPisZero(scip, val) )
               {
                  if ( val != 0.0 )
                     ++nsmallcoef;
                  else
                     ++nzerocoef;
               }
               else
               {
                  assert( ! SCIPisInfinity(scip, - SCIPgetLhsLinear(scip, data->createdconss[row])) );
                  assert( SCIPisInfinity(scip, SCIPgetRhsLinear(scip, data->createdconss[row])) );

                  /* all linear constraints are greater or equal constraints */
                  SCIP_CALL( SCIPchgLhsLinear(scip, data->createdconss[row], val) );
               }
            }
         }
      }

      SCIP_CALL( readNextLine(scip, file, &data->buffer, &data->bufferlen, linecount, &success) );
   }

   /* reset LP block offset */
   blockidxoffset = 0;

   for (b = 0; b < data->nsdpblocks; b++)
   {
      if ( nentriessdp[b] == 0 )
      {
         emptysdpblocks++;

         /* account for a possible LP block */
         if ( data->idxlinconsblock >= 0 && b >= data->idxlinconsblock )
            blockidxoffset = 1;

         SCIPerrorMessage("SDP block number %d does not contain any nonzero entries!\n", b + 1 + blockidxoffset);
      }
   }

   if ( emptysdpblocks > 0 )
      goto TERMINATE;

   for (c = 0; c < data->nlinconss; c++)
   {
      if ( nentrieslincon[c] == 0 )
      {
         SCIPerrorMessage("Linear constraint number %d does not contain nonzero entries!\n", c + 1);
         emptylinconsblocks++;
      }
   }

   if ( emptylinconsblocks > 0 )
      goto TERMINATE;

   if ( data->nsdpblocks > 0 )
   {
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdprow), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpcol), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpval), data->nsdpblocks) );

      for (b = 0; b < data->nsdpblocks; b++)
      {
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdprow[b]), data->sdpmemsize[b]) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpcol[b]), data->sdpmemsize[b]) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpval[b]), data->sdpmemsize[b]) );
      }

      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpconstrow), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpconstcol), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpconstval), data->nsdpblocks) );

      for (b = 0; b < data->nsdpblocks; b++)
      {
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpconstrow[b]), data->sdpconstmemsize[b]) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpconstcol[b]), data->sdpconstmemsize[b]) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpconstval[b]), data->sdpconstmemsize[b]) );
      }

      /* write the read blocks from buffer memory to the data object */
      for (b = 0; b < data->nsdpblocks; b++)
      {
         int nonz;

         for (nonz = 0; nonz < data->sdpnblocknonz[b]; nonz++)
         {
            if ( sdpcol_local[b][nonz] > sdprow_local[b][nonz] )
            {
               data->sdprow[b][nonz] = sdpcol_local[b][nonz];
               data->sdpcol[b][nonz] = sdprow_local[b][nonz];
            }
            else
            {
               data->sdprow[b][nonz] = sdprow_local[b][nonz];
               data->sdpcol[b][nonz] = sdpcol_local[b][nonz];
            }
            data->sdpval[b][nonz] = sdpval_local[b][nonz];
         }

         for (nonz = 0; nonz < data->sdpconstnblocknonz[b]; nonz++)
         {
            if ( sdpconstcol_local[b][nonz] > sdpconstrow_local[b][nonz] )
            {
               data->sdpconstrow[b][nonz] = sdpconstcol_local[b][nonz];
               data->sdpconstcol[b][nonz] = sdpconstrow_local[b][nonz];
            }
            else
            {
               data->sdpconstrow[b][nonz] = sdpconstrow_local[b][nonz];
               data->sdpconstcol[b][nonz] = sdpconstcol_local[b][nonz];
            }
            data->sdpconstval[b][nonz] = sdpconstval_local[b][nonz];
         }
      }

      /* construct pointer arrays */
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpnblockvars), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpblockvars), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->nvarnonz), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->rowpointer), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->colpointer), data->nsdpblocks) );
      SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->valpointer), data->nsdpblocks) );

      /* sdp blocks as specified in sdpa file */
      for (b = 0; b < data->nsdpblocks; b++)
      {
         /* sort the nonzeroes by non-decreasing variable indices */
         SCIPsortIntIntIntReal(sdpvar[b], data->sdprow[b], data->sdpcol[b], data->sdpval[b], data->sdpnblocknonz[b]);

         /* create the pointer arrays and insert used variables into vars-array */
         nextindaftervar = 0;
         data->sdpnblockvars[b] = 0;
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->sdpblockvars[b]), data->nvars) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->nvarnonz[b]), data->nvars) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->rowpointer[b]), data->nvars) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->colpointer[b]), data->nvars) );
         SCIP_CALL( SCIPallocBlockMemoryArray(scip, &(data->valpointer[b]), data->nvars) );

         for (v = 0; v < data->nvars; v++)
         {
            SCIP_Bool varused = FALSE;

            firstindforvar = nextindaftervar; /* this variable starts where the last one ended */
            data->nvarnonz[b][data->sdpnblockvars[b]] = 0;

            /* get the first index that doesn't belong to this variable */
            while ( nextindaftervar < data->sdpnblocknonz[b] && sdpvar[b][nextindaftervar] == v )
            {
               nextindaftervar++;
               varused = TRUE;
               data->nvarnonz[b][data->sdpnblockvars[b]]++;
            }

            if ( varused )
            {
               /* if the variable is used, add it to the vars array */
               data->sdpblockvars[b][data->sdpnblockvars[b]] = data->createdvars[v];
               /* save a pointer to the first nonzero belonging to this variable */
               data->rowpointer[b][data->sdpnblockvars[b]] = &(data->sdprow[b][firstindforvar]);
               data->colpointer[b][data->sdpnblockvars[b]] = &(data->sdpcol[b][firstindforvar]);
               data->valpointer[b][data->sdpnblockvars[b]] = &(data->sdpval[b][firstindforvar]);
               data->sdpnblockvars[b]++;
            }
         }
         assert( nextindaftervar == data->sdpnblocknonz[b] );
      }

      if ( nsmallcoef > 0 )
         SCIPverbMessage(scip, SCIP_VERBLEVEL_HIGH, NULL,
            "Remove %d block coefficients with absolute value less than epsilon = %g.\n", nsmallcoef, SCIPepsilon(scip));
      if ( nzerocoef > 0 )
         SCIPverbMessage(scip, SCIP_VERBLEVEL_HIGH, NULL, "Remove %d zero block coefficients.\n", nzerocoef);

      /* free buffer memory */
      for (b = 0; b < data->nsdpblocks; b++)
      {
         SCIPfreeBufferArray(scip, &(sdpconstval_local[b]));
         SCIPfreeBufferArray(scip, &(sdpconstcol_local[b]));
         SCIPfreeBufferArray(scip, &(sdpconstrow_local[b]));
         SCIPfreeBufferArray(scip, &(sdpval_local[b]));
         SCIPfreeBufferArray(scip, &(sdpcol_local[b]));
         SCIPfreeBufferArray(scip, &(sdprow_local[b]));
         SCIPfreeBufferArray(scip, &(sdpvar[b]));
      }
      SCIPfreeBufferArray(scip, &(sdpconstval_local));
      SCIPfreeBufferArray(scip, &(sdpconstcol_local));
      SCIPfreeBufferArray(scip, &(sdpconstrow_local));
      SCIPfreeBufferArray(scip, &(sdpval_local));
      SCIPfreeBufferArray(scip, &(sdpcol_local));
      SCIPfreeBufferArray(scip, &(sdprow_local));
      SCIPfreeBufferArray(scip, &sdpvar);
      SCIPfreeBufferArray(scip, &nentriessdpconst);
      SCIPfreeBufferArray(scip, &nentriessdp);
   }

   SCIPfreeBufferArray(scip, &nentrieslincon);

   return SCIP_OKAY;

 TERMINATE:
   /* free memory  */
   if ( data->nsdpblocks > 0 )
   {
      if ( sdpvar != NULL )
      {
         /* free buffer memory */
         for (b = 0; b < data->nsdpblocks; b++)
         {
            SCIPfreeBufferArrayNull(scip, &(sdpconstval_local[b]));
            SCIPfreeBufferArrayNull(scip, &(sdpconstcol_local[b]));
            SCIPfreeBufferArrayNull(scip, &(sdpconstrow_local[b]));
            SCIPfreeBufferArrayNull(scip, &(sdpval_local[b]));
            SCIPfreeBufferArrayNull(scip, &(sdpcol_local[b]));
            SCIPfreeBufferArrayNull(scip, &(sdprow_local[b]));
            SCIPfreeBufferArrayNull(scip, &(sdpvar[b]));
         }
      }
      SCIPfreeBufferArrayNull(scip, &(sdpconstval_local));
      SCIPfreeBufferArrayNull(scip, &(sdpconstcol_local));
      SCIPfreeBufferArrayNull(scip, &(sdpconstrow_local));
      SCIPfreeBufferArrayNull(scip, &(sdpval_local));
      SCIPfreeBufferArrayNull(scip, &(sdpcol_local));
      SCIPfreeBufferArrayNull(scip, &(sdprow_local));
      SCIPfreeBufferArrayNull(scip, &sdpvar);
      SCIPfreeBufferArrayNull(scip, &nentriessdpconst);
      SCIPfreeBufferArrayNull(scip, &nentriessdp);
   }
   SCIPfreeBufferArrayNull(scip, &nentrieslincon);

   SCIP_CALL( SDPAfreeData(scip, file, data) );
   return SCIP_READERROR;
}


/** reads integrality conditions from given SDPA-file */
static
SCIP_RETCODE SDPAreadInt(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file to read from */
   SCIP_Longint*         linecount,          /**< current linecount */
   SDPA_DATA*            data                /**< data pointer to save the results in */
   )
{  /*lint --e{818}*/
   SCIP_Bool success;
   int v;

   assert( scip != NULL );
   assert( file != NULL );
   assert( linecount != NULL );
   assert( data != NULL );

   if ( data->createdvars == NULL )
   {
      SCIPerrorMessage("Number of variables needs to be specified before integer section!\n");
      goto TERMINATE;
   }

   assert( data->nvars >= 0 );

   /* read to end of file */
   SCIP_CALL( readNextLineStar(scip, file, &data->buffer, &data->bufferlen, linecount, &success) );
   while ( success )
   {
      if ( strncmp(data->buffer, "*RANK1", 5) == 0 )
         break;

      /* check that line starts with '*' */
      if ( strncmp(data->buffer, "*", 1) != 0 )
      {
         SCIPerrorMessage("Expected '*' at the beginning of line %" SCIP_LONGINT_FORMAT " in the INT-section.\n",
            *linecount);
         goto TERMINATE;
      }

      if ( sscanf(data->buffer + 1, "%i", &v) != 1 ) /* move the index by one to ignore the first character of the line */
      {
         SCIPerrorMessage("Could not read variable index in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
         goto TERMINATE;
      }

      if ( v < 1 || v > data->nvars )
      {
         SCIPerrorMessage("Given integrality in line %" SCIP_LONGINT_FORMAT " for variable %d which does not exist!\n",
            *linecount, v);
         goto TERMINATE;
      }

      --v;

      if ( SCIPvarGetType(data->createdvars[v]) != SCIP_VARTYPE_BINARY )
      {
         SCIP_Bool infeasible;

         SCIP_CALL( SCIPchgVarType(scip, data->createdvars[v], SCIP_VARTYPE_INTEGER, &infeasible) );

         if ( infeasible )
         {
            SCIPerrorMessage("Infeasibility detected because of integrality of variable %s!\n",
               SCIPvarGetName(data->createdvars[v]));
            goto TERMINATE;
         }
      }

      /* read next line */
      SCIP_CALL( readNextLineStar(scip, file, &data->buffer, &data->bufferlen, linecount, &success) );
   }

   return SCIP_OKAY;

 TERMINATE:
   SCIP_CALL( SDPAfreeData(scip, file, data) );
   return SCIP_READERROR;
}


/** reads rank1 conditions from given SDPA-file */
static
SCIP_RETCODE SDPAreadRank1(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_FILE*            file,               /**< file to read from */
   SCIP_Longint*         linecount,          /**< current linecount */
   SDPA_DATA*            data                /**< data pointer to save the results in */
   )
{  /*lint --e{818}*/
   SCIP_Bool success;
   int blockidxoffset = 0;
   int v;

   assert( scip != NULL );
   assert( file != NULL );
   assert( linecount != NULL );
   assert( data != NULL );

   if ( data->sdpblocksizes == NULL )
   {
      SCIPerrorMessage("SDP blocks need to be specified before rank-1 section!\n");
      goto TERMINATE;
   }

   assert( data->nvars >= 0 );

   /* read to end of file */
   SCIP_CALL( readNextLineStar(scip, file, &data->buffer, &data->bufferlen, linecount, &success) );
   while ( success )
   {
      char* ps;

      if ( strncmp(data->buffer, "*INTEGER", 8) == 0 )
      {
         SCIPerrorMessage("Integer section in line %" SCIP_LONGINT_FORMAT " needs to be in front of rank1 section.\n",
            *linecount);
         goto TERMINATE;
      }

      /* check that line starts with '*' */
      if ( strncmp(data->buffer, "*", 1) != 0 )
      {
         SCIPerrorMessage("Expected '*' at the beginning of line %" SCIP_LONGINT_FORMAT " in the RANK1-section.\n", *linecount);
         goto TERMINATE;
      }

      /* move the index by one to ignore the first character of the line */
      ps = data->buffer + 1;
      if ( sscanf(ps, "%i", &v) != 1 )
      {
         SCIPerrorMessage("Could not read SDP block index in line %" SCIP_LONGINT_FORMAT ".\n", *linecount);
         goto TERMINATE;
      }

      /* switch from SDPA counting (starting from 1) to SCIP counting (starting from 0) */
      --v;

      /* reset LP block offset */
      blockidxoffset = 0;

      if ( v == data->idxlinconsblock )
      {
         SCIPerrorMessage("Given rank1 in line %" SCIP_LONGINT_FORMAT " for the LP block which is not valid.\n",
            *linecount);
         goto TERMINATE;
      }

      /* check if the LP block was already read and adjust the counter as well as the offset for error messages */
      if ( data->idxlinconsblock >= 0 && v > data->idxlinconsblock )
      {
         v -= 1;
         blockidxoffset = 1;
      }

      if ( v < 0 || v >= data->nsdpblocks )
      {
         SCIPerrorMessage("Given rank1 in line %" SCIP_LONGINT_FORMAT " for SDP block %d which does not exist!\n",
            *linecount, v + 1 + blockidxoffset);
         goto TERMINATE;
      }

      data->sdpblockrank1[v] = TRUE;
      ++data->nsdpblocksrank1;

      SCIP_CALL( readNextLineStar(scip, file, &data->buffer, &data->bufferlen, linecount, &success) );
   }

   return SCIP_OKAY;

 TERMINATE:
   SCIP_CALL( SDPAfreeData(scip, file, data) );
   return SCIP_READERROR;
}


/*
 * Callback methods of reader
 */


/** copy method for reader plugins (called when SCIP copies plugins) */
static
SCIP_DECL_READERCOPY(readerCopySdpa)
{  /*lint --e{715,818}*/
   assert( scip != NULL );

   SCIP_CALL( SCIPincludeReaderSdpa(scip) );

   return SCIP_OKAY;
}


/** problem reading method of reader */
static
SCIP_DECL_READERREAD(readerReadSdpa)
{  /*lint --e{715,818}*/
   SCIP_FILE* file;
   SCIP_Longint linecount = 0;
   SCIP_READERDATA* readerdata;
   SDPA_DATA* data;
   int b;

   assert( result != NULL );

   *result = SCIP_DIDNOTRUN;

   SCIPdebugMsg(scip, "Reading file %s ...\n", filename);

   file = SCIPfopen(filename, "r");

   if ( ! file )
      return SCIP_READERROR;

   SCIP_CALL( SCIPallocBuffer(scip, &data) );
   data->nsdpblocks = -1;
   data->nsdpblocksrank1 = 0;
   data->nlinconss = 0;
   data->nvars = -1;
   data->nconsblocks = -1;
   data->idxlinconsblock = -1;
   data->bufferlen = 0;
   data->sdpblockrank1 = NULL;
   data->createdvars = NULL;
   data->createdconss = NULL;
   data->sdpblocksizes = NULL;
   data->sdpnblocknonz = NULL;
   data->sdpnblockvars = NULL;
   data->nvarnonz = NULL;
   data->sdpblockvars = NULL;
   data->sdprow = NULL;
   data->sdpcol = NULL;
   data->sdpval = NULL;
   data->rowpointer = NULL;
   data->colpointer = NULL;
   data->valpointer = NULL;
   data->sdpconstnblocknonz = NULL;
   data->sdpconstrow = NULL;
   data->sdpconstcol = NULL;
   data->sdpconstval = NULL;
   data->sdpmemsize = NULL;
   data->sdpconstmemsize = NULL;
   data->buffer = NULL;

   readerdata = SCIPreaderGetData(reader);
   assert( readerdata != NULL );

   SCIP_CALL( SCIPgetBoolParam(scip, "reading/removesmallval", &readerdata->removesmallval) );

   /* create empty problem */
   SCIP_CALL( SCIPcreateProb(scip, filename, NULL, NULL, NULL, NULL, NULL, NULL, NULL) );

   SCIP_CALL( SCIPsetObjsense(scip, SCIP_OBJSENSE_MINIMIZE) );

   /* read the file */
   SCIPdebugMsg(scip, "Reading number of variables\n");
   SCIP_CALL( SDPAreadNVars(scip, file, &linecount, data) );

   SCIPdebugMsg(scip, "Reading number of blocks\n");
   SCIP_CALL( SDPAreadNBlocks(scip, file, &linecount, data) );

   SCIPdebugMsg(scip, "Reading blocksizes\n");
   SCIP_CALL( SDPAreadBlockSize(scip, file, &linecount, data) );

   SCIPdebugMsg(scip, "Reading objective values\n");
   SCIP_CALL( SDPAreadObjVals(scip, readerdata, file, &linecount, data) );

   SCIPdebugMsg(scip, "Reading block entries\n");
   SCIP_CALL( SDPAreadBlocks(scip, readerdata, file, &linecount, data) );

   if ( strncmp(data->buffer, "*INTEGER", 8) == 0 )
   {
      SCIPdebugMsg(scip, "Reading integer section\n");
      SCIP_CALL( SDPAreadInt(scip, file, &linecount, data) );
   }

   if ( strncmp(data->buffer, "*RANK1", 5) == 0 )
   {
      SCIPdebugMsg(scip, "Reading rank1 section\n");
      SCIP_CALL( SDPAreadRank1(scip, file, &linecount, data) );
   }

#ifdef SCIP_MORE_DEBUG
   for (b = 0; b < SCIPgetNConss(scip); b++)
   {
      SCIP_CALL( SCIPprintCons(scip, SCIPgetConss(scip)[b], NULL) );
      SCIPinfoMessage(scip, NULL, "\n");
   }
#endif

   /* create SDP-constraints */
   for (b = 0; b < data->nsdpblocks; b++)
   {
      SCIP_CONS *sdpcons;
      char sdpconname[SCIP_MAXSTRLEN];
#ifndef NDEBUG
      int snprintfreturn;
#endif
      assert( data->sdpblocksizes[b] > 0 );
      assert( (data->sdpnblockvars[b] > 0 && data->sdpnblocknonz[b] > 0) || (data->sdpconstnblocknonz[b] > 0) );
#ifndef NDEBUG
      snprintfreturn = SCIPsnprintf(sdpconname, SCIP_MAXSTRLEN, "SDP_%d", b);
      assert( snprintfreturn < SCIP_MAXSTRLEN );
#else
      (void) SCIPsnprintf(sdpconname, SCIP_MAXSTRLEN, "SDP_%d", b);
#endif

      /* special treatment of case without constant PSD blocks */
      if ( data->sdpconstnblocknonz == NULL )
      {
         if ( ! data->sdpblockrank1[b] )
         {
            SCIP_CALL( SCIPcreateConsSdp(scip, &sdpcons, sdpconname, data->sdpnblockvars[b], data->sdpnblocknonz[b],
                  data->sdpblocksizes[b], data->nvarnonz[b], data->colpointer[b], data->rowpointer[b],
                  data->valpointer[b], data->sdpblockvars[b], 0, NULL, NULL, NULL, TRUE) );
         }
         else
         {
            SCIP_CALL( SCIPcreateConsSdpRank1(scip, &sdpcons, sdpconname, data->sdpnblockvars[b], data->sdpnblocknonz[b],
                  data->sdpblocksizes[b], data->nvarnonz[b], data->colpointer[b], data->rowpointer[b],
                  data->valpointer[b], data->sdpblockvars[b], 0, NULL, NULL, NULL, TRUE) );
         }
      }
      else
      {
         if ( ! data->sdpblockrank1[b] )
         {
            SCIP_CALL( SCIPcreateConsSdp(scip, &sdpcons, sdpconname, data->sdpnblockvars[b],data->sdpnblocknonz[b],
                  data->sdpblocksizes[b], data->nvarnonz[b], data->colpointer[b],data->rowpointer[b], data->valpointer[b],
                  data->sdpblockvars[b], data->sdpconstnblocknonz[b],data->sdpconstcol[b], data->sdpconstrow[b],
                  data->sdpconstval[b], TRUE) );
         }
         else
         {
            SCIP_CALL( SCIPcreateConsSdpRank1(scip, &sdpcons, sdpconname, data->sdpnblockvars[b], data->sdpnblocknonz[b],
                  data->sdpblocksizes[b], data->nvarnonz[b], data->colpointer[b], data->rowpointer[b], data->valpointer[b],
                  data->sdpblockvars[b], data->sdpconstnblocknonz[b], data->sdpconstcol[b], data->sdpconstrow[b],
                  data->sdpconstval[b], TRUE) );
         }
      }
#ifdef SCIP_MORE_DEBUG
      SCIP_CALL( SCIPprintCons(scip, sdpcons, NULL) );
#endif
      SCIP_CALL( SCIPaddCons(scip, sdpcons) );

      SCIP_CALL( SCIPreleaseCons(scip, &sdpcons) );
   }

   /* free space */
   SCIP_CALL( SDPAfreeData(scip, file, data) );

   *result = SCIP_SUCCESS;

   return SCIP_OKAY;
}


/** problem writing method of reader */
static
SCIP_DECL_READERWRITE(readerWriteSdpa)
{  /*lint --e{715,818}*/
   SCIP_VAR** linvars;
   SCIP_Real* linvals;
   SCIP_VAR** sdpvars;
   SCIP_Real** sdpval;
   SCIP_Real* sdpconstval;
   SCIP_Real val;
   SCIP_Real lhs;
   SCIP_Real rhs;
   int** sdpcol;
   int** sdprow;
   int* sdpconstcol;
   int* sdpconstrow;
   int* sdpnvarnonz;
   int* varsenses;
   int* consssenses;
   int nsdpconss = 0;
   int sdpnvars;
   int sdpnnonz;
   int totalsdpnnonz = 0;
   int sdpblocksize;
   int sdparraylength;
   int totalsdpconstnnonz = 0;
   int sdpconstnnonz;
   int consind = 0;
   int linconsind = 0;
   int nblocks;
   int conssign = 1;
   int nchangedconss = 0;
   int nvarbndslinconss = 0;
   int nlinconss = 0;
   int nrank1sdpblocks = 0;
   int objcoeff = 1;
   int c;
   int i;
   int v;

   assert( scip != NULL );
   assert( result != NULL );
   assert( nvars > 0 );
   assert( nconss >= 0 );

   SCIPdebugMsg(scip, "Writing problem in SDPA format to file.\n");
   *result = SCIP_DIDNOTRUN;

   if ( transformed )
   {
      SCIPerrorMessage("SDPA reader currently only supports writing original problems!\n");
      return SCIP_READERROR; /*lint !e527*/
   }

#ifndef NDEBUG
   for (v = 0; v < nvars; v++)
      assert( SCIPvarGetStatus(vars[v]) == SCIP_VARSTATUS_ORIGINAL );
#endif

   /* write number of variables */
   SCIPinfoMessage(scip, file, "%d\n", nvars);

   /* collect different variable senses */
   SCIP_CALL( SCIPallocBufferArray(scip, &varsenses, nvars) );

   for (v = 0; v < nvars; v++)
   {
      SCIP_Real lb;
      SCIP_Real ub;

      lb = SCIPvarGetLbOriginal(vars[v]);
      ub = SCIPvarGetUbOriginal(vars[v]);

      varsenses[v] = 0;
      if ( SCIPisZero(scip, lb) )
      {
         varsenses[v] = 1;
         nvarbndslinconss++;
      }
      else
      {
         if ( ! SCIPisInfinity(scip, -lb) )
         {
            SCIPerrorMessage("Can only handle variables with lower bound 0 or minus infinity.\n");
            SCIPfreeBufferArray(scip, &varsenses);
            return SCIP_READERROR; /*lint !e527*/
         }
      }

      if ( SCIPisZero(scip, ub) )
      {
         varsenses[v] = -1;
         nvarbndslinconss++;
      }
      else
      {
         if ( ! SCIPisInfinity(scip, ub) )
         {
            SCIPerrorMessage("Can only handle variables with upper bound 0 or infinity.\n");
            SCIPfreeBufferArray(scip, &varsenses);
            return SCIP_READERROR; /*lint !e527*/
         }
      }
   }

   /* collect different constraint senses */
   SCIP_CALL( SCIPallocBufferArray(scip, &consssenses, nconss) );

   /* check if all constraints are either linear or SDP*/
   for (c = 0; c < nconss; c++)
   {
      if ( (strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "linear") != 0)
         && (strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDP") != 0)
         && (strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDPrank1") != 0))
      {
         SCIPerrorMessage("SDPA reader currently only supports linear, SDP and SDPrank1 constraints!\n");
         SCIPfreeBufferArray(scip, &consssenses);
         SCIPfreeBufferArray(scip, &varsenses);
         return SCIP_READERROR; /*lint !e527*/
      }

      /* count number of rank1 sdp blocks */
      if ( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDPrank1") == 0 )
         ++nrank1sdpblocks;

      /* only check linear constraints */
      if ( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "linear") == 0 )
      {
         lhs = SCIPgetLhsLinear(scip, conss[c]);
         rhs = SCIPgetRhsLinear(scip, conss[c]);

         if ( SCIPisEQ(scip, lhs, rhs) )
         {
            assert( ! SCIPisInfinity(scip, -lhs) && ! SCIPisInfinity(scip, rhs) );
            nlinconss += 2;
            consssenses[c] = 0;
         }
         else
         {
            if ( ! SCIPisInfinity(scip, -lhs) && ! SCIPisInfinity(scip, rhs) )
            {
               SCIPerrorMessage("Cannot handle ranged rows.\n");
               SCIPfreeBufferArray(scip, &consssenses);
               SCIPfreeBufferArray(scip, &varsenses);
               return SCIP_READERROR; /*lint !e527*/
            }
            else
            {
               assert( SCIPisInfinity(scip, -lhs) || SCIPisInfinity(scip, rhs) );

               if ( ! SCIPisInfinity(scip, -lhs) )
                  consssenses[c] = 1;
               else if ( ! SCIPisInfinity(scip, rhs) )
                  consssenses[c] = -1;

               nlinconss++;
            }
         }
      }
      else
      {
         assert( (strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDP") != 0)
            || (strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDPrank1") != 0) );

         /* count number of SDP constraints (conshdlrGetNConss doesn't seem to work before transformation) */
         ++nsdpconss;

         /* count SDP nonzeros */
         SCIP_CALL( SCIPconsSdpGetNNonz(scip, conss[c], &sdpnnonz, &sdpconstnnonz) );
         totalsdpnnonz += sdpnnonz;
         totalsdpconstnnonz += sdpconstnnonz;
      }
   }

   nblocks = nsdpconss;

   if ( nblocks > 0 && totalsdpnnonz == 0 )
   {
      SCIPerrorMessage("There are %d SDP blocks but no nonzero coefficients. \n", nblocks);
      SCIPfreeBufferArray(scip, &consssenses);
      SCIPfreeBufferArray(scip, &varsenses);
      return SCIP_READERROR; /*lint !e527*/
   }

   if ( nvarbndslinconss + nlinconss > 0 )
      nblocks++;

   /* write number of blocks */
   SCIPinfoMessage(scip, file, "%d\n", nblocks);

   /* write sizes of the SDP blocks and number of linear constraints */
   for (c = 0; c < nconss; c++)
   {
      if ( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDP") != 0 && strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDPrank1")   )
         continue;

      SCIPinfoMessage(scip, file, "%d ", SCIPconsSdpGetBlocksize(scip, conss[c]));
   }

   if ( nvarbndslinconss + nlinconss > 0 )
      SCIPinfoMessage(scip, file, "-%d \n", nvarbndslinconss + nlinconss);
   else
      SCIPinfoMessage(scip, file, "\n");

   /* write the objective values */
   /* If objsense = maximize, multiply objective values with -1 */
   if ( objsense == SCIP_OBJSENSE_MAXIMIZE )
   {
      objcoeff = -1;
      SCIPinfoMessage(scip, NULL, "WARNING: Transforming original maximization problem to a minimization problem by multiplying all objective coefficients by -1. \n");
   }

   for (v = 0; v < nvars; v++)
   {
      SCIP_Real obj;

      obj = SCIPvarGetObj(vars[v]);

      if ( ! SCIPisZero(scip, obj) )
         SCIPinfoMessage(scip, file, "%.15g ", obj * objcoeff);
      else
         SCIPinfoMessage(scip, file, "%.15g ", 0.0);
   }
   SCIPinfoMessage(scip, file, "\n");

   /* write variable bounds as linear constraints */
   if ( nvarbndslinconss > 0 )
   {
      for (c = 0; c < nvars; c++)
      {
         assert(varsenses[c] == 0 || varsenses[c] == -1 || varsenses[c] == 1 );

         if (varsenses[c] == 0 )
            continue;

         if ( varsenses[c] == -1 )
         {
            ++linconsind;
            SCIPinfoMessage(scip, file, "%d %d %d %d -1.0\n", c + 1, nsdpconss + 1, linconsind, linconsind);
         }
         else
         {
            ++linconsind;
            SCIPinfoMessage(scip, file, "%d %d %d %d 1.0\n", c + 1, nsdpconss + 1, linconsind, linconsind);
         }
      }
   }

   if ( nsdpconss > 0 )
   {
      /* allocate memory for SDPdata */
      SCIP_CALL( SCIPallocBufferArray(scip, &sdpnvarnonz, nvars) );
      SCIP_CALL( SCIPallocBufferArray(scip, &sdpcol, totalsdpnnonz) );
      SCIP_CALL( SCIPallocBufferArray(scip, &sdprow, totalsdpnnonz) );
      SCIP_CALL( SCIPallocBufferArray(scip, &sdpval, totalsdpnnonz) );
      SCIP_CALL( SCIPallocBufferArray(scip, &sdpvars, nvars) );
      SCIP_CALL( SCIPallocBufferArray(scip, &sdpconstcol, totalsdpconstnnonz) );
      SCIP_CALL( SCIPallocBufferArray(scip, &sdpconstrow, totalsdpconstnnonz) );
      SCIP_CALL( SCIPallocBufferArray(scip, &sdpconstval, totalsdpconstnnonz) );

      sdparraylength = totalsdpnnonz;
      sdpconstnnonz = totalsdpconstnnonz;
   }

   /* write SDP constraint blocks */
   for (c = 0; c < nconss; c++)
   {
      if ( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDP") == 0
         || strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDPrank1") == 0 )
      {
         /* write SDP nonzeros */
         if ( totalsdpnnonz > 0 )
         {
            /* coefficient matrices */

            /* for lint: */
            assert( sdpnvarnonz != NULL );
            assert( sdpcol != NULL );
            assert( sdprow != NULL );
            assert( sdpval != NULL );
            assert( sdpvars != NULL );
            assert( sdpconstcol != NULL );
            assert( sdpconstrow != NULL );
            assert( sdpconstval != NULL );

            /* initialization for SDPconsSDPGetData-call */
            sdparraylength = totalsdpnnonz;
            sdpconstnnonz = totalsdpconstnnonz;

            SCIP_CALL( SCIPconsSdpGetData(scip, conss[c], &sdpnvars, &sdpnnonz, &sdpblocksize, &sdparraylength,
                  sdpnvarnonz, sdprow, sdpcol, sdpval, sdpvars, &sdpconstnnonz,  sdpconstrow, sdpconstcol,
                  sdpconstval, NULL, NULL, NULL) );

            assert( sdpconstnnonz <= totalsdpconstnnonz );
            assert( sdparraylength <= totalsdpnnonz );

            for (v = 0; v < sdpnvars; v++)
            {
               for (i = 0; i < sdpnvarnonz[v]; i++)
               {
                  int ind;
                  ind = SCIPvarGetProbindex(sdpvars[v]);
                  assert( 0 <= ind && ind < nvars );
                  SCIPinfoMessage(scip, file, "%d %d %d %d %.15g\n", ind + 1, consind + 1, sdprow[v][i]+ 1, sdpcol[v][i] + 1,
                     sdpval[v][i]);
               }
            }

            /* constant matrix */

            /* initialization for SDPconsSDPGetData-call */
            sdparraylength = totalsdpnnonz;

            assert( sdpconstnnonz <= totalsdpconstnnonz );
            assert( sdparraylength <= totalsdpnnonz );

            for (i = 0; i < sdpconstnnonz; i++)
            {
               SCIPinfoMessage(scip, file, "%d %d %d %d %.15g\n", 0, consind + 1, sdpconstrow[i] + 1, sdpconstcol[i] + 1,
                  sdpconstval[i]);
            }
            consind++;
         }
      }
      else
      {
         assert( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "linear") == 0 );

         linconsind++;

         lhs = SCIPgetLhsLinear(scip, conss[c]);
         rhs = SCIPgetRhsLinear(scip, conss[c]);
         conssign = 1;

         /* in case of unconstrained left side and constrained right side swap the inequality by multipling with -1 */
         if ( ! SCIPisInfinity(scip, rhs) && SCIPisInfinity(scip, -lhs) )
         {
            conssign = -1;
            nchangedconss++;
         }

         linvars = SCIPgetVarsLinear(scip, conss[c]);
         linvals = SCIPgetValsLinear(scip, conss[c]);

         if ( ! SCIPisEQ(scip, lhs, rhs) )
         {
            for (v = 0; v < SCIPgetNVarsLinear(scip, conss[c]); v++)
            {
               i = SCIPvarGetProbindex(linvars[v]);
               assert( 0 <= i && i < nvars );
               SCIPinfoMessage(scip, file, "%d %d %d %d %.15g\n", i + 1, nsdpconss + 1, linconsind, linconsind, linvals[v] * conssign);
            }

            /* write the constant part of the LP block */
            if ( conssign < 0 )
               val = SCIPgetRhsLinear(scip, conss[c]);
            else
               val = SCIPgetLhsLinear(scip, conss[c]);

            if ( ! SCIPisZero(scip, val) )
               SCIPinfoMessage(scip, file, "%d %d %d %d %.15g\n", 0, nsdpconss + 1, linconsind, linconsind, val * conssign);
         }
         else  /* write linear constraint block */
         {
            for (v = 0; v < SCIPgetNVarsLinear(scip, conss[c]); v++)
            {
               i = SCIPvarGetProbindex(linvars[v]);
               assert( 0 <= i && i < nvars );
               SCIPinfoMessage(scip, file, "%d %d %d %d %.15g\n", i + 1, nsdpconss + 1, linconsind,linconsind, linvals[v] * conssign);
            }

            /* write the constant part of the LP block */
            if ( conssign < 0 )
               val = SCIPgetRhsLinear(scip, conss[c]);
            else
               val = SCIPgetLhsLinear(scip, conss[c]);

            if ( ! SCIPisZero(scip, val) )
               SCIPinfoMessage(scip, file, "%d %d %d %d %.15g\n", 0, nsdpconss + 1, linconsind, linconsind, val * conssign);

            ++linconsind;

            for (v = 0; v < SCIPgetNVarsLinear(scip, conss[c]); v++)
            {
               i = SCIPvarGetProbindex(linvars[v]);
               assert( 0 <= i && i < nvars );
               SCIPinfoMessage(scip, file, "%d %d %d %d %.15g\n", i + 1, nsdpconss + 1, linconsind,linconsind, linvals[v] * conssign*(-1));
            }

            /* write the constant part of the LP block */
            if ( conssign < 0 )
               val = SCIPgetRhsLinear(scip, conss[c]);
            else
               val = SCIPgetLhsLinear(scip, conss[c]);

            if ( ! SCIPisZero(scip, val) )
               SCIPinfoMessage(scip, file, "%d %d %d %d %.15g\n", 0, nsdpconss + 1, linconsind, linconsind, val * conssign*(-1));
         }
      }
   }

   if ( nchangedconss > 0 )
      SCIPverbMessage(scip, SCIP_VERBLEVEL_HIGH, NULL, "Changed the sign of %d constraints. \n", nchangedconss);

   /* write integrality constraints */
   if ( nbinvars + nintvars > 0 )
   {
      SCIPinfoMessage(scip, file, "*INTEGER\n");
      for (v = 0; v < nbinvars + nintvars; v++)
      {
         assert( SCIPvarIsIntegral(vars[v]) );
         SCIPinfoMessage(scip, file, "*%d\n", v + 1);
      }
   }

   /* write rank-1 SDP constraints (if existing) */
   if ( nrank1sdpblocks > 0 )
   {
      consind = 0;
      SCIPinfoMessage(scip, file, "*RANK1\n");
      for (c = 0; c < nconss; c++)
      {
         if ( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "linear") == 0 )
            continue;

	 if ( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDP") == 0
            || strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDPrank1") == 0 )
	    consind++;

	 if ( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(conss[c])), "SDPrank1") == 0 )
	 {
            assert( SCIPconsSdpShouldBeRankOne(conss[c]) );
            SCIPinfoMessage(scip, file, "*%d\n", consind);
         }
      }
   }

   if ( nsdpconss > 0 )
   {
      SCIPfreeBufferArray(scip, &sdpconstval);
      SCIPfreeBufferArray(scip, &sdpconstrow);
      SCIPfreeBufferArray(scip, &sdpconstcol);
      SCIPfreeBufferArray(scip, &sdpvars);
      SCIPfreeBufferArray(scip, &sdpval);
      SCIPfreeBufferArray(scip, &sdprow);
      SCIPfreeBufferArray(scip, &sdpcol);
      SCIPfreeBufferArray(scip, &sdpnvarnonz);
   }
   SCIPfreeBufferArray(scip, &consssenses);
   SCIPfreeBufferArray(scip, &varsenses);

   *result = SCIP_SUCCESS;

   return SCIP_OKAY;
}


/*
 * reader specific interface methods
 */

/** destructor of reader to free user data (called when SCIP is exiting) */
static
SCIP_DECL_READERFREE(readerFreeSdpa)
{
   SCIP_READERDATA* readerdata;

   assert(strcmp(SCIPreaderGetName(reader), READER_NAME) == 0);
   readerdata = SCIPreaderGetData(reader);
   assert(readerdata != NULL);
   SCIPfreeBlockMemory(scip, &readerdata);

   return SCIP_OKAY;
}

/** includes the SDPA file reader in SCIP */
SCIP_RETCODE SCIPincludeReaderSdpa(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_READERDATA* readerdata = NULL;
   SCIP_READER* reader;

   SCIP_CALL( SCIPallocBlockMemory(scip, &readerdata) );

   /* include reader */
   SCIP_CALL( SCIPincludeReaderBasic(scip, &reader, READER_NAME, READER_DESC, READER_EXTENSION, readerdata) );

   assert( reader != NULL );

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetReaderCopy(scip, reader, readerCopySdpa) );
   SCIP_CALL( SCIPsetReaderRead(scip, reader, readerReadSdpa) );
   SCIP_CALL( SCIPsetReaderWrite(scip, reader, readerWriteSdpa) );
   SCIP_CALL( SCIPsetReaderFree(scip, reader, readerFreeSdpa) );

   return SCIP_OKAY;
}
