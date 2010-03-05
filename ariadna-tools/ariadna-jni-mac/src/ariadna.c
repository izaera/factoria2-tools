/*
 * @(#)ariadna.c		1.0 04/11/06
 * based on @(#)heapViewer.c	1.5 04/07/27
 * 
 * Copyright (c) 2004 Matthias Ernst. All Rights Reserved.
 * Copyright (c) 2004 Sun Microsystems, Inc. All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * -Redistribution of source code must retain the above copyright notice, this
 *  list of conditions and the following disclaimer.
 * 
 * -Redistribution in binary form must reproduce the above copyright notice, 
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 * 
 * Neither the name of Sun Microsystems, Inc. or the names of contributors may 
 * be used to endorse or promote products derived from this software without 
 * specific prior written permission.
 * 
 * This software is provided "AS IS," without a warranty of any kind. ALL 
 * EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND WARRANTIES, INCLUDING
 * ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * OR NON-INFRINGEMENT, ARE HEREBY EXCLUDED. SUN MIDROSYSTEMS, INC. ("SUN")
 * AND ITS LICENSORS SHALL NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY LICENSEE
 * AS A RESULT OF USING, MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS
 * DERIVATIVES. IN NO EVENT WILL SUN OR ITS LICENSORS BE LIABLE FOR ANY LOST 
 * REVENUE, PROFIT OR DATA, OR FOR DIRECT, INDIRECT, SPECIAL, CONSEQUENTIAL, 
 * INCIDENTAL OR PUNITIVE DAMAGES, HOWEVER CAUSED AND REGARDLESS OF THE THEORY 
 * OF LIABILITY, ARISING OUT OF THE USE OF OR INABILITY TO USE THIS SOFTWARE, 
 * EVEN IF SUN HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * You acknowledge that this software is not designed, licensed or intended
 * for use in the design, construction, operation or maintenance of any
 * nuclear facility.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "jni.h"
#include "jvmti.h"

#include "org_mernst_ariadna_agent_Agent.h"


/* Check for NULL pointer error */
#define CHECK_FOR_NULL(ptr) \
	checkForNull(ptr, __FILE__, __LINE__)
static void
checkForNull(void *ptr, char *file, int line)
{
    if ( ptr == NULL ) {
	fprintf(stderr, "ERROR: NULL pointer error in %s:%d\n", file, line);
	abort();
    }
}

/* Deallocate JVMTI memory */
static void
deallocate(jvmtiEnv *jvmti, void *p)
{
    jvmtiError err;

    err = (*jvmti)->Deallocate(jvmti, (unsigned char *)p);
    if ( err != JVMTI_ERROR_NONE ) {
	fprintf(stderr, "ERROR: JVMTI Deallocate error err=%d\n", err);
	abort();
    }
}

/* Get name for JVMTI error code */
static char *
getErrorName(jvmtiEnv *jvmti, jvmtiError errnum)
{
    jvmtiError err;
    char      *name;

    err = (*jvmti)->GetErrorName(jvmti, errnum, &name);
    if ( err != JVMTI_ERROR_NONE ) {
	fprintf(stderr, "ERROR: JVMTI GetErrorName error err=%d\n", err);
	abort();
    }
    return name;
}

/* Check for JVMTI error */
#define CHECK_JVMTI_ERROR(jvmti, err) \
	checkJvmtiError(jvmti, err, __FILE__, __LINE__)
static void
checkJvmtiError(jvmtiEnv *jvmti, jvmtiError err, char *file, int line)
{
  if ( err != JVMTI_ERROR_NONE ) {
	char *name;

	name = getErrorName(jvmti, err);
	fprintf(stderr, "ERROR: JVMTI error err=%d(%s) in %s:%d\n",
		err, name, file, line);
	deallocate(jvmti, name);
	abort();
  }
}

static jvmtiEnv *theJvmti;

/* Typedef to hold class details */
typedef struct {
    char *signature;
    long instances;
    long bytes;
} ClassDetails;

/* Global static data */
typedef struct {
    FILE 	  *out;
    jint	  numClasses;
    ClassDetails  *details;
    jlong	  objectCounter;
} GlobalData;

