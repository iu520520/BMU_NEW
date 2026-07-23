# BMU_NEW Firmware

## 目录

- `USER/`、`Driver/`、`BMU_NEW.ntfx`：NTFx生成的启动、外设和芯片驱动
- `app/AFE/`：MP2797驱动和分步非阻塞采样状态机
- `app/CellBalance/`：手动均衡控制
- `app/Console/`：PC串口命令
- `app/Drivers/`：软件I2C、UART和RS232底层
- `app/freertos_app.c`：FreeRTOS任务创建及UART中断唤醒
- `Middlewares/Third_Party/FreeRTOS/`：厂商SDK随附的FreeRTOS 10.5.1

## FreeRTOS任务

| 任务 | 优先级 | 行为 |
| --- | ---: | --- |
| Console | 3 | 平时阻塞；UART4收到字符后由中断立即唤醒 |
| AFE | 2 | 每1 ms推进一次MP2797状态机 |

Console优先级高于AFE。UART4中断将字符写入环形缓冲区后发送任务通知，并通过
`portYIELD_FROM_ISR`立即触发抢占。

UART4的抢占优先级必须保持为5；它与
`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`相匹配。

## 构建

```powershell
cmake --preset n32g455-debug
cmake --build --preset n32g455-debug --parallel
```

NTFx重新生成代码后，需要确认
`USER/src/n32g45x_it.c`中的FreeRTOS SysTick调用和UART4任务通知仍然保留。
