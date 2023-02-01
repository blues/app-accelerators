/*!
 * @file n_cjson.h
 *
 * Written by Ray Ozzie and Blues Inc. team.
 *
 * Portions Copyright (c) 2019 Blues Inc. MIT License. Use of this source code is
 * governed by licenses granted by the copyright holder including that found in
 * the
 * <a href="https://github.com/blues/note-c/blob/master/LICENSE">LICENSE</a>
 * file.
 *
 * MODIFIED for use in notecard primarily by altering default memory allocator
 * and by renaming the functions so that they won't conflict with a developer's
 * own decision to incorporate the actual production cJSON into their own app.
 * In no way shall this interfere with a production cJSON.
 *
 * Renaming was done as follows:
 * CJSON_ -> N_CJSON_
 * cJSON_ -> J
 * cJSON -> J
 *
 * Portions Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software i
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef J_h
#define J_h

#ifdef __cplusplus
extern "C"
{
#endif

/* project version */
#define N_CJSON_VERSION_MAJOR 1
#define N_CJSON_VERSION_MINOR 7
#define N_CJSON_VERSION_PATCH 7

#include <stddef.h>

/* J Types: */
#define JInvalid (0)
#define JFalse  (1 << 0)
#define JTrue   (1 << 1)
#define JNULL   (1 << 2)
#define JNumber (1 << 3)
#define JString (1 << 4)
#define JArray  (1 << 5)
#define JObject (1 << 6)
#define JRaw    (1 << 7) /* raw json */

#define JIsReference 256
#define JStringIsConst 512

/* The J structure: */
typedef struct J {
    /* next/prev allow you to walk array/object chains. Alternatively, use GetArraySize/GetArrayItem/GetObjectItem */
    struct J *next;
    struct J *prev;
    /* An array or object item will have a child pointer pointing to a chain of the items in the array/object. */
    struct J *child;

    /* The type of the item, as above. */
    int type;

    /* The item's string, if type==JString  and type == JRaw */
    char *valuestring;
    /* writing to valueint is DEPRECATED, use JSetNumberValue instead */
    long int valueint;
    /* The item's number, if type==JNumber */
    JNUMBER valuenumber;
    /* The item's name string, if this item is the child of, or is in the list of subitems of an object. */
    char *string;
} J;

typedef struct JHooks {
    void *(*malloc_fn)(size_t sz);
    void (*free_fn)(void *ptr);
} JHooks;

typedef int Jbool;

#if !defined(__WINDOWS__) && (defined(WIN32) || defined(WIN64) || defined(_MSC_VER) || defined(_WIN32))
#define __WINDOWS__
#endif
#ifdef __WINDOWS__

/* When compiling for windows, we specify a specific calling convention to avoid issues where we are being called from a project with a different default calling convention.  For windows you have 2 define options:

N_CJSON_HIDE_SYMBOLS - Define this in the case where you don't want to ever dllexport symbols
N_CJSON_EXPORT_SYMBOLS - Define this on library build when you want to dllexport symbols (default)
N_CJSON_IMPORT_SYMBOLS - Define this if you want to dllimport symbol

For *nix builds that support visibility attribute, you can define similar behavior by

setting default visibility to hidden by adding
-fvisibility=hidden (for gcc)
or
-xldscope=hidden (for sun cc)
to CFLAGS

then using the N_CJSON_API_VISIBILITY flag to "export" the same symbols the way N_CJSON_EXPORT_SYMBOLS does

*/

/* export symbols by default, this is necessary for copy pasting the C and header file */
#if !defined(N_CJSON_HIDE_SYMBOLS) && !defined(N_CJSON_IMPORT_SYMBOLS) && !defined(N_CJSON_EXPORT_SYMBOLS)
#define N_CJSON_EXPORT_SYMBOLS
#endif

#if defined(N_CJSON_HIDE_SYMBOLS)
#define N_CJSON_PUBLIC(type)   type __stdcall
#elif defined(N_CJSON_EXPORT_SYMBOLS)
#define N_CJSON_PUBLIC(type)   __declspec(dllexport) type __stdcall
#elif defined(N_CJSON_IMPORT_SYMBOLS)
#define N_CJSON_PUBLIC(type)   __declspec(dllimport) type __stdcall
#endif
#else /* !WIN32 */
#if (defined(__GNUC__) || defined(__SUNPRO_CC) || defined (__SUNPRO_C)) && defined(N_CJSON_API_VISIBILITY)
#define N_CJSON_PUBLIC(type)   __attribute__((visibility("default"))) type
#else
#define N_CJSON_PUBLIC(type) type
#endif
#endif

/* Limits how deeply nested arrays/objects can be before J rejects to parse them.
 * This is to prevent stack overflows. */
#ifndef N_CJSON_NESTING_LIMIT
#define N_CJSON_NESTING_LIMIT 1000
#endif

/* returns the version of J as a string */
N_CJSON_PUBLIC(const char*) JVersion(void);

/* Supply malloc, realloc and free functions to J */
N_CJSON_PUBLIC(void) JInitHooks(JHooks* hooks);

