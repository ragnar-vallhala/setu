/*
 * Setu: NT security descriptors of files and directories, stored in an xattr
 *
 * Copyright 2026 The Setu Authors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/*
 * A file's or directory's full security descriptor (owner, group, DACL,
 * SACL) is stored in an extended attribute: one format byte (1) followed by
 * the descriptor in the self-relative layout of [MS-DTYP] 2.4.6
 * SECURITY_DESCRIPTOR (docs/ARCHITECTURE.md §3.3).
 *
 * The attribute is "security.NTACL". Anyone may read a "security.*"
 * attribute, so the server reads it itself; writing one needs CAP_SYS_ADMIN,
 * so the server hands the file's descriptor to the setu-ntacld helper
 * (services/fs/README.md) over a unix socket and the helper writes it. The
 * wineserver itself stays unprivileged. Where there is no helper (developer
 * machines, CI) or the file is not on a Setu volume, the descriptor goes to
 * "user.setu.sd" instead, which an ordinary user may write.
 *
 * Files without either attribute keep Wine's behaviour: their security
 * descriptor is derived from the Unix mode, and no NT access check is made
 * when they are opened.
 */

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "file.h"
#include "handle.h"
#include "thread.h"
#include "request.h"
#include "process.h"
#include "security.h"

#define XATTR_NTACL_NAME "security.NTACL"
#define XATTR_SD_NAME    "user.setu.sd"
#define XATTR_SD_FORMAT  1
#define XATTR_SD_MAX     65536

/* The setu-ntacld protocol (services/fs/setu-ntacl.h, kept in step by hand):
 * a SOCK_SEQPACKET message of a request header and its data, with the file's
 * descriptor in SCM_RIGHTS; the reply is an errno value. */
#define NTACL_SOCKET    "/run/setu/ntacl.sock"
#define NTACL_MAGIC     0x4c43414e  /* "NACL" */
#define NTACL_VERSION   1
#define NTACL_OP_SET    1

struct ntacl_request
{
    unsigned int   magic;
    unsigned short version;
    unsigned short op;
    unsigned int   len;
};

struct ntacl_reply
{
    unsigned int   magic;
    int            error;
};

/* [MS-DTYP] 2.4.6 self-relative security descriptor header */
struct sd_relative
{
    unsigned char  revision;
    unsigned char  sbz1;
    unsigned short control;
    unsigned int   owner;
    unsigned int   group;
    unsigned int   sacl;
    unsigned int   dacl;
};

static struct security_descriptor *parse_stored_sd( const unsigned char *buf, size_t len )
{
    struct sd_relative hdr;
    struct security_descriptor *sd;
    const unsigned char *rel;
    data_size_t owner_len = 0, group_len = 0, sacl_len = 0, dacl_len = 0, size;
    char *ptr;

    if (len < 1 + sizeof(hdr) || buf[0] != XATTR_SD_FORMAT) return NULL;
    rel = buf + 1;
    len -= 1;
    memcpy( &hdr, rel, sizeof(hdr) );
    if (hdr.revision != SECURITY_DESCRIPTOR_REVISION) return NULL;

    /* lengths of the parts, bounded by the buffer */
    if (hdr.owner)
    {
        const struct sid *sid = (const struct sid *)(rel + hdr.owner);
        if (hdr.owner > len - offsetof( struct sid, sub_auth[0] )) return NULL;
        owner_len = offsetof( struct sid, sub_auth[sid->sub_count] );
        if (owner_len > len - hdr.owner) return NULL;
    }
    if (hdr.group)
    {
        const struct sid *sid = (const struct sid *)(rel + hdr.group);
        if (hdr.group > len - offsetof( struct sid, sub_auth[0] )) return NULL;
        group_len = offsetof( struct sid, sub_auth[sid->sub_count] );
        if (group_len > len - hdr.group) return NULL;
    }
    if (hdr.sacl && (hdr.control & SE_SACL_PRESENT))
    {
        const struct acl *acl = (const struct acl *)(rel + hdr.sacl);
        if (hdr.sacl > len - sizeof(struct acl)) return NULL;
        sacl_len = acl->size;
        if (sacl_len > len - hdr.sacl) return NULL;
    }
    if (hdr.dacl && (hdr.control & SE_DACL_PRESENT))
    {
        const struct acl *acl = (const struct acl *)(rel + hdr.dacl);
        if (hdr.dacl > len - sizeof(struct acl)) return NULL;
        dacl_len = acl->size;
        if (dacl_len > len - hdr.dacl) return NULL;
    }

