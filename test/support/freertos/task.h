#ifndef TEST_FREERTOS_TASK_H
#define TEST_FREERTOS_TASK_H
extern void (*testTask)(void*);
extern bool testTaskFailure;
inline int xTaskCreate(void (*task)(void*), const char*, unsigned,
                       void*, unsigned, void*) {
  if (testTaskFailure) {
    return 0;
  }
  testTask = task;
  return pdPASS;
}
inline void (*&testTaskDeleteHook())() {
  static void (*hook)() = nullptr;
  return hook;
}
inline void vTaskDelete(void*) {
  if (testTaskDeleteHook()) testTaskDeleteHook()();
}
#endif