/* Memory Management: the caller is always responsible to free the results from all variants of JParse (with JDelete) and JPrint (with stdlib free, JHooks.free_fn, or JFree as appropriate). The exception is JPrintPreallocated, where the caller has full responsibility of the buffer. */
/* Supply a block of JSON, and this returns a J object you can interrogate. */
N_CJSON_PUBLIC(J *) JParse(const char *value);
/* ParseWithOpts allows you to require (and check) that the JSON is null terminated, and to retrieve the pointer to the final byte parsed. */
/* If you supply a ptr in return_parse_end and parsing fails, then return_parse_end will contain a pointer to the error so will match JGetErrorPtr(). */
N_CJSON_PUBLIC(J *) JParseWithOpts(const char *value, const char **return_parse_end, Jbool require_null_terminated);

/* Render a J entity to text for transfer/storage. */
N_CJSON_PUBLIC(char *) JPrint(const J *item);
/* Render a J entity to text for transfer/storage without any formatting. */
N_CJSON_PUBLIC(char *) JPrintUnformatted(const J *item);
/* Render a J entity to text using a buffered strategy. prebuffer is a guess at the final size. guessing well reduces reallocation. fmt=0 gives unformatted, =1 gives formatted */
N_CJSON_PUBLIC(char *) JPrintBuffered(const J *item, int prebuffer, Jbool fmt);
/* Render a J entity to text using a buffer already allocated in memory with given length. Returns 1 on success and 0 on failure. */
/* NOTE: J is not always 100% accurate in estimating how much memory it will use, so to be safe allocate 5 bytes more than you actually need */
N_CJSON_PUBLIC(Jbool) JPrintPreallocated(J *item, char *buffer, const int length, const Jbool format);
/* Delete a J entity and all subentities. */
N_CJSON_PUBLIC(void) JDelete(J *c);

/* Returns the number of items in an array (or object). */
N_CJSON_PUBLIC(int) JGetArraySize(const J *array);
/* Retrieve item number "index" from array "array". Returns NULL if unsuccessful. */
N_CJSON_PUBLIC(J *) JGetArrayItem(const J *array, int index);
/* Get item "string" from object. Case insensitive. */
N_CJSON_PUBLIC(J *) JGetObjectItem(const J * const object, const char * const string);
N_CJSON_PUBLIC(J *) JGetObjectItemCaseSensitive(const J * const object, const char * const string);
N_CJSON_PUBLIC(Jbool) JHasObjectItem(const J *object, const char *string);
/* For analysing failed parses. This returns a pointer to the parse error. You'll probably need to look a few chars back to make sense of it. Defined when JParse() returns 0. 0 when JParse() succeeds. */
N_CJSON_PUBLIC(const char *) JGetErrorPtr(void);

/* Check if the item is a string and return its valuestring */
N_CJSON_PUBLIC(char *) JGetStringValue(J *item);

/* These functions check the type of an item */
N_CJSON_PUBLIC(Jbool) JIsInvalid(const J * const item);
N_CJSON_PUBLIC(Jbool) JIsFalse(const J * const item);
N_CJSON_PUBLIC(Jbool) JIsTrue(const J * const item);
N_CJSON_PUBLIC(Jbool) JIsBool(const J * const item);
N_CJSON_PUBLIC(Jbool) JIsNull(const J * const item);
N_CJSON_PUBLIC(Jbool) JIsNumber(const J * const item);
N_CJSON_PUBLIC(Jbool) JIsString(const J * const item);
N_CJSON_PUBLIC(Jbool) JIsArray(const J * const item);
N_CJSON_PUBLIC(Jbool) JIsObject(const J * const item);
N_CJSON_PUBLIC(Jbool) JIsRaw(const J * const item);

/* These calls create a J item of the appropriate type. */
N_CJSON_PUBLIC(J *) JCreateNull(void);
N_CJSON_PUBLIC(J *) JCreateTrue(void);
N_CJSON_PUBLIC(J *) JCreateFalse(void);
N_CJSON_PUBLIC(J *) JCreateBool(Jbool boolean);
N_CJSON_PUBLIC(J *) JCreateNumber(JNUMBER num);
N_CJSON_PUBLIC(J *) JCreateString(const char *string);
/* raw json */
N_CJSON_PUBLIC(J *) JCreateRaw(const char *raw);
N_CJSON_PUBLIC(J *) JCreateArray(void);
N_CJSON_PUBLIC(J *) JCreateObject(void);

/* Create a string where valuestring references a string so
 * it will not be freed by JDelete */
N_CJSON_PUBLIC(J *) JCreateStringValue(const char *string);
N_CJSON_PUBLIC(J *) JCreateStringReference(const char *string);
/* Create an object/arrray that only references it's elements so
 * they will not be freed by JDelete */
N_CJSON_PUBLIC(J *) JCreateObjectReference(const J *child);
N_CJSON_PUBLIC(J *) JCreateArrayReference(const J *child);

/* These utilities create an Array of count items. */
N_CJSON_PUBLIC(J *) JCreateIntArray(const long int *numbers, int count);
N_CJSON_PUBLIC(J *) JCreateNumberArray(const JNUMBER *numbers, int count);
N_CJSON_PUBLIC(J *) JCreateStringArray(const char **strings, int count);

