#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <jni.h>
#include <jvmti.h>

FILE* stats_file = NULL;

#define MAX_MONITOR  1024

typedef struct 
{
  jobject monitor;
  int contended_count;
} contended_monitor_t;

contended_monitor_t monitors_stats[MAX_MONITOR];

void open_file() 
{
  char statsFileName[512];
  sprintf(statsFileName, "SynchronizedStats.txt");
  stats_file = fopen(statsFileName, "w");
}

void close_file()
{
  fclose(stats_file);
}

void JNICALL cbVMInit(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jthread thread) 
{
  if (stats_file == NULL)
    open_file();
}


static void JNICALL cbVMStart(jvmtiEnv* jvmti, JNIEnv* env) 
{
  jvmtiJlocationFormat format;
  (*jvmti)->GetJLocationFormat(jvmti, &format);
  //  printf("[contention-profiling] VMStart LocationFormat: %d\n", format);
}

static void JNICALL cbVMDeath(jvmtiEnv* jvmti, JNIEnv* env)
{
  if (stats_file != NULL)
    close_file();
  stats_file = NULL;
}

int get_line_number(jvmtiEnv* jvmti_env, jmethodID method, jlocation location)
{
    jvmtiLineNumberEntry* line_table;
    jint entry_count;
    int err = (*jvmti_env)->GetLineNumberTable(jvmti_env, method, &entry_count, &line_table);
    if (err == JVMTI_ERROR_NATIVE_METHOD || err == JVMTI_ERROR_ABSENT_INFORMATION)
      return 0;
    int entry;
    int line_number = -1;
    for (entry = 0; entry < entry_count; entry++)
    {
      if (line_table[entry].start_location < location)
	line_number = line_table[entry].line_number;
      else
	break;
    }  
    (*jvmti_env)->Deallocate(jvmti_env, (char*)line_table);
    return line_number;
}

void jni_sig_to_java_name(char* jni_sig, char* java_name)
{
  char c = *jni_sig;
  if (c == 0)
    return;
  if (c == 'L')
  {
    jni_sig++;
    c = *jni_sig;
  }
  while (c != 0)
  {
    if (c == ';')
    {
      jni_sig++;
      c = *jni_sig;
      continue;
    }
    if (c == '/')
      c = '.';
    *java_name = c;
    java_name++;
    jni_sig++;
    c = *jni_sig;
  }
  *java_name = 0;
}

int search_contended_monitor(jvmtiEnv* jvmti_env, jthread thread, jobject monitor)
{
  int i;
  for (i = 0; i < MAX_MONITOR; i++)
  {
    contended_monitor_t* cmon = &monitors_stats[i];
    jobject current = cmon->monitor;
    if (current == NULL) // last element, no match => create a new one
    {
      cmon->monitor = monitor;
      return i;
    }
    if (monitor == current)
    {
      return i;
    }
  }
  return -1;
}

static void JNICALL cbMonitorContendedEnter(jvmtiEnv *jvmti_env, JNIEnv* jni_env, jthread thread, jobject object)
{
  char* name;
  char javaName[512];
  jclass c;

  int idx = search_contended_monitor(jvmti_env, thread, object);
  if (idx < 0)
  {
    printf("Error: monitor_stats full!");
    return;
  }
  contended_monitor_t* cmon = &monitors_stats[idx];
  cmon->contended_count++;
  if (cmon->contended_count == 1)
  {
    // get Monitor type
    c = (*jni_env)->GetObjectClass(jni_env, object);
    (*jvmti_env)->GetClassSignature(jvmti_env, c, &name, NULL);
    jni_sig_to_java_name(name, javaName);
    fprintf(stats_file, "contention detected on [%d] %s", idx, javaName);
    (*jvmti_env)->Deallocate(jvmti_env, name);
  
    // get stack trace from thread blocked
    jvmtiFrameInfo frames[512];
    jint count;
    jint frame_count;
    (*jvmti_env)->GetFrameCount(jvmti_env, thread, &frame_count);
    // can dynamically allocate jvmtiFramInfo array using frame_count
    //    frames = malloc(frame_count*sizeof(jvmtiFrameInfo));
    (*jvmti_env)->GetStackTrace(jvmti_env, thread, 0, frame_count, frames, &count);
    int i;
    for (i = 0; i < count; i++)
    {
      char* method_name;
      char* class_sig;
      char java_class_sig[512];
      jclass clazz;
      (*jvmti_env)->GetMethodDeclaringClass(jvmti_env, frames[i].method, &clazz);
      (*jvmti_env)->GetClassSignature(jvmti_env, clazz, &class_sig, NULL);
      jni_sig_to_java_name(class_sig, java_class_sig);
      (*jvmti_env)->GetMethodName(jvmti_env, frames[i].method, &method_name, NULL, NULL);
      int line_number = get_line_number(jvmti_env, frames[i].method, frames[i].location);
      fprintf(stats_file, " %s.%s:%d, ", java_class_sig, method_name, line_number);
      (*jvmti_env)->Deallocate(jvmti_env, class_sig);
      (*jvmti_env)->Deallocate(jvmti_env, method_name);
    }
    // print infos
    fprintf(stats_file, "\n");
    fflush(stats_file);
    //    free(frames);
  }
  else
  {
    fprintf(stats_file, "new contention detected on [%d] ", idx);
    int i;
    for (i = 0; i < MAX_MONITOR; i++)
    {
      if (monitors_stats[i].monitor == NULL)
	break;
      fprintf(stats_file, "%d=%d ", i, monitors_stats[i].contended_count);
    }
    fprintf(stats_file, "\n");
    fflush(stats_file);
  }
}