    size = sizeof(*sd) + owner_len + group_len + sacl_len + dacl_len;
    if (!(sd = mem_alloc( size ))) return NULL;
    sd->control   = hdr.control & ~SE_SELF_RELATIVE;
    sd->owner_len = owner_len;
    sd->group_len = group_len;
    sd->sacl_len  = sacl_len;
    sd->dacl_len  = dacl_len;
    ptr = (char *)(sd + 1);
    ptr = mem_append( ptr, rel + hdr.owner, owner_len );
    ptr = mem_append( ptr, rel + hdr.group, group_len );
    ptr = mem_append( ptr, rel + hdr.sacl, sacl_len );
    mem_append( ptr, rel + hdr.dacl, dacl_len );

    if (!sd_is_valid( sd, size ))
    {
        free( sd );
        return NULL;
    }
    return sd;
}

static unsigned char *build_stored_sd( const struct security_descriptor *sd, size_t *ret_len )
{
    struct sd_relative hdr;
    unsigned char *buf, *ptr;
    unsigned int offset = sizeof(hdr);
    int present;
    const struct acl *sacl = sd_get_sacl( sd, &present ), *dacl = sd_get_dacl( sd, &present );
    size_t len = 1 + sizeof(hdr) + sd->owner_len + sd->group_len + sd->sacl_len + sd->dacl_len;

    if (!(buf = mem_alloc( len ))) return NULL;
    memset( &hdr, 0, sizeof(hdr) );
    hdr.revision = SECURITY_DESCRIPTOR_REVISION;
    hdr.control  = (sd->control | SE_SELF_RELATIVE) & 0xffff;
    if (sd->owner_len) { hdr.owner = offset; offset += sd->owner_len; }
    if (sd->group_len) { hdr.group = offset; offset += sd->group_len; }
    if (sacl) { hdr.sacl = offset; offset += sd->sacl_len; }
    if (dacl) { hdr.dacl = offset; offset += sd->dacl_len; }

    buf[0] = XATTR_SD_FORMAT;
    ptr = buf + 1;
    memcpy( ptr, &hdr, sizeof(hdr) );
    ptr += sizeof(hdr);
    ptr = mem_append( ptr, sd + 1, sd->owner_len + sd->group_len + sd->sacl_len + sd->dacl_len );
    *ret_len = len;
    return buf;
}

#ifdef HAVE_SYS_XATTR_H

/* read one of the attributes: of unix_fd, or of name relative to the cwd */
static struct security_descriptor *read_stored_sd( int unix_fd, const char *name, const char *attr )
{
    unsigned char buf[XATTR_SD_MAX];
    ssize_t len;

    if (name) len = getxattr( name, attr, buf, sizeof(buf) );
    else len = fgetxattr( unix_fd, attr, buf, sizeof(buf) );
    if (len <= 0) return NULL;
    /* a security.NTACL in another format (Samba's) is not ours: ignore it */
    return parse_stored_sd( buf, len );
}

static struct security_descriptor *get_stored_sd( int unix_fd, const char *name )
{
    struct security_descriptor *sd;

    if ((sd = read_stored_sd( unix_fd, name, XATTR_NTACL_NAME ))) return sd;
    return read_stored_sd( unix_fd, name, XATTR_SD_NAME );
}

#ifdef __linux__
static int ntacl_fd = -1;

static int ntacl_connect(void)
{
    const char *path = getenv( "SETU_NTACL_SOCKET" );
    struct sockaddr_un addr;
    struct timeval tv = { 5, 0 };
    int fd;

    if (!path || !path[0]) path = NTACL_SOCKET;
    if (strlen( path ) >= sizeof(addr.sun_path)) return -1;
    memset( &addr, 0, sizeof(addr) );
    addr.sun_family = AF_UNIX;
    strcpy( addr.sun_path, path );
    if ((fd = socket( AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0 )) == -1) return -1;
    setsockopt( fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv) );
    setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) );
    if (connect( fd, (struct sockaddr *)&addr, sizeof(addr) ) == -1)
    {
        close( fd );
        return -1;
    }
    return fd;
}

/* ask setu-ntacld to write security.NTACL of unix_fd; returns 0, the
 * helper's errno value, or -1 when there is no helper */
