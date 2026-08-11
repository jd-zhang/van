/*
 * Copyright (c) 2026 VanDB Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "van_int.h"
#include "van.h"
/*
 * _inmemtree_put_recover --
 *	Recovery function for put.
 *
 * PUBLIC: int _inmemtree_put_recover
 * PUBLIC:   (VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *);
 */
int
_inmemtree_put_recover(store, datp, lsnp, op, info)
	VAN_STORE *store;
	VAN_DATUM *datp;
	VAN_LSN *lsnp;
	van_recops op;
	void *info;
{
	_inmemtree_put_args *argp;
	VAN_TABLE *tablep;
	int ret;

	ret = 0;

out:	
	return ret;
}

/*
 * _inmemtree_del_recover --
 *	Recovery function for del.
 *
 * PUBLIC: int _inmemtree_del_recover
 * PUBLIC:   (VAN_STORE *, VAN_DATUM *, VAN_LSN *, van_recops, void *);
 */
int
_inmemtree_del_recover(store, datp, lsnp, op, info)
	VAN_STORE *store;
	VAN_DATUM *datp;
	VAN_LSN *lsnp;
	van_recops op;
	void *info;
{
	_inmemtree_del_args *argp;
	VAN_TABLE *tablep;
	int ret;

	ret = 0;

out:	
	return ret;
}

