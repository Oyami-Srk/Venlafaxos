#ifndef __MODULES_H__
#define __MODULES_H__

// code below is auto-generated by python script
// {Insert Below}

// System Task's process
extern void SysTask(void);
// FS Process
extern void Task_FS(void);
// TTY process
extern void Task_TTY(void);
// Test Process
extern void Task_Test(void);
// HD Process
extern void Task_HD(void);

#define __MODULES_COUNT__ 5
#define __MODULES_ENTRIES__ {(unsigned int)SysTask,(unsigned int)Task_FS,(unsigned int)Task_TTY,(unsigned int)Task_Test,(unsigned int)Task_HD}
#define __MODULES_PREFERRED_PID__ {(unsigned int)1,(unsigned int)0xFFFFFFFE,(unsigned int)0xFFFFFFFE,(unsigned int)0xFFFFFFFE,(unsigned int)0xFFFFFFFE}

// {Insert Above}
// code above is auto-generated by python script

#endif // __MODULES_H__
