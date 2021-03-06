/****************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * fs/procfs/fs_procfsereport.c
 *
 *   Copyright (C) 2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/
/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <tinyara/config.h>

#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/stat.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <tinyara/arch.h>
#include <tinyara/sched.h>
#include <tinyara/kmalloc.h>
#include <tinyara/fs/fs.h>
#include <tinyara/fs/procfs.h>
#include <tinyara/fs/dirent.h>
#include <tinyara/clock.h>



#if !defined(CONFIG_DISABLE_MOUNTPOINT) && defined(CONFIG_FS_PROCFS)
#if !defined(FS_PROCFS_EXCLUDE_EREPORT)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
/* Determines the size of an intermediate buffer that must be large enough
 * to handle the longest line generated by this logic.
 */
#define EREPORT_LINELEN         40
#define EREPORT_DIRNAME         "ereport"


/****************************************************************************
 * Private Types
 ****************************************************************************/
/* This enumeration identifies all of the task/thread nodes that can be
 * accessed via the procfs file system.
 */

enum ereport_node_e {
	EREPORT_LEVEL0 = 0,				/* The top-level directory */
	EREPORT_TASKADDR,				/* Task address */
};

struct ereport_node_s {
	FAR const char *relpath;	/* Relative path to the node */
	FAR const char *name;		/* Terminal node segment name */
	uint8_t nodetype;			/* Type of node (see enum ereport_node_e) */
	uint8_t dtype;				/* dirent type (see include/dirent.h) */
};

