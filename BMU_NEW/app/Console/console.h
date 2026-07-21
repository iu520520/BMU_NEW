#ifndef CONSOLE_H
#define CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化命令缓存并显示可用命令。 */
void console_init(void);

/* 非阻塞处理串口命令，并按需打印 AFE 电压。 */
void console_task(void);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_H */