static int ntacl_set( int unix_fd, const void *data, size_t len )
{
    struct ntacl_request req = { NTACL_MAGIC, NTACL_VERSION, NTACL_OP_SET, len };
    struct ntacl_reply reply;
    struct iovec iov[2];
    union { struct cmsghdr hdr; char buf[CMSG_SPACE(sizeof(int))]; } control;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    int attempt;

    iov[0].iov_base = &req;
    iov[0].iov_len  = sizeof(req);
    iov[1].iov_base = (void *)data;
    iov[1].iov_len  = len;
    for (attempt = 0; attempt < 2; attempt++)
    {
        /* a connection that broke (the helper restarted) is made again once */
        if (ntacl_fd == -1 && (ntacl_fd = ntacl_connect()) == -1) return -1;

        memset( &msg, 0, sizeof(msg) );
        memset( &control, 0, sizeof(control) );
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;
        msg.msg_control = control.buf;
        msg.msg_controllen = sizeof(control.buf);
        cmsg = CMSG_FIRSTHDR( &msg );
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy( CMSG_DATA(cmsg), &unix_fd, sizeof(int) );

        if (sendmsg( ntacl_fd, &msg, MSG_NOSIGNAL ) == (ssize_t)(sizeof(req) + len) &&
            recv( ntacl_fd, &reply, sizeof(reply), 0 ) == sizeof(reply) && reply.magic == NTACL_MAGIC)
            return reply.error;
        close( ntacl_fd );
        ntacl_fd = -1;
    }
    return EIO;
}
#else
static int ntacl_set( int unix_fd, const void *data, size_t len ) { return -1; }
#endif

#endif  /* HAVE_SYS_XATTR_H */

/* read the stored security descriptor of an open file, NULL if there is none */
struct security_descriptor *fd_get_stored_sd( int unix_fd )
{
#ifdef HAVE_SYS_XATTR_H
    if (unix_fd == -1) return NULL;
    return get_stored_sd( unix_fd, NULL );
#else
    return NULL;
#endif
}

/* read the stored security descriptor of a path, relative to root_fd if not -1 */
struct security_descriptor *path_get_stored_sd( int root_fd, const char *name )
{
#ifdef HAVE_SYS_XATTR_H
    struct security_descriptor *sd;

    if (root_fd != -1)
    {
        if (!name[0] || !strcmp( name, "." )) return fd_get_stored_sd( root_fd );
        if (fchdir( root_fd ) == -1) return NULL;
    }
    sd = get_stored_sd( -1, name );
    if (root_fd != -1) fchdir( server_dir_fd );
    return sd;
#else
    return NULL;
#endif
}

/* store a security descriptor on an open file; returns 0 or an errno value */
int fd_store_sd( int unix_fd, const struct security_descriptor *sd )
{
#ifdef HAVE_SYS_XATTR_H
    struct security_descriptor *ntacl;
    unsigned char *buf;
    size_t len;
    int ret;

    if (!(buf = build_stored_sd( sd, &len ))) return ENOMEM;
    ret = ntacl_set( unix_fd, buf, len );
    if (!ret)
    {
        /* a fallback copy from before would only be stale */
        fremovexattr( unix_fd, XATTR_SD_NAME );
    }
    else if (ret == -1 || ret == EXDEV || ret == ENOTSUP || ret == EOPNOTSUPP)
    {
        /* no helper, or not a Setu volume: the fallback attribute, unless a
         * security.NTACL we can't change would hide it */
        if ((ntacl = read_stored_sd( unix_fd, NULL, XATTR_NTACL_NAME )))
        {
            free( ntacl );
            ret = EPERM;
        }
        else ret = fsetxattr( unix_fd, XATTR_SD_NAME, buf, len, 0 ) == -1 ? errno : 0;
    }
    free( buf );
    return ret;
#else
    return ENOTSUP;
#endif
}

/* the owner of a file without a stored security descriptor: the token's
 * default owner (TokenOwner, Administrators for an elevated token) for the
 * user's own files, as for the files it creates on Windows (Windows 10
 * oracle: FileSecurity file_empty_dacl_owner_reads) */
const struct sid *file_default_owner( uid_t uid )
{
    if (uid == getuid() && current && current->process->token)
        return token_get_owner( current->token ? current->token : current->process->token );
    return security_unix_uid_to_sid( uid );
}

/* the parent directory of a server-side file name, relative to the same root */
char *get_parent_name( const char *name )
{
    const char *p = name + strlen( name );
    char *ret;

    while (p > name && p[-1] == '/') p--;  /* trailing slashes */
    while (p > name && p[-1] != '/') p--;
    while (p > name + 1 && p[-1] == '/') p--;
    if (p == name) return strdup( name[0] == '/' ? "/" : "." );
    if (!(ret = mem_alloc( p - name + 1 ))) return NULL;
    memcpy( ret, name, p - name );
    ret[p - name] = 0;
    return ret;
}

static const struct generic_map file_mapping =
{
    FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS
};

static const struct sid creator_owner_sid = { SID_REVISION, 1, SECURITY_CREATOR_SID_AUTHORITY, { SECURITY_CREATOR_OWNER_RID } };
static const struct sid creator_group_sid = { SID_REVISION, 1, SECURITY_CREATOR_SID_AUTHORITY, { SECURITY_CREATOR_GROUP_RID } };

static struct token *current_token(void)
{
    return current->token ? current->token : current->process->token;
}

static int privilege_enabled( struct token *token, struct luid luid )
{
    const struct luid_attr priv = { luid, 0 };
    return token_check_privileges( token, TRUE, &priv, 1, NULL );
}

