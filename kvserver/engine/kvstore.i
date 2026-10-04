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
%module kvstore

%{
#include "kvstore.h"
%}

/*
 * Binary input.
 *
 * A (const char *, long long) pair is fed by a single Python bytes
 * object: the pointer and the length are taken straight from it, so
 * keys and values may contain NUL bytes.  The default SWIG handling of
 * "char *" stops at the first NUL and so cannot carry binary.
 *
 * The names below are not decoration: SWIG matches a multi argument
 * typemap on the parameter names as well as the types, so they have to
 * read exactly like the ones in kvstore.h (and keep their 'const').
 * With them in place kv_put(key, value) and kv_del(key) take no length
 * argument from Python.
 */
%typemap(in) (const char *key, long long key_len) {
	if (!PyBytes_Check($input)) {
		SWIG_exception_fail(SWIG_TypeError,
		    "key must be a bytes object");
	}
	$1 = PyBytes_AS_STRING($input);
	$2 = (long long)PyBytes_GET_SIZE($input);
}

%typemap(in) (const char *value, long long value_len) {
	if (!PyBytes_Check($input)) {
		SWIG_exception_fail(SWIG_TypeError,
		    "value must be a bytes object");
	}
	$1 = PyBytes_AS_STRING($input);
	$2 = (long long)PyBytes_GET_SIZE($input);
}

/*
 * Binary output.
 *
 * kv_get() comes back as (return code, value) with the value a Python
 * bytes object, again so that binary survives.
 *
 * The generated wrapper is the caller of kv_get(), so it is also the one
 * that releases the malloc'd buffer once the copy exists: Python has no
 * way to hold a raw C pointer, and freeing it here keeps the Python side
 * free of manual memory management.  kv_free_value() stays exported for
 * callers that use the plain C interface.
 */
%typemap(in, numinputs=0) (char **value, long long *value_len)
    (char *tmpval = NULL, long long tmplen = 0) {
	$1 = &tmpval;
	$2 = &tmplen;
}

%typemap(argout) (char **value, long long *value_len) {
	PyObject *out;

	if (*$1 != NULL) {
		out = PyBytes_FromStringAndSize(*$1, (Py_ssize_t)(*$2));
		kv_free_value(*$1);
		*$1 = NULL;
	} else {
		Py_INCREF(Py_None);
		out = Py_None;
	}
	$result = SWIG_Python_AppendOutput($result, out);
}

%include "kvstore.h"