void collect_sync_stats(void* arg)
{
  jvmtiEnv* jvmti_env = (jvmtiEnv*)arg;
  while (1)
  {
    int res;
    printf("collecting for all threads...\n");
    /*
    jthread* threads;
    res = (*jvmti_env)->GetAllThreads(jvmti_env, &threads_count, &threads);
    if (res != JVMTI_ERROR_NONE)
    {
      printf("error on GetAllThreads: %d\n", res);
      return;
    }
    int i;
    for (i = 0; i < threads_count; i++)
    {
      jthread thread = threads[i];
      res = (*jvmti_env)->GetOwnedMonitorInfo(jvmti_env, thread, );
    }
    */
    sleep(1);
  }
}


JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void* reserved) 
{
  jvmtiEnv* jvmti;
  jvmtiError error;
  jint res;
  jvmtiCapabilities capabilities;
  jvmtiEventCallbacks callbacks;

  // Create the JVM TI environment (jvmti).
  res = (*vm)->GetEnv(vm, (void **)&jvmti, JVMTI_VERSION_1);
  if (res != JNI_OK)
  {
    printf("Error on getEnv: %d\n", res);
    return JNI_ERR;
  }
  memset(monitors_stats, 0, MAX_MONITOR*sizeof(monitors_stats[0]));
  // Parse the options supplied to this agent on the command line.
  //parse_agent_options(options);
  // handle 2 modes: event or poll
  // If options don't parse, do you want this to be an error?

  // Clear the capabilities structure and set the ones you need.
  (void)memset(&capabilities,0, sizeof(capabilities));
  capabilities.can_get_owned_monitor_info = 1;
  capabilities.can_get_owned_monitor_stack_depth_info = 1;
  capabilities.can_generate_monitor_events = 1;
  capabilities.can_get_line_numbers = 1;
  capabilities.can_get_monitor_info = 1;
  
  // Request these capabilities for this JVM TI environment.
  error = (*jvmti)->AddCapabilities(jvmti, &capabilities);
  if (error != JVMTI_ERROR_NONE)
  {
    printf("error on AddCapabilities: %d\n", error);
    return JNI_ERR;
  }

  // Clear the callbacks structure and set the ones you want.
  (void)memset(&callbacks,0, sizeof(callbacks));
  callbacks.VMInit = &cbVMInit;
  callbacks.VMStart = &cbVMStart;
  callbacks.VMDeath = &cbVMDeath;
  //    callbacks.MonitorWait = &cbMonitorWait;
  //    callbacks.MonitorWaited = &cbMonitorWaited;
  callbacks.MonitorContendedEnter = &cbMonitorContendedEnter;
  //    callbacks.MonitorContendedEntered = &cbMonitorContendedEntered;
  error = (*jvmti)->SetEventCallbacks(jvmti, &callbacks, (jint)sizeof(callbacks));
  if (error != JVMTI_ERROR_NONE)
  {
    printf("error on SetEventCallbacks: %d", error);
    return JNI_ERR;
  }

  // For each of the above callbacks, enable this event.
  error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, (jthread)NULL);
  error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_START, (jthread)NULL);
  error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, (jthread)NULL);
  error = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_MONITOR_CONTENDED_ENTER, (jthread)NULL);
  // In all the above calls, check errors.
  /*
  pthread_t collect_stats_thread;
  int pthread_res =pthread_create(&collect_stats_thread, NULL, &collect_sync_stats, (void*)jvmti);
  if (pthread_res != 0)
  {
    printf("Error creating thread: %d\n", pthread_res);
    return JNI_ERR;
  } 
  */
  return JNI_OK; // Indicates to the VM that the agent loaded OK.
}



