# 联网、持久遥测、更新与 USB 存储软件参考

本目录提供产品层组合：完整 staging 字节的鉴权/升级请求顺序、复位前会合、
本地文件系统与 USB MSC 的单写入者交接，以及 ST 官方 MSC 单设备/单 LUN profile。
平台公共机制继续由 Nexus 提供；分区、信任根、版本政策、USB 身份和恢复动作由产品给定。

## 地址、连接与可信时间

`route_reference` 通过实际 `nx_lwip_adapter_service`、DHCP 和 DNS 公共接口准备
IPv4 路线。它使用既有 NO_SYS 执行者；调用者先把真实 PHY 事实应用到 adapter，
再把同一次 link 观察传给 `example_network_route_step`。只有 DHCP 提供地址且
DNS 返回结果才发布 peer。准备与连接使用固定 deadline 和有限 progress-turn 预算。

```mermaid
flowchart LR
    L[产品 PHY 观察与 apply_link] --> A[DHCP 实际地址]
    A --> D[DNS 实际结果]
    D --> T[既有 TCP / TLS / MQTT workflow]
    T --> C[实际会话 cleanup]
    C --> J[DNS callback join]
    J --> S[DHCP stop]
```

地址变化、地址丢失或断链立即关闭路线准入。应用的 wait adapter 在推进网络时调用
`step`，错误使现有 gateway 流程退出并清理 TCP/TLS/MQTT；所有上层用户 join 后，
才调用 `route_stop`。DNS cancellation 返回 BUSY 时继续保留 query、netif 和时钟，
不能开始下一次 query 或释放其存储。随后可按产品退避政策显式启动新路线。

选用此组合时链接 `Nexus::LwIPManagedServices` 与相应 managed TCP/storage profile；
同镜像选择一种 lwIP profile。现有 `network_gateway` Native 主程序仍是其原有固定地址
参考，本片段不把它自动改成 DHCP 主程序。route 单元合同替代外部 stack 端口，
验证真实 C 组合顺序；它不建立线上 DHCP/DNS 互通或实板 PHY 资格。

`example_trusted_utc_read` 可直接作为 `nx_tls_utc_t.read`：复用 WallClock 的有效期、
漂移和 uncertainty，只有 TRUSTED 且满足产品误差上限才输出 Unix 秒。普通 SNTP
继续保持 MEASURED。信任来源、断电后的可信时间恢复和证书政策由产品提供。

## 持久遥测与实际 gateway 连接

`delivery_reference` 是明确的**单槽产品 outbox**：在已打开的 FlashDB KVDB 中使用
`telemetry` key，编码 schema、非零业务 ID、CRC32 和 pending/acked 状态。
只在成功持久化后才允许发送；pending 时新记录返回 BUSY。重启后加载原记录，
损坏、未知 schema 和不确定写入均关闭准入，按原 Flash/FAL/FlashDB 合同恢复后重开。

```mermaid
flowchart LR
    P[持久 pending + 业务 ID] --> Q[复制 wire: ID + payload]
    Q --> G[真实 gateway TCP / TLS / coreMQTT]
    G --> A[匹配当前 session / packet 的 PUBACK]
    A --> J[本地会话用户 join]
    J --> K[持久 ACK tombstone]
    K --> N[接纳更大的业务 ID]
```

`apps/network_gateway/gateway_delivery.c` 直接调用已有 `example_gateway_run` 和
`cleanup`，无需另一份 transport 实现。它在调用者存储保留完整 wire：前 8 字节是
little-endian uint64 业务 ID，其后至多 88 字节应用数据。普通 outbox 可保存 96 字节；
这个 gateway 的 96 字节 wire 限额包含 ID，超额在网络准入前返回 EXHAUSTED。

只有实际 gateway 的 delivered 观察与 local_sessions_released 同时成立才持久 ACK。
cleanup 返回 BUSY 时保持所有 wire、请求与 transport 存储，随后显式调用
`example_gateway_delivery_cleanup`。ACK 写入失败可能已产生持久作用；先恢复并重新
加载，再决定重发。远端按业务 ID 去重，接纳、PUBACK 和持久 ACK 是不同事实。
acked 记录保留高水 ID，下一次只接纳更大 ID；不删除这一身份来重用旧编号。