static struct security_descriptor *copy_sd( const struct security_descriptor *sd )
{
    data_size_t size = sizeof(*sd) + sd->owner_len + sd->group_len + sd->sacl_len + sd->dacl_len;
    return memdup( sd, size );
}

/* the stored security descriptor becomes the object's descriptor */
struct security_descriptor *file_sd_get( struct object *obj, int unix_fd )
{
    struct security_descriptor *sd = fd_get_stored_sd( unix_fd );

    if (!sd) return NULL;
    free( obj->sd );
    obj->sd = sd;
    return sd;
}

#define DACL_CONTROL_BITS (SE_DACL_PRESENT | SE_DACL_DEFAULTED | SE_DACL_AUTO_INHERIT_REQ | \
                           SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED)
#define SACL_CONTROL_BITS (SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_AUTO_INHERIT_REQ | \
                           SE_SACL_AUTO_INHERITED | SE_SACL_PROTECTED)

/* map the generic rights of the effective ACEs of an ACL in place */
static void map_acl_generic( struct acl *acl )
{
    struct ace *ace;
    unsigned int i;

    if (!acl) return;
    for (i = 0, ace = ace_first( acl ); i < acl->count; i++, ace = ace_next( ace ))
    {
        if (ace->flags & INHERIT_ONLY_ACE) continue;
        if (ace->type != ACCESS_ALLOWED_ACE_TYPE && ace->type != ACCESS_DENIED_ACE_TYPE &&
            ace->type != SYSTEM_AUDIT_ACE_TYPE) continue;
        ace->mask = map_access( ace->mask, &file_mapping );
    }
}

/* whether the token may put the labels of a SACL on an object: not above its
 * own integrity level without SeRelabelPrivilege (Windows 10 oracle:
 * FileSecurity file_label_system) */
static int labels_allowed( struct token *token, const struct security_descriptor *sd )
{
    const struct acl *sacl;
    const struct ace *ace;
    unsigned int i;
    int present;

    if (!(sacl = sd_get_sacl( sd, &present ))) return TRUE;
    for (i = 0, ace = ace_first( sacl ); i < sacl->count; i++, ace = ace_next( ace ))
    {
        const struct sid *sid = (const struct sid *)(ace + 1);
        if (ace->type != SYSTEM_MANDATORY_LABEL_ACE_TYPE || !sid->sub_count) continue;
        if (sid->sub_auth[sid->sub_count - 1] > token_get_integrity( token ) &&
            !privilege_enabled( token, SeRelabelPrivilege ))
            return FALSE;
    }
    return TRUE;
}

/* an empty SACL is stored as a NULL one (Windows 10 oracle: FileSecurity
 * file_sacl_empty, file_label_none) */
static void drop_empty_sacl( struct security_descriptor *sd )
{
    const struct acl *sacl;
    int present;
    char *ptr;

    if (!(sacl = sd_get_sacl( sd, &present )) || sacl->count) return;
    ptr = (char *)(sd + 1) + sd->owner_len + sd->group_len;
    memmove( ptr, ptr + sd->sacl_len, sd->dacl_len );
    sd->sacl_len = 0;
}

/* Set parts of a file's security descriptor and store the result. Returns 1 on
 * success, 0 on failure (error set) and -1 if the file system can't store it. */
