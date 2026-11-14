# 后续应用迭代 / Next application iterations

当前软件入口为 Native、四个精确 F407 Board 的两后端、五种模板及独立共享 SPI
fixture。源码锁、实际本地验收和实板状态分别维护，编写了目标不等于执行成功。

The maintained scope has nine presets and five starters. Qualification follows
actual execution at the coordinated SDK pin; authored targets are not results.

| 工作 / Work | 必需输入 / Inputs | 退出条件 / Acceptance |
| --- | --- | --- |
| 四板启动与关闭 / Board lifecycle | PCB、探针、串口、晶振与供电 | 实际启动、时钟、IRQ、UART TC、停止和恢复 |
| 传感器 / Sensor | 引脚、CS、电气、器件型号 | 实际身份、已知标定、负载下时序 |
| RS485 | DE/RE、收发器、拓扑与终端 | TC 后撤 DE、静默间隔、丢失和恢复 |
| MCU 持久化 / Persistence | 产品保留区和维护窗口 | 擦写干扰与掉电恢复，不自动重试 |
| ILI9341／XPT2046 | 实物装配、共享接口和时序 | 显示、触摸、冲突及安全停止实测 |
| 模拟采集与 DAC / Analog | 精确引脚、负载、参考电压 | 精度、settling、触发和停止电平实测 |
| 共享 owner / Shared owner | 真实控制器与端点接线、业务负载 | 仲裁、取消、排空和最后借用归还 |
| 联网 / Networking | 产品地址、PHY、威胁模型与恢复策略 | 实际链路、协议、认证及失败恢复 |

平台通用驱动缺陷在 Nexus 修复并增加真实合同测试，应用组合与产品策略保留在这里。
不增加隐式 worker、产品框架或根据 GPIO/UART 编译成功推断其他硬件资格。

Generic provider defects are repaired in Nexus. Application wiring, storage and
recovery stay here or in the owning product. All current physical HIL remains
`not_executed`; extending platform code never supplies missing station evidence.
