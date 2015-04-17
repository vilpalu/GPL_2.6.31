/*
 *Copyright(c)2004,Cisco URP imburses and Network Information Center in Beijing University of Posts and Telecommunications researches.
 *
 *All right reserved
 *
 *File Name: expErrorTable.c
 *File Description: expErrorTable MIB operation.
 *
 *Current Version:1.0
 *Author:JianShun Tong
 *Date:2004.8.20
 */


/*
 * This file was generated by mib2c and is intended for use as
 * a mib module for the ucd-snmp snmpd agent. 
 */


/*
 * This should always be included first before anything else 
 */
#include <net-snmp/net-snmp-config.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif


/*
 * minimal include directives 
 */
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include "header_complex.h"
#include "expErrorTable.h"
#include "expExpressionTable.h"



/*
 * expErrorTable_variables_oid:
 *   this is the top level oid that we want to register under.  This
 *   is essentially a prefix, with the suffix appearing in the
 *   variable below.
 */

oid             expErrorTable_variables_oid[] =
    { 1, 3, 6, 1, 2, 1, 90, 1, 2, 2 };

/*
 * variable2 expErrorTable_variables:
 */

struct variable2 expErrorTable_variables[] = {
    /*
     * magic number        , variable type , ro/rw , callback fn  , L, oidsuffix 
     */
#define	EXPERRORTIME  1
    {EXPERRORTIME,  ASN_UNSIGNED, RONLY, var_expErrorTable, 2, {1, 1}},
#define	EXPERRORINDEX 2
    {EXPERRORINDEX, ASN_INTEGER,  RONLY, var_expErrorTable, 2, {1, 2}},
#define	EXPERRORCODE 3
    {EXPERRORCODE,  ASN_INTEGER,  RONLY, var_expErrorTable, 2, {1, 3}},
#define	EXPERRORINSTANCE 4
    {EXPERRORINSTANCE, ASN_OBJECT_ID, RONLY, var_expErrorTable, 2, {1, 4}}
};

extern struct header_complex_index *expExpressionTableStorage;


void
init_expErrorTable(void)
{
    DEBUGMSGTL(("expErrorTable", "initializing...  "));



    /*
     * register ourselves with the agent to handle our mib tree 
     */
    REGISTER_MIB("expErrorTable",
                 expErrorTable_variables, variable2,
                 expErrorTable_variables_oid);

    DEBUGMSGTL(("expErrorTable", "done.\n"));
}



unsigned char  *
var_expErrorTable(struct variable *vp,
                  oid * name,
                  size_t *length,
                  int exact, size_t *var_len, WriteMethod ** write_method)
{
    struct expExpressionTable_data *StorageTmp = NULL;

    DEBUGMSGTL(("expErrorTable", "var_expErrorTable: Entering...  \n"));
    /*
     * this assumes you have registered all your data properly
     */
    if ((StorageTmp =
         header_complex(expExpressionTableStorage, vp, name, length, exact,
                        var_len, write_method)) == NULL)
        return NULL;

    /*
     * this is where we do the value assignments for the mib results.
     */
    switch (vp->magic) {

    case EXPERRORTIME:
        *var_len = sizeof(StorageTmp->expErrorTime);
        return (u_char *) & StorageTmp->expErrorTime;

    case EXPERRORINDEX:
        *var_len = sizeof(StorageTmp->expErrorIndex);
        return (u_char *) & StorageTmp->expErrorIndex;

    case EXPERRORCODE:
        *var_len = sizeof(StorageTmp->expErrorCode);
        return (u_char *) & StorageTmp->expErrorCode;

    case EXPERRORINSTANCE:
        *var_len = StorageTmp->expErrorInstanceLen * sizeof(oid);
        return (u_char *) StorageTmp->expErrorInstance;
    }
}