int file_sd_set( struct object *obj, int unix_fd, const struct security_descriptor *sd,
                 unsigned int set_info )
{
    struct token *token = current_token();
    struct security_descriptor *old, *cur;
    unsigned int control, keep = SE_OWNER_DEFAULTED | SE_GROUP_DEFAULTED | DACL_CONTROL_BITS | SACL_CONTROL_BITS;
    int err;

    if (set_info & OWNER_SECURITY_INFORMATION)
    {
        const struct sid *owner = sd_get_owner( sd );
        if (!owner)
        {
            set_error( STATUS_INVALID_SECURITY_DESCR );
            return 0;
        }
        /* a token may only set an owner it holds, unless it may restore files */
        if (!token_sid_can_own( token, owner ) && !privilege_enabled( token, SeRestorePrivilege ))
        {
            set_error( STATUS_INVALID_OWNER );
            return 0;
        }
        keep &= ~SE_OWNER_DEFAULTED;
    }
    if ((set_info & LABEL_SECURITY_INFORMATION) && !labels_allowed( token, sd ))
    {
        set_error( STATUS_INVALID_LABEL );
        return 0;
    }
    if (set_info & GROUP_SECURITY_INFORMATION) keep &= ~SE_GROUP_DEFAULTED;
    if (set_info & DACL_SECURITY_INFORMATION) keep &= ~DACL_CONTROL_BITS;
    if (set_info & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)) keep &= ~SACL_CONTROL_BITS;

    if (!(cur = fd_get_stored_sd( unix_fd )))
    {
        /* the first time: start from the descriptor derived from the mode */
        if (!obj->sd && !obj->ops->get_sd( obj )) return -1;
        if (!(cur = copy_sd( obj->sd ))) return 0;
    }
    control = cur->control;
    old = obj->sd;
    obj->sd = cur;
    if (!set_sd_defaults_from_token( obj, sd, set_info, token ))
    {
        free( obj->sd );
        obj->sd = old;
        return 0;
    }
    obj->sd->control = (obj->sd->control & ~keep) | (control & keep);
    /* an ACL set is auto-inherited only when the caller asks for it with the
     * *_AUTO_INHERIT_REQ bit, as SetSecurityInfo does (Windows 10 oracle:
     * FileSecurity dir_auto_inherited_flags, handle_set_dir_dacl) */
    if (set_info & DACL_SECURITY_INFORMATION)
    {
        obj->sd->control &= ~SE_DACL_AUTO_INHERITED;
        if (sd->control & SE_DACL_AUTO_INHERIT_REQ) obj->sd->control |= SE_DACL_AUTO_INHERITED;
    }
    if (set_info & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION))
    {
        obj->sd->control &= ~SE_SACL_AUTO_INHERITED;
        if (sd->control & SE_SACL_AUTO_INHERIT_REQ) obj->sd->control |= SE_SACL_AUTO_INHERITED;
    }
    obj->sd->control &= ~(SE_DACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERIT_REQ);
    if (set_info & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)) drop_empty_sacl( obj->sd );
    /* generic rights are stored mapped (Windows 10 oracle: FileSecurity file_dacl_generic) */
    {
        int present;
        map_acl_generic( (struct acl *)sd_get_dacl( obj->sd, &present ) );
        map_acl_generic( (struct acl *)sd_get_sacl( obj->sd, &present ) );
    }

    if ((err = fd_store_sd( unix_fd, obj->sd )))
    {
        free( obj->sd );
        obj->sd = old;
        if (err == ENOTSUP || err == EOPNOTSUPP || err == ENOSYS) return -1;
        errno = err;
        file_set_error();
        return 0;
    }
    free( old );
    return 1;
}

/* the ACE (or ACEs) a new object inherits from a parent ACE, or how many bytes they need */
static data_size_t emit_ace( const struct ace *ace, unsigned char inh, const struct sid *owner,
                             const struct sid *group, const struct generic_map *map, struct ace *out );

/* the ACEs a new object gets from one parent ACE; they are marked inherited
 * only when the parent's ACL is auto-inherited */
static data_size_t inherit_ace( const struct ace *ace, int is_container, int auto_inherit,
                                const struct sid *owner, const struct sid *group,
                                const struct generic_map *map, struct ace *out )
{
    unsigned char flags = ace->flags, inh;

    if (ace->type != ACCESS_ALLOWED_ACE_TYPE && ace->type != ACCESS_DENIED_ACE_TYPE &&
        ace->type != SYSTEM_AUDIT_ACE_TYPE && ace->type != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
        return 0;

    if (is_container)
    {
        if (flags & CONTAINER_INHERIT_ACE)
        {
            inh = (flags & ~INHERIT_ONLY_ACE) | INHERITED_ACE;
            if (flags & NO_PROPAGATE_INHERIT_ACE)
                inh &= ~(OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | NO_PROPAGATE_INHERIT_ACE);
        }
        else if ((flags & OBJECT_INHERIT_ACE) && !(flags & NO_PROPAGATE_INHERIT_ACE))
            inh = flags | INHERIT_ONLY_ACE | INHERITED_ACE;
        else return 0;
    }
    else
    {
        if (!(flags & OBJECT_INHERIT_ACE)) return 0;
        inh = (flags & ~VALID_INHERIT_FLAGS) | INHERITED_ACE;
    }
    if (!auto_inherit) inh &= ~INHERITED_ACE;
    return emit_ace( ace, inh, owner, group, map, out );
}

/* an ACE with the given flags: if it is effective, creator SIDs are replaced
 * and generic rights mapped, and a changed ACE that still propagates is
 * followed by an inherit-only copy of the original */
static data_size_t emit_ace( const struct ace *ace, unsigned char inh, const struct sid *owner,
                             const struct sid *group, const struct generic_map *map, struct ace *out )
{
    const struct sid *sid = (const struct sid *)(ace + 1);
    const struct sid *eff_sid = sid;
    unsigned int mask = ace->mask;
    data_size_t size;
    int split;

    if (equal_sid( sid, &creator_owner_sid )) eff_sid = owner;
    else if (equal_sid( sid, &creator_group_sid )) eff_sid = group;
    split = !(inh & INHERIT_ONLY_ACE) &&
            (eff_sid != sid || (mask & (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL)));

    if (!split)
    {
        size = sizeof(*ace) + sid_len( sid );
        if (out) set_ace( out, sid, ace->type, inh, mask );
        return size;
    }

    /* the effective ACE, with the creator SID replaced and generic rights mapped */
    size = sizeof(*ace) + sid_len( eff_sid );
    if (out)
    {
        set_ace( out, eff_sid, ace->type, inh & ~(OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | NO_PROPAGATE_INHERIT_ACE),
                 map_access( mask, map ) );
        out = ace_next( out );
    }
    /* and, if it still propagates, an inherit-only copy of the original */
    if (inh & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE))
    {
        if (out) set_ace( out, sid, ace->type, inh | INHERIT_ONLY_ACE, mask );
        size += sizeof(*ace) + sid_len( sid );
    }
    return size;
}

