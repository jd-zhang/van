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

#include "van_log.h"

int _van_logrec_cget(VAN_LOG_CURSOR *lgc, VAN_LSN *lsn,
    VAN_DATUM *datum, flags_t flags) {
	int ret;
	VAN_LOG *logp;
	VAN_STORE *store;
	VAN_LOGREC_HEADER rechdr;
	VAN_LSN glsn, clsn, flsn;

	ret = 0;
	logp = lgc->logp;
	memset(&rechdr, 0, sizeof(rechdr));
	memset(&glsn, 0, sizeof(glsn));
	memset(&flsn, 0, sizeof(flsn));
	clsn = logp->cur_lsn;
	flsn.file = logp->firstfnum;
	flsn.offset = VAN_LOGFILE_SIZE(logp);

	VAN_ASSERT(lsn != NULL);

	/* No log records currently */
	if (clsn.file == logp->firstfnum && VAN_LOGREC_ISFIRST(clsn)) {
		ret = VAN_NOTFOUND;
		goto err;
	}

	switch (flags) {
		case VAN_NEXT:
			if (lgc->logfile != NULL) {

			}
			/* fallthrough */
		case VAN_FIRST:
			ret = _van_logc_openfile(lgc, &flsn);
			break;
		case VAN_PREV:
			if (lgc->logfile != NULL) {

			}
			/* fallthrough */
		case VAN_LAST:
			ret = _van_logc_openfile(lgc, &logp->last_lsn);
			break;
		case VAN_SET:
			if (LOG_BIGGER(lsn, &clsn) || LOG_SMALLER(lsn, &flsn)) {
				ret = VAN_NOTFOUND;
				break;
			}
			ret = _van_logc_openfile(lgc, lsn);
			break;
		case VAN_CURRENT:
			break;
		default:
			break;
	}

err:
	return ret;
}
