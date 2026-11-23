# Stream recovery / 流恢复

本目录有可运行的 Native 流示例，以及受独立应用合同执行的有限接收策略。
`main.c`/`stream_application.c` 通过显式模型注入验证不可变借用、容量 LOSS、BUSY
stop、归还和 restart；`receive_application.c` 保留有限窗口、失败 release 重试
及关闭策略。两条路径使用真实公开接口，不声称已经运行 MCU DMA。

The Native application exercises immutable loans, capacity loss, BUSY stop,
release and restart. The finite receive fragment retains exact loan tokens on
failed release and keeps storage through failed drain. These are distinct
application contracts, not duplicate implementations.

诊断仅解释已复制事实，不推断服务频率，也不授权释放数据。

Diagnostics neither infer timing nor authorize reclamation. See the [application guide](../../docs/examples.md)
and [validation boundary](../../docs/validation.md). Physical HIL is `not_executed`.

## ADC：有限窗口内的连续采集 / ADC finite-window acquisition

真实应用实现为 [adc_acquisition.c](adc_acquisition.c)，完整调用合同见
[adc_acquisition.h](adc_acquisition.h)。它接收已经由 Platform 准备的公共 ADC
接口和调用者初始化的 FREE stream，不复制 provider，不绑定默认 ADC 或 PCB。
完整源码中的
[F407 连续 ADC 配置](../../external/nexus/tests/contracts/io_evolution_assembly/f407-adc-stream-profile.toml)
可供核对资源；它是软件参考，不授权实板引脚。

1. 为 stream 提供精确 slot 数量和独立数据区；每个 block 采用完整 scan 的原生
   `uint16_t` 计数序列。请求的完整 scan 频率、block 数量、单次服务预算和 DMA
   可达性必须满足所选 provider。
2. 在同一任务调用 `example_adc_acquisition_start`，明确 trigger 频率、有效 block
   目标、就绪的同时间域 `now` 和有限 duration。溢出拒绝，不转为无限等待。
3. 由已有主循环或任务每周期调用一次 `example_adc_acquisition_step`。一次至多
   service 一次并处理一个 block；复制最后一个原始计数，统计完整 scan，再归还
   原 token。EMPTY 不复用旧样本，LOSS、畸形 scan 或错误 block 请求停止。
4. 达到目标、deadline 或显式 cancel 后继续 step。source stop 的 BUSY/错误、
   失败 release 均保留 active、stream 和精确 token。只有 source 已 join 且所有
   READY/BORROWED/FILLING 状态已退出，active 才变为 false。
5. 另外 join 应用自行安装的观察者、waiter 和外部调用者，再停止允许退出的
   Platform。模块自己不安装通知或观察者。复用 stream 前由调用者确认全部退出，
   再显式重新初始化，不能在失败停机时清零状态。

The real application C receives an already prepared public ADC face and a
caller-initialized FREE stream. Start supplies the complete-scan rate, valid block
target, ready same-domain time and representable finite duration. Each existing
executor cycle services once and consumes at most one whole native `uint16_t`
scan block, copies its last raw count and releases the exact token. This is not
voltage calibration or a physical sample timestamp. Empty input never reuses a
previous sample; loss, malformed storage and errors request shutdown.

Keep stepping after cancellation, timeout or any error while `active` is true.
Busy/failed source withdrawal and failed release retain their original storage.
Only provider source join plus a fully free stream ends this application's
borrow. Separately join externally installed observers/waiters before reclaiming
their storage or stopping Platform. The module starts no worker and performs no
automatic reset, reinitialization or physical qualification.

## 音频：TX、RX 与 duplex / Audio TX, RX and duplex

[audio_acquisition.c](audio_acquisition.c) 与
[audio_acquisition.h](audio_acquisition.h) 复用一个公共 audio owner、一个原始
blocks request 和独立方向的 stream。使用
`example_audio_acquisition_start` 选择 RX；使用 `start_streams` 显式提供可选
`example_audio_playback_t` 和 RX stream，选择 TX-only 或 duplex。
支持范围仍由所选 I2S/SAI 配置决定，例如完整源码的
[F407 I2S blocks](../../external/nexus/tests/contracts/f407_capture_audio/ve-i2s2-duplex-blocks.toml)
与 [SAI 软件参考](../../external/nexus/tests/contracts/stm32f4_sai/README.md)。