/* an explicit ACE of the creator's ACL; ACE types this code doesn't know are copied */
static data_size_t explicit_ace( const struct ace *ace, const struct sid *owner, const struct sid *group,
                                 const struct generic_map *map, struct ace *out )
{
    /* generic rights are mapped in place (Windows 10 oracle: FileSecurityInherit
     * child_file_generic_dacl, PrivateObjectSecurity creator_generic_mapped) */
    if (out)
    {
        memcpy( out, ace, ace->size );
        if (!(ace->flags & INHERIT_ONLY_ACE) &&
            (ace->type == ACCESS_ALLOWED_ACE_TYPE || ace->type == ACCESS_DENIED_ACE_TYPE ||
             ace->type == SYSTEM_AUDIT_ACE_TYPE))
            out->mask = map_access( ace->mask, map );
    }
    return ace->size;
}

/* the creator's explicit ACEs followed by the ACEs inherited from the parent's ACL */
static struct acl *build_acl( const struct acl *explicit, const struct acl *parent, int is_container,
                              int auto_inherit, const struct sid *owner, const struct sid *group,
                              const struct generic_map *map )
{
    const struct ace *ace;
    struct acl *acl;
    struct ace *out;
    data_size_t size = sizeof(*acl);
    unsigned int i, count = 0;

    if (explicit)
        for (i = 0, ace = ace_first( explicit ); i < explicit->count; i++, ace = ace_next( ace ))
            if (!(ace->flags & INHERITED_ACE)) size += explicit_ace( ace, owner, group, map, NULL );
    if (parent)
        for (i = 0, ace = ace_first( parent ); i < parent->count; i++, ace = ace_next( ace ))
            size += inherit_ace( ace, is_container, auto_inherit, owner, group, map, NULL );
    if (size > MAX_ACL_LEN) return NULL;
    if (!(acl = mem_alloc( size ))) return NULL;
    acl->revision = ACL_REVISION;
    acl->pad1 = 0;
    acl->size = size;
    acl->pad2 = 0;
    out = ace_first( acl );
    if (explicit)
        for (i = 0, ace = ace_first( explicit ); i < explicit->count; i++, ace = ace_next( ace ))
        {
            if (ace->flags & INHERITED_ACE) continue;
            out = (struct ace *)((char *)out + explicit_ace( ace, owner, group, map, out ));
        }
    if (parent)
        for (i = 0, ace = ace_first( parent ); i < parent->count; i++, ace = ace_next( ace ))
            out = (struct ace *)((char *)out + inherit_ace( ace, is_container, auto_inherit, owner, group, map, out ));
    for (ace = ace_first( acl ); (char *)ace < (char *)acl + size; ace = ace_next( ace )) count++;
    acl->count = count;
    return acl;
}

static int acl_inherits( const struct acl *acl, int is_container )
{
    const struct ace *ace;
    unsigned int i;

    if (!acl) return FALSE;
    for (i = 0, ace = ace_first( acl ); i < acl->count; i++, ace = ace_next( ace ))
        if (inherit_ace( ace, is_container, 0, &world_sid, &world_sid, &file_mapping, NULL )) return TRUE;
    return FALSE;
}

/* The security descriptor of a new object. Owner and group come from the
 * creator's descriptor or the token. Each ACL holds the creator's explicit
 * ACEs followed by the ACEs inherited from the parent, unless the creator's
 * ACL is protected; with neither, the DACL is the token's default DACL.
 * Generic rights are mapped with the given mapping. */
