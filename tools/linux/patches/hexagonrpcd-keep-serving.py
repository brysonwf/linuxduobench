#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
r"""hexagonrpcd: keep serving after a request it can't handle, instead of exiting.

invoke_requested_procedure() returns 1 (with an AEE error already in `result`) for an unsupported handle/method or
bad arguments, and run_fastrpc_listener() treats that as fatal. On the Surface Duo's SM8150 sensors firmware the DSP
asks for a local "sns_registry" interface hexagonrpcd doesn't have, gets "not found", then calls handle 0xffffffff -
and the daemon exits ("Unsupported handle: 4294967295"), so the DSP loses its file service (hexagonrpc issue #23).
With this patch the error goes back to the DSP and the loop continues. The stale `returned` pointer (freed just
before) is cleared and the error reply carries no output buffers.
Idempotent. usage: python3 -I hexagonrpcd-keep-serving.py <hexagonrpc-source-dir>   (tested on v0.3.2)"""
import sys
p = sys.argv[1] + '/hexagonrpcd/listener.c'
s = open(p, encoding='utf-8').read()
if 'keep serving' not in s:
    old = ("\t\tret = invoke_requested_procedure(n_ifaces, ifaces,\n"
           "\t\t\t\t\t\t handle, sc, &result,\n"
           "\t\t\t\t\t\t decoded, &returned);\n"
           "\t\tif (ret)\n"
           "\t\t\tbreak;\n")
    new = ("\t\tret = invoke_requested_procedure(n_ifaces, ifaces,\n"
           "\t\t\t\t\t\t handle, sc, &result,\n"
           "\t\t\t\t\t\t decoded, &returned);\n"
           "\t\tif (ret == 1) {\n"
           "\t\t\t/* keep serving: the AEE error in result goes back to the DSP, with no output buffers */\n"
           "\t\t\treturned = NULL;\n"
           "\t\t\tsc = REMOTE_SCALARS_MAKE(REMOTE_SCALARS_METHOD(sc), REMOTE_SCALARS_INBUFS(sc), 0);\n"
           "\t\t\tret = 0;\n"
           "\t\t} else if (ret)\n"
           "\t\t\tbreak;\n")
    assert s.count(old) == 1, 'listener loop anchor not found'
    s = s.replace(old, new)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
    print('patched', p)
