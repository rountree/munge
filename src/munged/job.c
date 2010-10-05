/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  UCRL-CODE-155910.
 *
 *  This file is part of the MUNGE Uid 'N' Gid Emporium (MUNGE).
 *  For details, see <http://munge.googlecode.com/>.
 *
 *  MUNGE is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.  Additionally for the MUNGE library (libmunge), you
 *  can redistribute it and/or modify it under the terms of the GNU Lesser
 *  General Public License as published by the Free Software Foundation,
 *  either version 3 of the License, or (at your option) any later version.
 *
 *  MUNGE is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  and GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  and GNU Lesser General Public License along with MUNGE.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <munge.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "conf.h"
#include "dec.h"
#include "enc.h"
#include "fd.h"
#include "log.h"
#include "m_msg.h"
#include "munge_defs.h"
#include "str.h"
#include "work.h"


/*****************************************************************************
 *  Extern Variables
 *****************************************************************************/

extern volatile sig_atomic_t done;      /* defined in munged.c               */


/*****************************************************************************
 *  Private Prototypes
 *****************************************************************************/

static void _job_exec (m_msg_t m);


/*****************************************************************************
 *  Public Functions
 *****************************************************************************/

void
job_accept (conf_t conf)
{
    work_p  w;
    m_msg_t m;
    int     sd;

    assert (conf != NULL);
    assert (conf->ld >= 0);

    if (!(w = work_init ((work_func_t) _job_exec, conf->nthreads))) {
        log_errno (EMUNGE_SNAFU, LOG_ERR,
            "Unable to create %d work thread%s", conf->nthreads,
            ((conf->nthreads > 1) ? "s" : ""));
    }
    log_msg (LOG_INFO, "Created %d work thread%s", conf->nthreads,
            ((conf->nthreads > 1) ? "s" : ""));

    while (!done) {
        if ((sd = accept (conf->ld, NULL, NULL)) < 0) {
            switch (errno) {
                case ECONNABORTED:
                case EINTR:
                    continue;
                case EMFILE:
                case ENFILE:
                case ENOBUFS:
                case ENOMEM:
                    log_msg (LOG_INFO,
                        "Suspended new connections while processing backlog");
                    work_wait (w);
                    continue;
                default:
                    log_errno (EMUNGE_SNAFU, LOG_ERR,
                        "Unable to accept connection");
                    break;
            }
        }
        /*  With fd_timed_read_n(), a poll() is performed before any read()
         *    in order to provide timeouts and ensure the read() won't block.
         *    As such, it shouldn't be necessary to set the client socket as
         *    non-blocking.  However according to the Linux poll(2) and
         *    select(2) manpages, spurious readiness notifications can occur.
         *    poll()/select() may report a socket as ready for reading while
         *    the subsequent read() blocks.  This could happen when data has
         *    arrived, but upon examination is discarded due to an invalid
         *    checksum.  To protect against this, the client socket is set
         *    non-blocking and EAGAIN is handled appropriately.
         */
        if (fd_set_nonblocking (sd) < 0) {
            close (sd);
            log_msg (LOG_WARNING,
                "Unable to set nonblocking client socket: %s",
                strerror (errno));
        }
        else if (m_msg_create (&m) != EMUNGE_SUCCESS) {
            close (sd);
            log_msg (LOG_WARNING, "Unable to create client request");
        }
        else if (m_msg_bind (m, sd) != EMUNGE_SUCCESS) {
            m_msg_destroy (m);
            log_msg (LOG_WARNING, "Unable to bind socket for client request");
        }
        else if (work_queue (w, m) < 0) {
            m_msg_destroy (m);
            log_msg (LOG_WARNING, "Unable to queue client request");
        }
    }
    log_msg (LOG_NOTICE, "Exiting on signal=%d", done);
    work_fini (w, 1);
    return;
}


/*****************************************************************************
 *  Private Functions
 *****************************************************************************/

static void
_job_exec (m_msg_t m)
{
/*  Receives and responds to the message request [m].
 */
    munge_err_t  e;
    const char  *p;

    assert (m != NULL);

    e = m_msg_recv (m, MUNGE_MSG_UNDEF, MUNGE_MAXIMUM_REQ_LEN);
    if (e == EMUNGE_SUCCESS) {
        switch (m->type) {
            case MUNGE_MSG_ENC_REQ:
                enc_process_msg (m);
                break;
            case MUNGE_MSG_DEC_REQ:
                dec_process_msg (m);
                break;
            default:
                m_msg_set_err (m, EMUNGE_SNAFU,
                    strdupf ("Invalid message type %d", m->type));
                break;
        }
    }
    if (m->error_num != EMUNGE_SUCCESS) {
        p = (m->error_str != NULL)
            ? m->error_str
            : munge_strerror (m->error_num);
        log_msg (LOG_INFO, "%s", p);
    }
    m_msg_destroy (m);
    return;
}
