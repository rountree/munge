/*****************************************************************************
 *  $Id: msg_server.c,v 1.10 2004/04/03 21:53:00 dun Exp $
 *****************************************************************************
 *  This file is part of the Munge Uid 'N' Gid Emporium (MUNGE).
 *  For details, see <http://www.llnl.gov/linux/munge/>.
 *
 *  Copyright (C) 2003-2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-155910.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License;
 *  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
 *  Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <string.h>
#include "dec_v1.h"
#include "enc_v1.h"
#include "log.h"
#include "munge_defs.h"
#include "munge_msg.h"
#include "str.h"


/*****************************************************************************
 *  Extern Functions
 *****************************************************************************/

void
munge_msg_server_thread (munge_msg_t m)
{
/*  Receives and responds to the message request [m].
 */
    munge_err_t  e;
    const char *p;

    assert (m != NULL);

    if ((e = _munge_msg_recv (m)) != EMUNGE_SUCCESS) {
        ; /* fall out of if clause, log error, and drop request */
    }
    else if (m->head.version > MUNGE_MSG_VERSION) {
        _munge_msg_set_err (m, EMUNGE_SNAFU,
            strdupf ("Invalid message version %d", m->head.version));
    }
    else {
        switch (m->head.type) {
            case MUNGE_MSG_ENC_REQ:
                enc_v1_process_msg (m);
                break;
            case MUNGE_MSG_DEC_REQ:
                dec_v1_process_msg (m);
                break;
            default:
                _munge_msg_set_err (m, EMUNGE_SNAFU,
                    strdupf ("Invalid message type %d", m->head.type));
                break;
        }
    }
    if (m->errnum != EMUNGE_SUCCESS) {
        p = (m->errstr != NULL) ? m->errstr : munge_strerror (m->errnum);
        log_msg (LOG_INFO, "%s", p);
    }
    _munge_msg_destroy (m);
    return;
}


void
err_v1_response (munge_msg_t m)
{
/*  If an error condition has been set for the message [m], copy it to the
 *    version-specific message format for transport over the domain socket.
 */
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */

    assert (m != NULL);

    if (m->errnum != EMUNGE_SUCCESS) {
        m1 = m->pbody;
        m1->error_num = m->errnum;
        m1->error_str =
            strdup (m->errstr ? m->errstr : munge_strerror (m->errnum));
        m1->error_len = strlen (m1->error_str) + 1;
    }
    return;
}