struct security_descriptor *sd_inherit_map( const struct security_descriptor *parent,
                                            const struct security_descriptor *creator,
                                            int is_container, struct token *token,
                                            const struct generic_map *map )
{
    const struct sid *owner = creator ? sd_get_owner( creator ) : NULL;
    const struct sid *group = creator ? sd_get_group( creator ) : NULL;
    const struct acl *pdacl = NULL, *psacl = NULL, *cdacl = NULL, *csacl = NULL;
    const struct acl *dacl_src, *sacl_src = NULL;
    struct acl *dacl = NULL, *sacl = NULL;
    int present, cdacl_present = 0, csacl_present = 0;
    unsigned int control = SE_DACL_PRESENT;
    struct security_descriptor *sd;
    char *ptr;

    if (!owner) owner = token_get_owner( token );
    if (!group) group = token_get_primary_group( token );
    if (parent)
    {
        pdacl = sd_get_dacl( parent, &present );
        if (!present) pdacl = NULL;
        psacl = sd_get_sacl( parent, &present );
        if (!present) psacl = NULL;
    }
    if (creator)
    {
        cdacl = sd_get_dacl( creator, &cdacl_present );
        csacl = sd_get_sacl( creator, &csacl_present );
        if (creator->control & SE_DACL_DEFAULTED) cdacl_present = 0;
        if (creator->control & SE_SACL_DEFAULTED) csacl_present = 0;
    }

    /* DACL */
    /* the creator's DACL replaces the inherited ACEs unless the parent's DACL
     * is auto-inherited (Windows 10 oracle: FileSecurityInherit) */
    if (cdacl_present && (!cdacl || (creator->control & SE_DACL_PROTECTED) || !acl_inherits( pdacl, is_container ) ||
                          !(parent->control & SE_DACL_AUTO_INHERITED)))
    {
        if (cdacl && !(dacl = build_acl( cdacl, NULL, is_container, 0, owner, group, map ))) return NULL;
        dacl_src = dacl;
        control |= creator->control & SE_DACL_PROTECTED;
    }
    else if (acl_inherits( pdacl, is_container ))
    {
        int ai = !!(parent->control & SE_DACL_AUTO_INHERITED);
        if (!(dacl = build_acl( cdacl_present ? cdacl : NULL, pdacl, is_container, ai, owner, group, map ))) return NULL;
        dacl_src = dacl;
        if (ai) control |= SE_DACL_AUTO_INHERITED;
    }
    else
    {
        /* the token's default DACL, generic rights mapped */
        const struct acl *def = token_get_default_dacl( token );
        if (def && !(dacl = build_acl( def, NULL, is_container, 0, owner, group, map ))) return NULL;
        dacl_src = dacl;
    }

    /* SACL */
    if (csacl_present && csacl && ((creator->control & SE_SACL_PROTECTED) || !acl_inherits( psacl, is_container ) ||
                                   !(parent->control & SE_SACL_AUTO_INHERITED)))
    {
        sacl_src = csacl;
        control |= SE_SACL_PRESENT | (creator->control & SE_SACL_PROTECTED);
    }
    else if (acl_inherits( psacl, is_container ))
    {
        if (!(sacl = build_acl( csacl_present ? csacl : NULL, psacl, is_container,
                                !!(parent->control & SE_SACL_AUTO_INHERITED), owner, group, map )))
        {
            free( dacl );
            return NULL;
        }
        sacl_src = sacl;
        control |= SE_SACL_PRESENT;
        if (parent->control & SE_SACL_AUTO_INHERITED) control |= SE_SACL_AUTO_INHERITED;
    }

    if ((sd = mem_alloc( sizeof(*sd) + sid_len( owner ) + sid_len( group ) +
                         (sacl_src ? sacl_src->size : 0) + (dacl_src ? dacl_src->size : 0) )))
    {
        sd->control   = control;
        sd->owner_len = sid_len( owner );
        sd->group_len = sid_len( group );
        sd->sacl_len  = sacl_src ? sacl_src->size : 0;
        sd->dacl_len  = dacl_src ? dacl_src->size : 0;
        ptr = (char *)(sd + 1);
        ptr = mem_append( ptr, owner, sd->owner_len );
        ptr = mem_append( ptr, group, sd->group_len );
        ptr = mem_append( ptr, sacl_src, sd->sacl_len );
        mem_append( ptr, dacl_src, sd->dacl_len );
    }
    free( dacl );
    free( sacl );
    return sd;
}

/* the security descriptor of a new file or directory */
struct security_descriptor *sd_inherit( const struct security_descriptor *parent,
                                        const struct security_descriptor *creator,
                                        int is_container, struct token *token )
{
    return sd_inherit_map( parent, creator, is_container, token, &file_mapping );
}

#define BACKUP_RIGHTS  (READ_CONTROL | ACCESS_SYSTEM_SECURITY | FILE_GENERIC_READ | FILE_TRAVERSE)
#define RESTORE_RIGHTS (WRITE_DAC | WRITE_OWNER | ACCESS_SYSTEM_SECURITY | FILE_GENERIC_WRITE | \
                        FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | DELETE)

/* rights a token gets on a file: from its DACL, from the parent directory, and
 * from the backup and restore privileges when opening for backup */