#define TYPE_ROOT (JVMTI_REFERENCE_CONSTANT_POOL + 1)
#define TYPE_STACK (TYPE_ROOT + 1)
#define TYPE_SUPERCLASS (TYPE_STACK + 1)

static void writeLong(FILE *out, jlong l) {
  char b[8];
  int i=0;
  for(i=0; i<8; i++) {
    b[7-i] = (l & 0xFF); l >>= 8;
  }
  fwrite(b, 8, 1, out);
}

static void writeRecord(void *udata, jlong o, char type, jlong arg) {
  GlobalData *gdata = (GlobalData*)udata;
  writeLong(gdata->out, o);
  fwrite(&type, 1, 1, gdata->out);
  writeLong(gdata->out, arg);
}

static void tagObject(void *user_data, jlong* tag_ptr, jlong class_tag, jlong size) {
  GlobalData *gdata = (GlobalData*)user_data;

  if(*tag_ptr == 0) {
    ClassDetails *d = &(gdata->details[class_tag]);
    d->instances++;
    d->bytes+=size;

    *tag_ptr = (gdata->objectCounter++);

    writeRecord(user_data, *tag_ptr, JVMTI_REFERENCE_CLASS, class_tag);
  }
}


static jvmtiIterationControl JNICALL root(
     jvmtiHeapRootKind root_kind,
     jlong class_tag,
     jlong size,
     jlong* tag_ptr,
     void* user_data) {

  tagObject(user_data, tag_ptr, class_tag, size);
  writeRecord(user_data, *tag_ptr, TYPE_ROOT, root_kind);

  return JVMTI_ITERATION_CONTINUE;
}

static jvmtiIterationControl JNICALL stack(
     jvmtiHeapRootKind root_kind,
     jlong class_tag,
     jlong size,
     jlong* tag_ptr,
     jlong thread_tag,
     jint depth,
     jmethodID method,
     jint slot,
     void* user_data) {

  tagObject(user_data, tag_ptr, class_tag, size);
  writeRecord(user_data, *tag_ptr, TYPE_STACK, thread_tag);

  return JVMTI_ITERATION_CONTINUE;
}


static jvmtiIterationControl JNICALL reportReference(
     jvmtiObjectReferenceKind reference_kind,
     jlong class_tag,
     jlong size,
     jlong* tag_ptr,
     jlong referrer_tag,
     jint referrer_index,
     void* user_data)
{
  GlobalData *gdata = (GlobalData*)user_data;

  tagObject(user_data, tag_ptr, class_tag, size);
  if(reference_kind == JVMTI_REFERENCE_CLASS) {
    if(referrer_tag < gdata->numClasses)
      writeRecord(user_data, *tag_ptr, TYPE_SUPERCLASS, referrer_tag);
  } else {
    writeRecord(user_data, *tag_ptr, reference_kind, referrer_tag);
  }

  return JVMTI_ITERATION_CONTINUE;
}

/* Heap object callback */
static jvmtiIterationControl JNICALL
untagObject(jlong class_tag, jlong size, jlong* tag_ptr, void* user_data)
{
  (*tag_ptr) = 0;
  return JVMTI_ITERATION_CONTINUE;
}


