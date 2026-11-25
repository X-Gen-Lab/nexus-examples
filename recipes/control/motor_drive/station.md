# STM32F407ZG 电驱工装与测量记录

本清单适用于 `f407zg-power-stage-software-reference` 的 STM32F407ZGT6
（LQFP144），使用 8 MHz HSE 和 PLL168。绑定依据为 [Board](board/board.json)
和所选 TOML。当前没有已审核功率 PCB、实际 AFE、校准记录或实板测量，
物理状态为 `not_executed`。

## 配置边界

| 项目 | 相分流 [assembly.toml](assembly.toml) | 直流母线单分流 [single.toml](single.toml) |
| --- | --- | --- |
| 互补 PWM | TIM8 AF3，CH1/1N PC6/PA7，CH2/2N PC7/PB0，CH3/3N PC8/PB1 | 相同 |
| 硬件保护 | PA6 TIM8_BKIN，生成配置为高有效，TIM8_BRK_TIM12 IRQ | 相同 |
| 电流采集 | TIM8 CC4 触发 ADC1，PA0/PA1/PA2 为 A/B/C 顺序 rank | ADC1_IN0 PA0，每载波两个 active-vector 窗口 |
| 母线电压 | ADC1_IN3 PA3，第四个顺序 rank | 独立有来源身份和期限的 Task 观测 |
| 转子 | TIM4 AF2，PD12/PD13 A/B。ADC IRQ 读取计数 | 独立有来源身份和期限的 Task 观测 |
| 声明载波与时间预算 | 边沿对齐 1 kHz，IRQ 20 µs、计算 100 µs、提交点 990 µs | 边沿对齐 1 kHz，IRQ 10 µs、计算 50 µs、提交点 990 µs |

三相 rank 的顺序采样需观察通道间 skew。采用两相重构时记录实际 phase mask，
验证第三相重构。单分流需关联两次 ADC code、各自采样窗口及开关矢量，确认第二次
完成后只计算一次。TIM4 的计数观察与任何实际采样孔径分别记录。默认 main 从
`commissioned=false` 开始并在会话准入前退出，所以可用于冷态检查。活动链路时序
需要另有明确授权、接线与有界运行条件的产品工装。

## 工装记录

- 绑定 MCU marking、UID、物理 PCB 版本、原理图版本、实际 AFE 和功率级装配，
  以及探头配置、校准文件和独立捕获时钟。按每个软件 pad 核对导通、供电、接地、
  实际门极极性和相序。对照默认无激励和 BKIN 高有效，保存门极级实际观测及其
  输出抑制、故障保持和显式重新准入的边界。
- 标识每个实际分流电阻、AFE 增益／极性／偏移和 ADC 参考，记录零电流代码、
  有效量程、饱和点、单位换算、稳定时间及其原始样本。对照电机相序、编码器计数
  方向、每转计数、极对数和电角零位。将校准绑定到同一 PCB、功率级、电机与
  角度来源，记录 `calibration_identity`、`expected_calibration_identity`、
  `calibration_revision` 和 `motor_revision`。示例中的身份 `1` 与数值常量
  只用于软件检查。
- 保存同一构建的 source/toolchain、`resolved.json`、生成的 `bindings.c`、
  ELF、map、资源报告及内容哈希。每次采集附上确切负荷配置、IRQ 优先级、
  已选控制器、接线记录、测量分辨率、不确定度、已审 limits 和原始数据。

## 活动链路时序

绑定运行实际 ADC、快环计算和 PowerStage 提交的完整镜像。先测基线，再在同一
镜像上施加产品声明的并发负荷，记录选定 IRQ、DMA／总线速率、缓冲上限、服务频率
及负荷发生时段。输入条件、探头布置、触发和序号定义保持一致。保留测量点和编译
选项，注明测量插桩成本。空载与负荷下均保留逐周期关联的原始记录。

| 链路位置 | 相同计划序号下需保留的观测 |
| --- | --- |
| TIM8 CC4 → ADC → IRQ | 与载波对齐的触发及实际采样窗口，转换完成到 IRQ 入口延迟，顺序 rank 或双 DC 捕获的身份和损失 |
| IRQ → compute → shadow | 快环入口、退出及完整 CCR shadow 写毕，保存计算周期数和距离提交截止的最小余量 |
| shadow → 载波采用 | 用独立捕获关联实际载波更新与互补输出边沿，确认 staged 计划何时采用及是否存在缺失、重复或混用计划 |
| 故障与停机 | 源失效、计划超时或 BKIN 前后的实际输出、首次错误、剩余会话借用及最终结清 |

通用 `tools/hil/measure.py` 的 `cycles`、`irq_latency`、`service_interval`、
`loss` 与 `duration` 可承载各自的有界观测。补充保存完整的触发至 PWM 输出采用
波形与序号关联。DWT／软件时间可以定位计算与提交，实际输出采用由独立波形确认。
选定实际站点的接线与测量传输后，用匹配 Board/part 的 fixture 与同一镜像的
`resolved.json` 生成 `measure.py plan`，并在物理准入时绑定该镜像的 ELF 和
资源报告。两份本地 TOML 只选择电驱，尚无 UART smoke 路线或精确物理 fixture。
Qiming 等同芯片 Board 的 fixture 与本软件 Board 的身份不同。
1 kHz 配置值、SIL 的 20 kHz 模型、平台 `fast.toml` 的声明预算分别对应各自软件
范围。20 kHz 资格须来自同一完整 20 kHz 镜像在已审负荷、样本量和观测边界下的
实际记录。当前测量值和物理 limits 均待工装审查。
