# 网络更新与 USB 存储软件参考

本目录提供产品层组合：完整 staging 字节的鉴权/升级请求顺序、复位前会合、
本地文件系统与 USB MSC 的单写入者交接，以及 ST 官方 MSC 单设备/单 LUN profile。
平台公共机制继续由 Nexus 提供；分区、信任根、版本政策、USB 身份和恢复动作由产品给定。

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
```

GoogleTest 执行真实 recipe C、Nexus Flash/Block wrapper 和官方 MSC records/Class 源；
mock 仅位于产品认证/生命周期和物理 Flash/Block 边界。它们验证软件顺序、数据完整
接纳与失败责任。测试没有实际复位、验签密钥政策、MCUboot boot-copy 或 USB wire
枚举；这些不是从 API 编译或 callback 成功推导的结果。物理 HIL 为 `not_executed`。