/* Callback for JVMTI_EVENT_DATA_DUMP_REQUEST (Ctrl-\ or at exit) */
static void JNICALL
dataDumpRequest(jvmtiEnv *jvmti)
{
  GlobalData globalData, *gdata = &globalData;
  jvmtiError    err;
  void         *user_data;
  jclass       *classes;
  jint          i;
  FILE	 *classFile;

  gdata->out = fopen("heap", "wb");
  if ( gdata->out == NULL ) {
    fprintf(stderr, "ERROR: Could not open heap for writing\n");
    return;
  }

  classFile = fopen("classes.txt", "w");
  if(classFile == NULL) {
    fprintf(stderr, "ERROR: Could not open classes for writing\n");
    fclose(gdata->out);
    return;
  }

  fprintf(stderr, "ariadna: dumping heap ...\n");

  /* Get all the loaded classes */
  err = (*jvmti)->GetLoadedClasses(jvmti, &gdata->numClasses, &classes);
  CHECK_JVMTI_ERROR(jvmti, err);

  gdata->numClasses++;

  /* Setup an area to hold details about these classes */
  gdata->details = (ClassDetails*)calloc(sizeof(ClassDetails), gdata->numClasses);
  CHECK_FOR_NULL(gdata->details);

  gdata->details[0].signature = "unknownClass";

  for ( i = 1 ; i < gdata->numClasses ; i++ ) {
    char *sig;

    /* Get and save the class signature */
    err = (*jvmti)->GetClassSignature(jvmti, classes[i-1], &sig, NULL);
    CHECK_JVMTI_ERROR(jvmti, err);
    CHECK_FOR_NULL(sig);

    gdata->details[i].signature = strdup(sig);
    deallocate(jvmti, sig);

    /* Tag this jclass */
    err = (*jvmti)->SetTag(jvmti, classes[i-1], (jlong)i);
    CHECK_JVMTI_ERROR(jvmti, err);
  }

  gdata->objectCounter = gdata->numClasses;

  /* Iterate over the heap */
  err = (*jvmti)->IterateOverReachableObjects(jvmti, &root, &stack,
				  &reportReference, gdata);
  CHECK_JVMTI_ERROR(jvmti, err);

  for ( i = 0 ; i < gdata->numClasses; i++ ) {
    fprintf(classFile, "%s %ld %ld\n", gdata->details[i].signature, gdata->details[i].instances, gdata->details[i].bytes);
  }

  /* untag previously tagged objects */
  err = (*jvmti)->IterateOverHeap(jvmti, JVMTI_HEAP_OBJECT_TAGGED,
				  &untagObject, gdata);
  CHECK_JVMTI_ERROR(jvmti, err);

  /* Free up all allocated space */
  deallocate(jvmti, classes);
  for ( i = 1 ; i < gdata->numClasses; i++ ) {
		if ( gdata->details[i].signature != NULL ) {
	  free(gdata->details[i].signature);
      }
  }
  free(gdata->details);
  fclose(gdata->out);
  fclose(classFile);

  fprintf(stderr, "ariadna: ... done\n");
}

/* Callback for JVMTI_EVENT_VM_INIT */
static void JNICALL 
vmInit(jvmtiEnv *jvmti, JNIEnv *env, jthread thread)
{
  jvmtiError    err;
	
  err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
		  JVMTI_EVENT_DATA_DUMP_REQUEST, NULL);
  CHECK_JVMTI_ERROR(jvmti, err);
}

/* Agent_OnLoad() is called first, we prepare for a VM_INIT event here. */
JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
  jint          rc;
  jvmtiError          err;
  jvmtiCapabilities   capabilities;
  jvmtiEventCallbacks callbacks;
  jvmtiEnv           *jvmti;

  /* Get JVMTI environment */
  jvmti = NULL;
  rc = (*vm)->GetEnv(vm, (void **)&jvmti, JVMTI_VERSION);
  if (rc != JNI_OK) {
    fprintf(stderr, "ERROR: Unable to create jvmtiEnv, GetEnv failed, error=%d\n", rc);
    return -1;
  }

  /* Get/Add JVMTI capabilities */
  err = (*jvmti)->GetCapabilities(jvmti, &capabilities);
  CHECK_JVMTI_ERROR(jvmti, err);
  capabilities.can_tag_objects = 1;
  capabilities.can_generate_garbage_collection_events = 1;
  err = (*jvmti)->AddCapabilities(jvmti, &capabilities);
  CHECK_JVMTI_ERROR(jvmti, err);

  /* Set callbacks and enable event notifications */
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.VMInit                  = &vmInit;
  callbacks.DataDumpRequest         = &dataDumpRequest;
  err = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, sizeof(callbacks));
  CHECK_JVMTI_ERROR(jvmti, err);
  err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE,
			JVMTI_EVENT_VM_INIT, NULL);
  CHECK_JVMTI_ERROR(jvmti, err);

  theJvmti = jvmti;

  fprintf(stderr, "ariadna: successfully loaded\n");
  return 0;
}

/* Agent_OnUnload() is called last */
JNIEXPORT void JNICALL
Agent_OnUnload(JavaVM *vm)
{
}

JNIEXPORT void JNICALL Java_org_mernst_ariadna_agent_Agent_dump
  (JNIEnv *ignore1, jclass ignore2)
{
  dataDumpRequest(theJvmti);
}