- start 查询真实公共 layout，按 active slots 与 container 位宽计算完整存储帧。
  block_bytes 必须为完整帧倍数；I2S 高半字在前与 SAI 原生容器的格式保持原样。
  first_container 只复制原始存储字节，不做 PCM 转换。
- RX 提供 FREE pool。TX 额外状态仅在显式选择时由调用者提供；全部初始 TX slot
  必须已有完整 READY pattern，且与 RX/控制存储独立。例子明确复播各个已归还
  block 的原 pattern，不自动补静音或选任意产品音源。
- 同一任务每周期调用一次 `example_audio_acquisition_step`：至多 service 一次、
  复播一个已 detach 的 TX block、消费一个 RX block。TX 目标只统计已经由 provider
  归还的存储，不把队列中的 READY block 当作发送成功。duplex 由 RX block 目标
  结束窗口，TX 的观察计数独立。
- 完成目标、deadline 或 cancel 都进入同一显式停止路径。先证明唯一 owner 已
  停止且 request 为 SETTLED，再分别归还 RX loan、丢弃尚未播放的 TX READY block。
  TX/RX 各自的 release 失败保留各自 token；任何一边未退出都保持 active。
- active 为 false 只证明本例已持有的 request/stream 借用退出。外部通知、观察者和
  调用者仍需另行 join。外部 slave 时钟停止可能阻止实际 serializer drain，deadline
  不授权回收。存储块计数不承诺精确线上帧数或音频无缝性。

Audio uses one prepared public owner and one original blocks request. RX-only is
the short `start` entry; `start_streams` selects optional caller-owned TX state
and RX stream. Both directions share the selected layout and whole-frame block
size, while keeping independent stream and loan identities. RX starts FREE; TX
starts with every slot READY and containing the caller's explicit patterns. This
reference replays detached patterns, never synthesizes silence or converts PCM.

Each serialized step has bounded call counts: one provider service, at most one
TX replay and one RX block. TX-only ends after the target number of observed
returned blocks; duplex ends at the RX target with independent TX observation.
Queued TX bytes never count as played. Cancellation/deadline/errors preserve all
storage until the original request is acquire-observed SETTLED and both streams
fully join. After owner join, each step discards at most one unplayed TX block;
failed TX and RX releases retain separate exact tokens. External clocks and
observers remain the caller's responsibility. These are storage observations,
not measured wire timing, a no-gap audio guarantee or hardware acceptance.

## 软件验证 / Software verification

已维护的 [GoogleTest/GoogleMock 应用合同](../../tests/apps/stream_recovery/block_acquisition_test.cpp)
执行真实应用 C、公共 dispatch 与 stream，仅物理端口和 release 故障边界受控。
覆盖完整 scan/frame、单步预算、拒绝、有限时间溢出、取消、BUSY/隔离、双方向退出、
release 重试及已知控制/slot/完整 payload reservation 别名。冷态检查拒绝绕回范围，
不会假装知道 opaque provider context 的完整尺寸；该部分由真实 provider 准入和
调用者存储合同继续保证。地址、容量和对象身份在整个借用期间不得被外部改写。

The maintained application contracts execute the actual C with real public
dispatch and streams. Only hardware ports and the release-failure boundary are
controlled. They establish software behavior, including known reservation
independence and separate duplex drain, not MCU DMA execution or physical timing.
Cold checks do not infer the full opaque provider-context extent. Keep genuine
initialized objects and stable graph identities for the complete loan lifetime.

完整 Native 构建后可单独执行此模块；它不代替完整 local CI 或 ARM 链接：
After configuring/building Native, run the whole module below; this focused
scope does not replace full local CI or ARM linking:

