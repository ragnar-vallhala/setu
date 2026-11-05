/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/*
 * The setu-ntacld protocol (README.md, "NT security descriptors").
 *
 * A client connects to SETU_NTACL_SOCKET (SOCK_SEQPACKET) and sends one
 * message per request: a struct setu_ntacl_request followed by `len` bytes
 * of data, with exactly one file descriptor in an SCM_RIGHTS control
 * message: the open file whose attribute is to be written. The helper never
 * takes a path. Each request gets one struct setu_ntacl_reply: 0 or an errno
 * value. The connection may be kept for more requests.
 *
 * The wineserver's copy of these definitions is in
 * personality/wine/server/filesec.c; keep the two in step.
 */
#ifndef SETU_NTACL_H
#define SETU_NTACL_H

#include <stdint.h>

#define SETU_NTACL_SOCKET  "/run/setu/ntacl.sock"
#define SETU_NTACL_XATTR   "security.NTACL"
#define SETU_NTACL_MAGIC   0x4c43414eu  /* "NACL" */
#define SETU_NTACL_VERSION 1
/* the attribute: one format byte (1) and a self-relative security
 * descriptor ([MS-DTYP] 2.4.6), at most 64 KiB */
#define SETU_NTACL_FORMAT  1
#define SETU_NTACL_MAX     (1 + 65536)

enum setu_ntacl_op
{
    SETU_NTACL_SET    = 1,  /* data: the attribute's new value */
    SETU_NTACL_REMOVE = 2,  /* no data */
};

struct setu_ntacl_request
{
    uint32_t magic;    /* SETU_NTACL_MAGIC */
    uint16_t version;  /* SETU_NTACL_VERSION */
    uint16_t op;       /* enum setu_ntacl_op */
    uint32_t len;      /* bytes of data after the header */
};

struct setu_ntacl_reply
{
    uint32_t magic;    /* SETU_NTACL_MAGIC */
    int32_t  error;    /* 0, or an errno value */
};

#endif /* SETU_NTACL_H */