/* Append item to the specified array/object. */
N_CJSON_PUBLIC(void) JAddItemToArray(J *array, J *item);
N_CJSON_PUBLIC(void) JAddItemToObject(J *object, const char *string, J *item);
/* Use this when string is definitely const (i.e. a literal, or as good as), and will definitely survive the J object.
 * WARNING: When this function was used, make sure to always check that (item->type & JStringIsConst) is zero before
 * writing to `item->string` */
N_CJSON_PUBLIC(void) JAddItemToObjectCS(J *object, const char *string, J *item);
/* Append reference to item to the specified array/object. Use this when you want to add an existing J to a new J, but don't want to corrupt your existing J. */
N_CJSON_PUBLIC(void) JAddItemReferenceToArray(J *array, J *item);
N_CJSON_PUBLIC(void) JAddItemReferenceToObject(J *object, const char *string, J *item);

/* Remove/Detatch items from Arrays/Objects. */
N_CJSON_PUBLIC(J *) JDetachItemViaPointer(J *parent, J * const item);
N_CJSON_PUBLIC(J *) JDetachItemFromArray(J *array, int which);
N_CJSON_PUBLIC(void) JDeleteItemFromArray(J *array, int which);
N_CJSON_PUBLIC(J *) JDetachItemFromObject(J *object, const char *string);
N_CJSON_PUBLIC(J *) JDetachItemFromObjectCaseSensitive(J *object, const char *string);
N_CJSON_PUBLIC(void) JDeleteItemFromObject(J *object, const char *string);
N_CJSON_PUBLIC(void) JDeleteItemFromObjectCaseSensitive(J *object, const char *string);

/* Update array items. */
N_CJSON_PUBLIC(void) JInsertItemInArray(J *array, int which, J *newitem); /* Shifts pre-existing items to the right. */
N_CJSON_PUBLIC(Jbool) JReplaceItemViaPointer(J * const parent, J * const item, J * replacement);
N_CJSON_PUBLIC(void) JReplaceItemInArray(J *array, int which, J *newitem);
N_CJSON_PUBLIC(void) JReplaceItemInObject(J *object,const char *string,J *newitem);
N_CJSON_PUBLIC(void) JReplaceItemInObjectCaseSensitive(J *object,const char *string,J *newitem);

/* Duplicate a J item */
N_CJSON_PUBLIC(J *) JDuplicate(const J *item, Jbool recurse);
/* Duplicate will create a new, identical J item to the one you pass, in new memory that will
need to be released. With recurse!=0, it will duplicate any children connected to the item.
The item->next and ->prev pointers are always zero on return from Duplicate. */
/* Recursively compare two J items for equality. If either a or b is NULL or invalid, they will be considered unequal.
 * case_sensitive determines if object keys are treated case sensitive (1) or case insensitive (0) */
N_CJSON_PUBLIC(Jbool) JCompare(const J * const a, const J * const b, const Jbool case_sensitive);


N_CJSON_PUBLIC(void) JMinify(char *json);

/* Helper functions for creating and adding items to an object at the same time.
 * They return the added item or NULL on failure. */
N_CJSON_PUBLIC(J*) JAddNullToObject(J * const object, const char * const name);
N_CJSON_PUBLIC(J*) JAddTrueToObject(J * const object, const char * const name);
N_CJSON_PUBLIC(J*) JAddFalseToObject(J * const object, const char * const name);
N_CJSON_PUBLIC(J*) JAddBoolToObject(J * const object, const char * const name, const Jbool boolean);
N_CJSON_PUBLIC(J*) JAddNumberToObject(J * const object, const char * const name, const JNUMBER number);
N_CJSON_PUBLIC(J*) JAddStringToObject(J * const object, const char * const name, const char * const string);
N_CJSON_PUBLIC(J*) JAddRawToObject(J * const object, const char * const name, const char * const raw);
N_CJSON_PUBLIC(J*) JAddObjectToObject(J * const object, const char * const name);
N_CJSON_PUBLIC(J*) JAddArrayToObject(J * const object, const char * const name);
#define JConvertToJSONString JPrintUnformatted
#define JConvertFromJSONString JParse

/* When assigning an integer value, it needs to be propagated to valuenumber too. */
#define JSetIntValue(object, number) ((object) ? (object)->valueint = (object)->valuenumber = (number) : (number))
/* helper for the JSetNumberValue macro */
N_CJSON_PUBLIC(JNUMBER) JSetNumberHelper(J *object, JNUMBER number);
#define JSetNumberValue(object, number) ((object != NULL) ? JSetNumberHelper(object, (JNUMBER)number) : (number))

/* Macro for iterating over an array or object */
#define JArrayForEach(element, array) for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)
// Iterate over the fields of an object
#define JObjectForEach(element, array) JArrayForEach(element, array)

/* malloc/free objects using the malloc/free functions that have been set with JInitHooks */
N_CJSON_PUBLIC(void *) JMalloc(size_t size);
N_CJSON_PUBLIC(void) JFree(void *object);

#ifdef __cplusplus
}
#endif

#endif
