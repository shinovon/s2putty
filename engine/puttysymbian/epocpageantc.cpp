/*    epocpageantc.cpp
 *
 * Symbian OS implementation of Pagent client code. Currently a stub.
 *
 * Copyright 2002 Petteri Kangaslampi
 *
 * See license.txt for full copyright and license information.
*/

extern "C" {
#include "putty.h"

int agent_exists(void)
{
    return FALSE;
}

void agent_cancel_query(agent_pending_query *q)
{
}

agent_pending_query *agent_query(void * /*in*/, int /*inlen*/, void **out, int *outlen,
                void (* /*callback*/ )(void *, void *, int),
                void * /*callback_ctx*/)
{
    *out = NULL;
    *outlen = 0;
    return NULL;
}
}
