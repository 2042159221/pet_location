# E220x API 设计拆解

## 1. 先给结论

这套 API 设计的核心不是“函数多”，而是两句话：

1. 对上层暴露统一能力，不暴露寄存器细节
2. 对下层封装硬件动作，不把 HAL 写进芯片逻辑

这就是一个合格驱动库最重要的 API 意识。

## 2. API 分层

### 2.1 配置 API

严格说，`ebyte_conf.h` 里的宏也是 API，只不过是编译期 API：

- `EBYTE_E220_400M22S`
- `EBYTE_PORT_SPI_CS_SOFTWARE`
- `EBYTE_RF_TRANSMIT_CHECK_MODE`

这种 API 的特点是：

- 不通过函数传参
- 编译后固定
- 适合产品型号、硬件连线方式、驱动策略这种“不会在运行时频繁变化”的东西

### 2.2 统一接口 API

`ebyte_core.h` 定义的 `Ebyte_RF_t` 是整套设计最关键的 API。

它把一个射频驱动抽象成以下能力：

- `Init`
- `Send`
- `EnterSleepMode`
- `EnterReceiveMode`
- `StartPollTask`
- `InterruptTrigger`
- `GetName`
- `GetDriverVersion`

这套能力抽象得很克制，没有把寄存器读写暴露给上层，这非常对。

### 为什么这样设计好

因为上层真正关心的是：

- 能不能初始化
- 能不能发
- 能不能收
- 能不能周期处理
- 当前到底是什么模块

上层根本不应该关心 `RADIO_SET_PACKETTYPE` 是多少。

### 2.3 芯片实现 API

`ebyte_e220x.h` 暴露的是“面向功能”的高层 API，例如：

- `Ebyte_E220x_Init`
- `Ebyte_E220x_SetRfFrequency`
- `Ebyte_E220x_SetRx`
- `Ebyte_E220x_SendPayload`
- `Ebyte_E220x_IntOrPollTask`
- `Ebyte_E220x_SetSleep`

这说明头文件作者希望外部以“功能调用”的方式使用驱动，而不是以“寄存器工具箱”的方式使用。

### 2.4 端口 API

`ebyte_port.h` 暴露的是“硬件动作接口”：

- SPI 收发
- CS 控制
- RST 控制
- TXEN 控制
- RXEN 控制
- BUSY 读取
- 毫秒延时

这一层的设计很值得学，因为它不暴露 HAL，也不把 HAL 写进芯片驱动里，而是让芯片驱动只依赖抽象动作。

### 2.5 回调 API

`ebyte_callback.h` 只给出两个回调：

- `Ebyte_Port_TransmitCallback(uint16e_t state)`
- `Ebyte_Port_ReceiveCallback(uint16e_t state, uint8e_t *buffer, uint8e_t length)`

这层 API 的输入非常典型：

- 一个状态码，告诉你发生了什么
- 一组数据指针和长度，只在接收场景下提供

### 2.6 项目接口 API

`Inf_LoRa.c` 又做了一层更项目化的接口：

- `Inf_LoRa_Init()`
- `Inf_LoRa_SenData()`
- `Inf_LoRa_ReadData()`

这一层的好处是：

- 应用层以后即便换底层驱动，也只需要改 `Inf`
- `App` 层不需要知道 `Ebyte_RF` 的存在

## 3. 参数设计方式

### 3.1 `buffer + size` 是整套 API 的主形态

这套源码大量使用：

```c
uint8e_t *buffer, uint8e_t size
```

或者：

```c
uint8e_t *payload, uint8e_t size
```

这是嵌入式 C 最典型的风格，原因很现实：

- 不依赖动态内存
- 不依赖复杂对象
- 调用成本低
- 很容易映射到 FIFO / DMA / 寄存器块

### 3.2 `out` 参数用来返回多值

比如：

- `Ebyte_E220x_GetRxBufferStatus(uint8e_t *payloadLength, uint8e_t *rxStartBufferPointer)`
- `Ebyte_E220x_GetPayload(uint8e_t *buffer, uint8e_t *size, uint8e_t maxSize)`

这说明作者没有试图用复杂结构体包一层结果对象，而是直接用指针把多个结果回填出去。

### 3.3 `command + buffer + size` 是底层原语 API 的标准形状

例如：

- `Ebyte_E220x_WriteCommand(command, buffer, size)`
- `Ebyte_E220x_ReadCommand(command, buffer, size)`

这类 API 很有代表性，因为它把“芯片命令协议”的最小公共模型抽象出来了：