static unsigned int file_max_rights( struct token *token, const struct security_descriptor *sd,
                                     const struct security_descriptor *parent, unsigned int options )
{
    unsigned int rights = MAXIMUM_ALLOWED, prights = MAXIMUM_ALLOWED;

    sd_access_check( token, sd, &file_mapping, &rights );
    if (!parent) prights = FILE_DELETE_CHILD | FILE_LIST_DIRECTORY;
    else sd_access_check( token, parent, &file_mapping, &prights );
    if (prights & FILE_DELETE_CHILD) rights |= DELETE;
    if (prights & FILE_LIST_DIRECTORY) rights |= FILE_READ_ATTRIBUTES;
    if (options & FILE_OPEN_FOR_BACKUP_INTENT)
    {
        if (privilege_enabled( token, SeBackupPrivilege )) rights |= BACKUP_RIGHTS;
        if (privilege_enabled( token, SeRestorePrivilege )) rights |= RESTORE_RIGHTS;
    }
    if (privilege_enabled( token, SeSecurityPrivilege )) rights |= ACCESS_SYSTEM_SECURITY;
    return rights;
}

static int check_rights( unsigned int want, unsigned int rights, unsigned int *granted )
{
    unsigned int need = want & ~MAXIMUM_ALLOWED;

    if (need & ~rights)
    {
        set_error( (need & ~rights) == ACCESS_SYSTEM_SECURITY ? STATUS_PRIVILEGE_NOT_HELD : STATUS_ACCESS_DENIED );
        return FALSE;
    }
    *granted = (want & MAXIMUM_ALLOWED) ? ((rights & ~ACCESS_SYSTEM_SECURITY) | need) : need;
    return TRUE;
}

/* Check an open or create of a file or directory against stored security
 * descriptors, before anything on disk changes. For an existing file with a
 * stored descriptor, *granted receives the rights the handle gets. For a new
 * file whose parent has a stored descriptor, or that gets one from its creator,
 * *new_sd receives the descriptor to store. Returns FALSE with the error set if
 * access is denied. */
int file_sd_check_open( int root_fd, const char *name, int flags, unsigned int options,
                        const struct security_descriptor *creator_sd, unsigned int *access,
                        unsigned int *granted, struct security_descriptor **new_sd )
{
    struct token *token = current_token();
    struct security_descriptor *sd, *psd;
    char *parent;
    struct stat st;
    int ret = TRUE;

    *granted = 0;
    *new_sd = NULL;

    if (!fstatat( root_fd == -1 ? AT_FDCWD : root_fd, name, &st, 0 ))
    {
        if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) return TRUE;  /* collision */
        if (!(sd = path_get_stored_sd( root_fd, name ))) return TRUE;
        psd = NULL;
        if ((parent = get_parent_name( name )))
        {
            psd = path_get_stored_sd( root_fd, parent );
            free( parent );
        }
        if ((ret = check_rights( *access, file_max_rights( token, sd, psd, options ), granted )))
            *access = *granted;
        free( sd );
        free( psd );
        return ret;
    }

    if (!(flags & O_CREAT) || errno != ENOENT) return TRUE;
    if (!(parent = get_parent_name( name ))) return TRUE;
    psd = path_get_stored_sd( root_fd, parent );
    free( parent );
    if (psd)
    {
        unsigned int prights = MAXIMUM_ALLOWED;
        unsigned int need = (options & FILE_DIRECTORY_FILE) ? FILE_ADD_SUBDIRECTORY : FILE_ADD_FILE;

        sd_access_check( token, psd, &file_mapping, &prights );
        if ((options & FILE_OPEN_FOR_BACKUP_INTENT) && privilege_enabled( token, SeRestorePrivilege ))
            prights |= RESTORE_RIGHTS;
        if (!(prights & need))
        {
            free( psd );
            set_error( STATUS_ACCESS_DENIED );
            return FALSE;
        }
    }
    if (psd || creator_sd)
    {
        if ((*new_sd = sd_inherit( psd, creator_sd, !!(options & FILE_DIRECTORY_FILE), token )))
        {
            /* the creator gets the rights it asked for, whatever the new descriptor says */
            *granted = *access & ~MAXIMUM_ALLOWED;
            if (*access & MAXIMUM_ALLOWED)
                *granted |= file_max_rights( token, *new_sd, psd, options ) & ~ACCESS_SYSTEM_SECURITY;
            if (*granted) *access = *granted;
        }
    }
    free( psd );
    return TRUE;
}

/* store the descriptor computed for a new file; failure leaves the file with its mode */
void file_sd_store_new( int unix_fd, const struct security_descriptor *sd )
{
    if (unix_fd != -1) fd_store_sd( unix_fd, sd );
}