```sh
ctest --test-dir build/native-debug -R '^block_acquisition_google$' \
  --output-on-failure --no-tests=error
```

## 独立 F407 ARM 消费 / Standalone F407 ARM consumption

同一目录的 leaf CMake 通过公开 `find_package`、`nexus_add_platform` 和
`nexus_add_firmware` 链接真实应用实现、typed factory 和选定 F4 provider。
硬件选择只有各自 TOML；两个 target 均为 `EXCLUDE_FROM_ALL`，必须显式指定。
每个构建目录只使用一份装配，错误 target 不会获得另一份隐式硬件配置。

| 装配 | target / main | 明确的调用者存储 |
| --- | --- | --- |
| [adc-window.toml](adc-window.toml) | `adc_window` / [adc_window_main.c](adc_window_main.c) | 8 × 400 B、完整双通道原始 scan 的主 SRAM banks |
| [audio-window.toml](audio-window.toml) | `audio_window` / [audio_window_main.c](audio_window_main.c) | 每方向 3 × 256 B、16-bit stereo，显式 SRAM ramp pattern |

[software-board/board.json](software-board/board.json) 是精确 STM32F407ZGT6 硅片
软件参考，仅引用已维护 ADC1 和 I2S3 路线。HSE8 是软件假定，未知 PCB、实装
codec、模拟输入、电气与实测时序保持未知；不能用于声称启明或其他实板已有接线。
所有 request、stream、slot、payload 与可能失败的未发布 fill descriptor 都是
独立静态存储，DMA bank 使用 `_Alignas(4)` 非 const 主 SRAM，不借用 Flash pattern。

main 给出正常窗口与取消后的退出推进；每阶段最多一百万次有界 step 调用，
不是物理 WCET 或成功停机承诺。预算结束仍 active 时保留 Platform 和所有借用，
返回错误，不清零、不调用 Platform stop。只有实际退出后才尝试冷态 stop；
冷态失败也保留静态状态。这些消费者只做编译／链接，不自动烧录或操作设备。

The leaf project links the actual application C and selected F4 implementation
through public CMake APIs. Each build directory has one authored TOML. Both image
targets are excluded from the default build, so select the matching target
explicitly. The shared Board is a precise F407ZG silicon software reference with
unknown PCB/electrical facts and an unmeasured HSE8 assumption.

Requests, streams, slots, payloads and any unpublished fill token have static
lifetimes. ADC and both audio DMA banks use aligned mutable main SRAM. Audio
explicitly prepares a 16-bit stereo ramp after checking the public layout; no
Flash-backed DMA pattern is inferred. The main progresses the normal window and
then cancellation under separate finite step-count budgets. Failed exit retains
all static storage and Platform; it never treats budget expiry as withdrawal.
Only complete application join permits the attempted cold Platform stop.
Linking these images does not run hardware or establish measured timing.

从完整例库根目录执行，先准备对应 SDK 的 ARM 工具链与选定依赖。
下面把 `case_name` 改为 `audio` 可构建另一条路径；两次使用不同 build 目录。
From the complete example checkout, prepare the selected SDK dependencies and
ARM toolchain first. Set `case_name=audio` for the other independent build:

```sh
case_name=adc
cmake -S apps/stream_recovery -B "build/${case_name}-window-arm" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/external/nexus/cmake/toolchains/arm-gcc.cmake" \
  -DNexus_DIR="$PWD/external/nexus/cmake" \
  -DNEXUS_ASSEMBLY_FILE="$PWD/apps/stream_recovery/${case_name}-window.toml"
cmake --build "build/${case_name}-window-arm" \
  --target "${case_name}_window" --parallel 2
```

产物在所选 build 的 `bin/`：ELF、BIN、map 与 resources JSON。
预算和 section 检查分别绑定实际装配及源码；物理 HIL 仍为 `not_executed`。
Outputs are ELF/BIN/map/resources in the chosen build's `bin/`; their checks
establish the actual linked assembly and storage layout, with physical HIL still
`not_executed`.