产品只定义一份 `nx_flashdb_operation_get`，返回本次完整调用的精确 scope；与其他
FlashDB 使用者串行，明确选择 region 和维护窗口。已有独立 `database_reference`
拥有自己的 scope 示例，组合进产品时统一该所有者。F407 片内 Flash 写擦可能停顿
取指和 IRQ，低优先级调用不提供实时控制隔离；运行时持久化须选择允许的维护窗口
或独立存储方案。CRC 检测损坏，不认证消息或存储内容。

## 凭据轮换与远端命令

`example_gateway_credentials_try` 用候选凭据执行实际 gateway 工作。只有完成认证、
业务工作和局部退出后才写 `tls_active` selector。产品预先授权候选信任根，并在
独立存储中保留旧凭据和候选凭据；函数只切换元数据，不擦除任一份材料。候选失败
保持两份材料；selector 写入失败先恢复/重读，不能假设持久选择仍未变化。具体
证书链、撤销、授权、私钥保护和回退选择由产品负责。

`example_command_gate` 只做有限命令准入：非零 session epoch、严格递增序号、
有限有效期和 payload 大小。产品先认证发送者、验证 schema，再用本地单调时间域
计算有效期；远端时间戳不能直接作为本地 deadline。准入不保留 payload 指针，也不
执行输出；应用继续通过自己的目标快照或执行器端口提交，并遵守实际操作的借用合同。

## staging 到复位准备

`update_reference` 使用实际 `nx_flash_region_program`，按顺序写入固定长度 image。
调用者先完成 staging 区域擦除与 Flash 维护准入，区域和 MCUboot metadata 分别给定。
总 deadline 在 begin 时固定，分片、鉴权、升级请求和 quiesce 不会延长它。

```mermaid
flowchart LR
    A[已准入且已擦除的 staging] --> B[顺序 Flash program]
    B --> C[完整字节数核对]
    C --> D[产品 authenticate actual staged bytes]
    D --> E[MCUboot permanent request]
    E --> F[退出生产者与会合 DMA/回调/平台]
    F --> G[READY_TO_RESET]
```

`authorize` 必须读取并认证实际 staging 字节，核对目标和版本；下载完成或 CRC 通过
不能代替它。`request_upgrade` 接入既有 `nx_mcuboot_request(PERMANENT)`，同时由产品
提供该次调用的不可变 Flash operation/areas/clock/deadline。当前 overwrite-only
profile 不提供 trial/rollback；本 recipe 不改变其引导合同。

Flash 或 trailer 请求失败可能已经修改持久介质，状态会锁存 FAILED，不盲目重放。
成功请求后，`prepare_reset` 调用真实 quiesce；BUSY/错误保留 REQUESTED 与全部资源，
只有成功才进入 READY_TO_RESET。这里不执行 reset。产品随后决定复位，并在下一次
启动记录真实认证/升级结果。中止或 timeout 不授权回收底层仍保留的硬件责任。

`boot_audit_reference` 补全更新后的结果消费：产品认证完整 staging 后，在
`request_upgrade` 中先用 `example_update_track` 保存实际候选内容 ID，再请求升级。
track 失败阻止该次请求。下一次启动由 `example_update_audit_boot` 调用产品
authenticate 端口读取、认证实际 running region，并持久保存 expected、observed
及认证结果。不同的合法 image 和认证失败都可以成为失败观察；SUCCESS 表示结果
已记录，产品仍需检查 matches。重复同一结果不再写入相同记录。

`example_update_boot_report` 为之后的有限遥测提供复制结果。这个报告不确认 MCUboot
试运行、不增加回退能力，也不代替应用自检。运行 image、数据库、slot 与 trailer 的
区域和版本身份继续由产品唯一布局确定。

## 本地文件系统与 MSC 的独占交接

