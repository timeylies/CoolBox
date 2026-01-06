#if !defined(BASE_TASK_H)
#define BASE_TASK_H

class BaseTask {
public:
  BaseTask(uint16_t stackSize, UBaseType_t priority,
           const char *taskName = "") {
            StackSize = stackSize;
            Priority = priority;
            TaskName = taskName;
  }

  void init() {
    xTaskCreate(task, TaskName, StackSize, this,
                Priority, &this->taskHandle);
  }

  virtual void main() = 0;

protected:
  uint16_t StackSize;
  UBaseType_t Priority;
  const char *TaskName;
  static void task(void *pvParam) {
    BaseTask *pBaseTask = static_cast<BaseTask *>(pvParam);
    pBaseTask->main();
  }

  TaskHandle_t taskHandle;

};      // end class BaseTask() --------------------------------------------------
#endif  //BASE_TASK_H