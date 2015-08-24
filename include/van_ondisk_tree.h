#ifndef _VAN_ONDISK_TREE_H
#define _VAN_ONDISK_TREE_H

#define NT_OD_INVALID 0
#define NT_OD_BINTERNAL 1
#define NT_OD_BLEAF 2
#define NT_OD_BMETA 3

#define OD_META_VERSION		1
#define OD_MAX_LEVEL		255
#define	OD_BASE_UNIT		512
#define OD_DEF_BLKSIZE		1048576
#define OD_BLOB_THRESHOLD	2048
#define OD_DEF_CACHESIZE	(64 * MEGABYTE)
#define OD_FILEID_LEN		20
struct _ondisk_meta {
	uint32	nlevel;		/* 0-3 */
	uint32	metasize;	/* 4-7 */
	uint32	blksize;	/* 8-11 */
	uint64	nrecs;		/* 12-19 */
	uint32	root_blkid;	/* 20-23 */
	uint32	first_blkid;	/* 24-27 */
	uint32	last_blkid;	/* 28-31 */
	uint32	blob_threshold;	/* 32-35 */
	char	fileid[OD_FILEID_LEN];	/* 36-55 */
	char	blob_dir[128];
	char	file_path[128];
	char	bloom_path[128];
};

/* This struct will not be changed during upgrades */
struct _ondisk_meta_summary {
	uint32	metaversion;
	uint32	metasize;
	uint64	metaoff;	
};

/* 
 * Node structure for internal/leaf.
 * 
 * The key is always having the type ONDISK_ITEM
 *
 * For internal, the layout is:
 * |header|offset0|offset1|...|offsetn|
 * |empty space|keyn|blkn|..|key0|blk0|
 * So the value part is just the block id
 * of child.
 *
 * For leaf, the layout is:
 * |header|offkey0|offval0|...|offk/vn|
 * |empty space|keyn|valn|..|key0|val0|
 * The value can have different types,
 * having the type of ONDISK_ITEM.
 */

#define P_OVERHEAD (SSZA(ONDISK_HEADER, indexes))
#define P_INDEXES(page)	((char *)page + P_OVERHEAD)
#define P_INDX(page, idx) ((ONDISK_HEADER *)page)->indexes[idx]
#define P_ADDR(page, idx) ((char *)page + P_INDX(page, idx))
#define P_PTR(page, i) ((char *)page + i)
#define P_KEY(page, idx) ((ONDISK_ITEM *)P_ADDR(page, idx))
#define P_DATA(page, idx) P_KEY(page, idx)
#define P_BLKID(page, idx) *((uint32 *)P_ADDR(page, idx))
/* 
 * For internal's key and leaf's key/data, we need a item header,
 * and a indx extra to store the keys. We may consider adding
 * compression to suppress the bytes for the index or header,
 * but this is disk storage, so low priority.
 */
#define P_ITEMOFF (SSZA(ONDISK_ITEM, data))
#define P_ITEMSIZE(size) (P_ITEMOFF + size)
#define P_BLKSIZE sizeof(uint32)

#define INVALID_BLKID	(uint32)-1
#define IS_VALID_BLKID(id)	(id != INVALID_BLKID)
struct _ondisk_header {
	uint32	level;
	uint32	type;
	uint32	size;
	uint32	capacity;
	uint32	blkid;
	uint32	next_blkid;
	uint32	prev_blkid;
	uint32	offset;
	uint32	left;
	uint32	indexes[1];
};

#define OD_VARCHAR	1
#define OD_FILE		2
struct _ondisk_item {
	uint32	flags;
	uint32	len;
	uint8	type;
	char	data[1];
};

struct _ondisk_tree {
	VAN_TABLE	*table;
	ONDISK_META	*meta;
	ONDISK_META_SUMMARY	*meta_summary;
	int		tree_fid;
	int		bloom_fid;
	uint32		metasize;
	uint32		blksize;
	size_t		blob_threshold;
	size_t		blk_cachesize;

	mode_t		omode;
	char		*blob_dir;
	char		*file_path;
	char		*bloom_path;

	ONDISK_BUILDINFO *buildinfo;

	ticks_t		created_ticks;
	ticks_t		merged_ticks;
	/* The index on table's ondisk_tree list. */
	size_t		table_indx; 
#define	ONDISK_TREE_MERGED	0x01
#define ONDISK_TREE_FREED	0x02
#define ONDISK_TREE_DUMPED	0x04
	flags_t		flags;
	int		refcount;
	/* Protect flags, refcount, and table_index. */
	pthread_mutex_t	tree_mutex;


	/* For key/data smaller equal to or small than the default. */
	VAN_DATUM	*defretkey;
	VAN_DATUM	*defretdata;

	/* For key/data bigger than the default. */
	VAN_DATUM *reakey;
	VAN_DATUM *readata;

	/* function handles */
	int (*get)(ONDISK_TREE *,
	    VAN_DATUM *, VAN_DATUM *, flags_t);

};

typedef ONDISK_HEADER *odheadptr_arr2[2];
struct _ondisk_buildinfo {
	size_t nrecs;
	size_t toplevel;
	uint32 next_blkid;
	uint32 first_blkid;
	uint32 last_blkid;
	uint32 root_blkid;
	odheadptr_arr2 *nodes;
	ONDISK_ITEM **reserved_keys;
	ONDISK_HEADER *root;
};

struct _ondisk_treecursor {
	VAN_TABLE	*table;
	ONDISK_TREE	*tree;
	ONDISK_HEADER	*page;
	uint32		indx;
	uint32		flags;

	/* For key/data smaller equal to or small than the default. */
	VAN_DATUM *defretkey; 
	VAN_DATUM *defretdata;

	/* For key/data bigger than the default. */
	VAN_DATUM *reakey;
	VAN_DATUM *readata;

	/* function handles */
	int (*get)(ONDISK_TREECURSOR *,
	    VAN_DATUM *, VAN_DATUM *, flags_t);

};

#endif
