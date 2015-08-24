#include "van_int.h"
#include "van.h"

#include "van_ondisk_tree.h"

typedef unsigned long ul;
typedef unsigned int ui;
typedef unsigned short us;

int _van_ondisktree_dump(ONDISK_TREE *tree, FILE *f) {
	ONDISK_HEADER *page;
	ONDISK_META *meta;
	int ret;
	uint32 i;
	struct stat st;
	size_t sz;

	meta = tree->meta;
	ret = 0;

	ret = _van_ondisktree_dump_meta(meta, f);
	VAN_ASSERT(ret == 0);

	ret = _van_file_stat(tree->tree_fid, &st);
	VAN_ASSERT(ret == 0);
	
	for (i = 0; i <= meta->root_blkid; i++) {
		ret = _van_ondisk_cache_get(tree, i, &page);
		VAN_ASSERT(ret == 0);
		ret = _van_ondisktree_dump_header(page, f);
		VAN_ASSERT(ret == 0);
		ret = _van_ondisk_cache_put(tree, page);
		VAN_ASSERT(ret == 0);
	}

	return ret;
}

int _van_ondisktree_dump_meta(ONDISK_META *meta, FILE *f) {
	int ret;

	ret = 0;
	fprintf(f, "========= Meta page information ==========\n");
	fprintf(f, " nlevel:%lu\n metasize:%lu\n blksize:%lu\n"
	    " nrecs:%lu\n root_blkid:%lu\n first_blkid:%lu\n"
	    " last_blkid:%lu\n blob_threshold:%lu\n", (ul)meta->nlevel,
	    (ul)meta->metasize, (ul)meta->blksize, (ul)meta->nrecs,
	    (ul)meta->root_blkid, (ul)meta->first_blkid,
	    (ul)meta->last_blkid, (ul)meta->blob_threshold);

	return (ret);
}

int _van_ondisktree_dump_header(ONDISK_HEADER *hdr, FILE *f) {
	const char *type;
	int ret, pginval;
	
	ret = 0;
	type = _van_ondisktree_blk_typestr((int)hdr->type);
	pginval = 0;
	if (hdr->type != NT_OD_BINTERNAL &&
	    hdr->type != NT_OD_BLEAF)
		pginval = 1;
	fprintf(f, " ===== page %lu (%lx) information ===\n",
	    (ul)hdr->blkid, (ul)hdr);
	fprintf(f, " type:%s\n size:%lu\n level:%lu\n capacity:%lu\n"
	    " offset:%lu\n left:%lu\n", type, (ul)hdr->size,
	    (ul)hdr->level, (ul)hdr->capacity, (ul)hdr->offset,
	    (ul)hdr->left);
	if (hdr->type == NT_OD_BLEAF)
		fprintf(f, " next_blkid:%lu\n prev_blkid:%lu\n",
		    (ul)hdr->next_blkid, (ul)hdr->prev_blkid);

	return (ret);
}

const char *_van_ondisktree_blk_typestr(int type) {
	const char *str;

	if (type == NT_OD_BINTERNAL)
		str = "internal";
	else if (type == NT_OD_BLEAF)
		str = "leaf";
	else
		str = "(unknown)";

	return str;
}