struct ereport_dir_s {
	struct procfs_dir_priv_s base;	/* Base directory private data */
	FAR const struct ereport_node_s *node;	/* Directory node description */
};
/* This structure describes one open "file" */
struct ereport_file_s {
	struct procfs_file_s base;	/* Base open file structure */
	FAR const struct ereport_node_s *node;	/* Describes the file node */
	struct ereport_dir_s dir;		/* Reference to item being accessed */
	char line[EREPORT_LINELEN];		/* Pre-allocated buffer for formatted lines */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* File system methods */
static int ereport_open(FAR struct file *filep, FAR const char *relpath, int oflags, mode_t mode);
static int ereport_close(FAR struct file *filep);
static ssize_t ereport_read(FAR struct file *filep, FAR char *buffer, size_t buflen);
static int ereport_opendir(const char *relpath, FAR struct fs_dirent_s *dir);
static int ereport_closedir(FAR struct fs_dirent_s *dir);
static int ereport_readdir(FAR struct fs_dirent_s *dir);
static int ereport_rewinddir(FAR struct fs_dirent_s *dir);
static int ereport_stat(const char *relpath, FAR struct stat *buf);
/****************************************************************************
 * Private Variables
 ****************************************************************************/

/****************************************************************************
 * Public Variables
 ****************************************************************************/

/* See fs_mount.c -- this structure is explicitly externed there.
 * We use the old-fashioned kind of initializers so that this will compile
 * with any compiler.
 */

const struct procfs_operations ereport_operations = {
	ereport_open,					/* open */
	ereport_close,					/* close */
	ereport_read,					/* read */
	NULL,						/* write */

	NULL,						/* dup */

	ereport_opendir,					/* opendir */
	ereport_closedir,				/* closedir */
	ereport_readdir,					/* readdir */
	ereport_rewinddir,				/* rewinddir */

	ereport_stat						/* stat */
};

/* These structures provide information about every node */
static const struct ereport_node_s g_ereport_level0 = {
	"", "ereport", (uint8_t)EREPORT_LEVEL0, DTYPE_DIRECTORY	/* Top-level directory */
};

static const struct ereport_node_s g_ereport_taskaddr = {
	"taskaddr", "taskaddr", (uint8_t)EREPORT_TASKADDR, DTYPE_FILE	/* Task command line */
};

/* This is the list of all nodes */

static FAR const struct ereport_node_s *const g_ereport_nodeinfo[] = {
	&g_ereport_level0,
	&g_ereport_taskaddr,				/* Task address */
};

#define EREPORT_NNODES (sizeof(g_ereport_nodeinfo)/sizeof(FAR const struct ereport_node_s * const))

static const struct ereport_node_s *const g_ereport_level0info[] = {
	&g_ereport_taskaddr,				/* Task command line */
};

#define EREPORT_NLEVEL0NODES (sizeof(g_ereport_level0info)/sizeof(FAR const struct ereport_node_s * const))


/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ereport_taskaddr_read
 ****************************************************************************/
static size_t ereport_taskaddr_read(FAR struct ereport_file_s *efile, FAR char *buffer, size_t buflen, off_t offset)
{
	size_t linesize;
	size_t copysize;
	unsigned long taskaddr = 0;
	pid_t pid = getpid();
	struct tcb_s *tcbptr = sched_gettcb(pid);
	if (tcbptr != NULL) {
		entry_t e = tcbptr->entry;
		if ((tcbptr->flags & TCB_FLAG_TTYPE_MASK) == TCB_FLAG_TTYPE_PTHREAD) {
			taskaddr = (unsigned long)e.pthread;
		} else {
			taskaddr = (unsigned long)e.main;
		}
	}
	//memset(efile->line, 0, EREPORT_LINELEN);
	linesize = snprintf(efile->line, EREPORT_LINELEN, "Error_Report pid:%d, taskaddr:%lu", pid, taskaddr);
	efile->line[linesize++] = '\0';
	copysize = procfs_memcpy(efile->line, linesize, buffer, buflen, &offset);
	return copysize;
}

/****************************************************************************
 * Name: ereport_findnode
 ****************************************************************************/
static FAR const struct ereport_node_s *ereport_findnode(FAR const char *relpath)
{
	int i;

	/* Two path forms are accepted:
	 *
	 * "ereport" - It is a top directory.
	 * "ereport/<node>" - If <node> is a recognized node then, then it
	 *   is a file or directory.
	 */
	if (strncmp(relpath, EREPORT_DIRNAME, strlen(EREPORT_DIRNAME)) != 0) {
		fdbg("ERROR: Bad relpath: %s\n", relpath);
		return NULL;
	}
	relpath += strlen(EREPORT_DIRNAME);

	if (relpath[0] == '/') {
		relpath++;
	}

	/* Search every string in g_ereport_nodeinfo or until a match is found */

	for (i = 0; i < EREPORT_NNODES; i++) {
		if (strcmp(g_ereport_nodeinfo[i]->relpath, relpath) == 0) {
			return g_ereport_nodeinfo[i];
		}
	}

	/* Not found */

	return NULL;
}

/****************************************************************************
 * Name: ereport_open
 ****************************************************************************/

static int ereport_open(FAR struct file *filep, FAR const char *relpath, int oflags, mode_t mode)
{
	FAR struct ereport_file_s *efile;
	FAR const struct ereport_node_s *node;

	fvdbg("Open '%s'\n", relpath);

	/* PROCFS is read-only.  Any attempt to open with any kind of write
	 * access is not permitted.
	 *
	 * REVISIT:  Write-able proc files could be quite useful.
	 */

	if ((oflags & O_WRONLY) != 0 || (oflags & O_RDONLY) == 0) {
		fdbg("ERROR: Only O_RDONLY supported\n");
		return -EACCES;
	}

	/* Find the nodes of connectivity directory */
	node = ereport_findnode(relpath);
	if (!node) {
		/* Entry not found */
		fdbg("ERROR: Invalid path \"%s\"\n", relpath);
		return -ENOENT;
	}

	if (!DIRENT_ISFILE(node->dtype)) {
		fdbg("ERROR: Path \"%s\" is not a regular file\n", relpath);
		return -EISDIR;
	}

	/* Allocate a container to hold the domain selection */
	efile = (FAR struct ereport_file_s *)kmm_zalloc(sizeof(struct ereport_file_s));
	if (!efile) {
		fdbg("ERROR: Failed to allocate file container\n");
		return -ENOMEM;
	}

	efile->node = node;

	/* Save the index as the open-specific state in filep->f_priv */
	filep->f_priv = (FAR void *)efile;

	return OK;
}

/****************************************************************************
 * Name: ereport_close
 ****************************************************************************/

static int ereport_close(FAR struct file *filep)
{
	FAR struct ereport_file_s *efile;

	/* Recover our private data from the struct file instance */

	efile = (FAR struct ereport_file_s *)filep->f_priv;
	DEBUGASSERT(efile);

	/* Release the file container structure */

	kmm_free(efile);
	filep->f_priv = NULL;
	return OK;
}

/****************************************************************************
 * Name: ereport_read
 ****************************************************************************/

static ssize_t ereport_read(FAR struct file *filep, FAR char *buffer, size_t buflen)
{
	FAR struct ereport_file_s *efile;
	ssize_t ret;

	fvdbg("buffer=%p buflen=%d\n", buffer, (int)buflen);

	/* Recover our private data from the struct file instance */

	efile = (FAR struct ereport_file_s *)filep->f_priv;
	DEBUGASSERT(efile);

	/* Provide the requested data */
	ret = 0;

	switch (efile->node->nodetype) {
	case EREPORT_TASKADDR:			/* Task Address */
		ret = ereport_taskaddr_read(efile, buffer, buflen, filep->f_pos);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	/* Update the file offset */

	if (ret > 0) {
		filep->f_pos += ret;
	}

	return ret;
}

/****************************************************************************
 * Name: ereport_opendir
 *
 * Description:
 *   Open a directory for read access
 *
 ****************************************************************************/
static int ereport_opendir(FAR const char *relpath, FAR struct fs_dirent_s *dir)
{
	FAR struct ereport_dir_s *ereport_dir;
	FAR const struct ereport_node_s *node;

	fvdbg("relpath: \"%s\"\n", relpath ? relpath : "NULL");
	DEBUGASSERT(relpath && dir && !dir->u.procfs);

	/* Find the directory entry being opened */
	node = ereport_findnode(relpath);
	if (!node) {
		/* Entry not found */
		fdbg("ERROR: Invalid path \"%s\"\n", relpath);
		return -ENOENT;
	}

	if (!DIRENT_ISDIRECTORY(node->dtype)) {
		fdbg("ERROR: Path \"%s\" is not a regular directory\n", relpath);
		return -ENOTDIR;
	}

	ereport_dir = (FAR struct ereport_dir_s *)kmm_zalloc(sizeof(struct ereport_dir_s));
	if (!ereport_dir) {
		fdbg("ERROR: Failed to allocate the directory structure\n");
		return -ENOMEM;
	}

	if (node->nodetype == EREPORT_LEVEL0) {
		/* This is a top level directory : connectivity */
		ereport_dir->base.level = 1;
		ereport_dir->base.nentries = EREPORT_NLEVEL0NODES;
		ereport_dir->base.index = 0;
		ereport_dir->node = node;
	}

	dir->u.procfs = (FAR void *)ereport_dir;
	return OK;
}

/****************************************************************************
 * Name: ereport_closedir
 *
 * Description: Close the directory listing
 *
 ****************************************************************************/

static int ereport_closedir(FAR struct fs_dirent_s *dir)
{
	FAR struct ereport_dir_s *priv;

	DEBUGASSERT(dir && dir->u.procfs);
	priv = dir->u.procfs;

	if (priv) {
		kmm_free(priv);
	}

	dir->u.procfs = NULL;
	return OK;
}

/****************************************************************************
 * Name: ereport_readdir
 *
 * Description: Read the next directory entry
 *
 ****************************************************************************/

static int ereport_readdir(struct fs_dirent_s *dir)
{
	FAR struct ereport_dir_s *ereport_dir;
	FAR const struct ereport_node_s *node = NULL;
	unsigned int index;
	int ret;

	DEBUGASSERT(dir && dir->u.procfs);
	ereport_dir = dir->u.procfs;

	/* Have we reached the end of the directory */

	index = ereport_dir->base.index;
	if (index >= ereport_dir->base.nentries) {
		/* We signal the end of the directory by returning the special
		 * error -ENOENT
		 */
		fdbg("Entry %d: End of directory\n", index);
		ret = -ENOENT;
	} else {
		/* No, we are not at the end of the directory.
		 * Handle the directory listing by the node type.
		 */

		switch (ereport_dir->node->nodetype) {
		case EREPORT_LEVEL0:		/* Top level directory */
			DEBUGASSERT(ereport_dir->base.level == 1);
			node = g_ereport_level0info[index];
			break;
		default:
			return -ENOENT;
		}

		/* Save the filename and file type */

		dir->fd_dir.d_type = node->dtype;
		strncpy(dir->fd_dir.d_name, node->name, NAME_MAX + 1);

		/* Set up the next directory entry offset.      NOTE that we could use the
		 * standard f_pos instead of our own private index.
		 */

		ereport_dir->base.index = index + 1;
		ret = OK;
	}
	return ret;
}

/****************************************************************************
 * Name: ereport_rewindir
 *
 * Description: Reset directory read to the first entry
 *
 ****************************************************************************/

static int ereport_rewinddir(struct fs_dirent_s *dir)
{
	FAR struct ereport_dir_s *priv;

	DEBUGASSERT(dir && dir->u.procfs);
	priv = dir->u.procfs;

	priv->base.index = 0;
	return OK;
}

/****************************************************************************
 * Name: ereport_stat
 *
 * Description: Return information about a file or directory
 *
 ****************************************************************************/

static int ereport_stat(const char *relpath, struct stat *buf)
{
	FAR const struct ereport_node_s *node;

	/* Find the directory entry being opened */
	node = ereport_findnode(relpath);

	if (!node) {
		fdbg("ERROR: Invalid path \"%s\"\n", relpath);
		return -ENOENT;
	}
	/* If the node exists, it is the name for a read-only file or
	 * directory.
	 */
	if (node->dtype == DTYPE_FILE) {
		buf->st_mode = S_IFREG | S_IROTH | S_IRGRP | S_IRUSR;
	} else {
		buf->st_mode = S_IFDIR | S_IROTH | S_IRGRP | S_IRUSR;
	}

	/* File/directory size, access block size */
	buf->st_size = 0;
	buf->st_blksize = 0;
	buf->st_blocks = 0;
	return OK;
}
#endif
#endif