`msc_handoff` 的 callback 是产品实际生命周期边界，不是“已停止”布尔标志：

```mermaid
flowchart LR
    L[LOCAL: 文件系统唯一写入者] --> U[flush/unmount + 所有文件用户 join]
    U --> S[Block media sync]
    S --> B[USB MSC start]
    B --> M[USB: MSC 唯一写入者]
    M --> J[USB stop + DCD/DMA/回调 join]
    J --> K[Block media sync]
    K --> N[local mount]
    N --> L
```

unmount 或 USB stop 失败保留原 owner。start/mount 部分失败分别保留 USB_CLEANUP
和 LOCAL_CLEANUP，必须先实际 stop/unmount 再交给另一个写入者。文件系统与主机绝不
同时修改同一块设备；descriptor、buffer 和底层 provider 活到其各自 join 完成。

## ST 官方 MSC profile

`usb_msc/CMakeLists.txt` 按需编译组织锁定的四个官方 MSC C 文件，复用现有 Nexus ST
Device Core/DCD bridge。产品私有 profile 固定一个 Device、LUN0、FS bulk IN 0x81/
OUT 0x01、64-byte packets、512-byte Block sectors 和至多 UINT32_MAX 个 sectors。

ST 的 storage callbacks 没有 context，因此这个明确的单设备产品 profile 使用一份
静态绑定；平台不会由此增加运行期 registry。Class state 大小精确等于选定一-LUN
`USBD_MSC_BOT_HandleTypeDef`，不使用 heap。另有 32-byte aligned 512-byte staging，
隔离官方 BOT buffer 与设备 buffer-alignment 要求。Read 只在完整成功后复制给 class；
Write 后在同一 deadline 下 sync，short-I/O/错误关闭后续 I/O 并保留 first fault。

装配顺序：

1. 产品停止并 unmount 本地文件系统，完成 Block sync。
2. `example_st_msc_bind` 绑定已激活的 Block、同域 clock 和有限操作预算。
3. `example_st_msc_activate` 授予 MSC 访问；此时已没有本地文件使用者。
4. 把 `example_st_msc_storage` 作为 ST bridge 的 `class_context`，传入 `USBD_MSC`
   和 `example_st_msc_descriptors`，按选定 endpoint profile 初始化/推进 bridge。
5. 返回本地使用前，先 close bridge 并证明 DCD/请求/DMA/回调已退出，class deinit
   归还 exact allocation，再 unbind；随后 sync、mount。

描述符中的 VID 0xCAFE/PID 0x4001、固定 serial 和字符串是软件夹具值；产品使用自己
的 USB 身份和唯一 serial。一个-LUN布局、Core ABI 宏和 endpoint 选择均在此私有
profile 固定，不支持同镜像再装另一份 MSC 实例或更换 vendor config 宏。

## 构建与证据范围

```sh
cmake --build build/native-debug --target network_reference_test st_msc_reference_test
ctest --test-dir build/native-debug -R 'network_reference_google|st_msc_reference_google' --output-on-failure
cmake --build build/native-debug --target network_delivery_test network_route_test network_gateway_delivery_test
ctest --test-dir build/native-debug -R 'network_delivery_google|network_route_google|network_gateway_delivery_google' --output-on-failure
```

GoogleTest 执行真实 recipe C、Nexus Flash/Block wrapper 和官方 MSC records/Class 源；
mock 仅位于产品认证/生命周期和物理 Flash/Block 边界。它们验证软件顺序、数据完整
接纳与失败责任。测试没有实际复位、验签密钥政策、MCUboot boot-copy 或 USB wire
枚举；这些不是从 API 编译或 callback 成功推导的结果。物理 HIL 为 `not_executed`。

新增 delivery 合同执行真实 recipe、FlashDB 上游、FAL 与 Flash 包装，模型位于物理
介质边界。gateway delivery 合同替代既有 gateway 与数据库依赖，验证业务 ID、ACK
持久化顺序和退出责任；现有 gateway parser/peer 合同分别验证其真实协议实现。