1. 一条命令
2. 一段附加数据
3. 一段读回数据

如果你以后自己写 I2C/SPI 器件驱动，也很适合先把这种原语 API 抽出来。

## 4. 统一接口对象 `Ebyte_RF_t` 为什么是高明设计

### 4.1 它解决了“多型号兼容”的问题

`ebyte_core.c` 根据宏配置把 `Ebyte_RF` 绑定到不同芯片实现。这意味着上层代码不需要知道产品型号。

这本质上是：

- 编译期选择具体实现
- 运行时通过统一结构体访问

这是 C 语言里很实用的“轻量多态”。

### 4.2 它避免了到处写 `#if defined(...)`

如果没有 `Ebyte_RF_t`，上层代码很可能会变成：

```c
#if defined(EBYTE_E220_400M22S)
Ebyte_E220x_SendPayload(...);
#elif defined(...)
...
#endif
```

这种写法会快速污染上层业务代码。现在作者把这种判断集中到了 `ebyte_core.c`，这就是好的边界设计。

### 4.3 它也暴露了一个权衡

虽然使用了函数指针表，但并没有做“动态注册”，而是全局常量对象 `const Ebyte_RF_t Ebyte_RF`。

优点：

- 简单
- 无初始化顺序风险
- 无额外注册代码

代价：

- 运行时不能热切换实现
- 只能靠编译期宏选型

对 MCU 驱动来说，这个权衡是合理的。

## 5. 暴露与隐藏的策略

### 5.1 做得好的地方

- 应用层不直接操作寄存器
- 应用层不直接操作 `ebyte_e220x.c`
- HAL 细节被 `ebyte_port.c` 吸收
- 产品型号选择被 `ebyte_core.c` 吸收

### 5.2 不够严的地方

#### 第一，内部函数没有系统性 `static`

例如 `WriteCommand`、`ReadRegisters`、`SetStandby` 等很多函数虽然没有放入公共头文件，但本身不是 `static`。

这意味着：

- 从“头文件暴露层面”看，它们是隐藏的
- 从“链接属性层面”看，它们其实还是全局符号

这是“逻辑隐藏”和“语言级隐藏”不完全一致。

#### 第二，回调是硬编码依赖，不是注册式 API

芯片层直接调用：

- `Ebyte_Port_TransmitCallback()`
- `Ebyte_Port_ReceiveCallback()`

这种方式足够简单，但会让驱动层在编译期依赖回调层。更彻底的设计通常会把回调函数指针放进一个注册结构体，由上层注册。

#### 第三，返回值体系比较弱

多数 API 返回 `void`，错误处理更多靠：

- 忙等待
- 无限循环
- 外部状态回调

这对 demo 或小型工程足够，但对大型系统不够友好。

## 6. 接口命名风格

你可以从命名看出作者的分层意识：

- `Ebyte_E220x_*`：芯片能力
- `Ebyte_Port_*`：端口适配能力
- `Inf_LoRa_*`：项目接口能力

这个命名前缀系统很朴素，但非常有效。

不过也有工程细节不够严谨的地方：

- `Ebyte_Port_SpiTransmitAndReceivce` 中 `Receivce` 拼写有误
- `Inf_LoRa_SenData` 中 `SenData` 拼写有误
- `ebyte_e220x.h` 里 `Ebyte_E220x_SendPayload` 出现重复声明

这些问题不影响主设计，但会影响专业度和可维护性。

## 7. 从 API 设计里你应该学什么

真正值得内化的不是函数名，而是以下原则：

1. 先定义上层真正需要的能力，再写底层实现
2. 配置项尽量编译期固化，别把所有东西都做成运行时参数
3. 硬件相关动作必须隔离
4. 业务回调必须隔离
5. 公共接口尽量窄，内部原语自己消化

## 8. 本文档对应的关键源码定位

- `Int/LoRa/ebyte_conf.h:30-39`
- `Int/LoRa/ebyte_core.h:43-55`
- `Int/LoRa/ebyte_core.c:41-50`
- `Int/LoRa/E220xMx/ebyte_e220x.h:170-181`
- `Int/LoRa/E220xMx/ebyte_e220x.c:570-796`
- `Int/LoRa/E220xMx/ebyte_port.h:9-17`
- `Int/LoRa/E220xMx/ebyte_port.c:36-157`
- `Int/LoRa/E220xMx/ebyte_callback.h:3-5`
- `Int/LoRa/E220xMx/ebyte_callback.c:52-118`
- `Inf/Inf_LoRa.c:6-18`
