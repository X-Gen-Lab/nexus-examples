# UART FIFO acquisition / UART FIFO 采集

This opt-in example supplies actual bounded traffic to an already selected byte
UART. This standalone application and the optional platform-owned
instrumentation fixture build the same public-only sources for the exact H743
USART3 and U575 USART1 FIFO references. It is independent of the normal
`platform_reference` application. No private provider or vendor header is copied
into this repository, and no scheduler or worker is created.

本示例显式选入后，在已选定的 byte UART 上产生真实、有界的收发负载。独立应用入口
与平台拥有的可选 instrumentation fixture 编译同一份公开 API 源码，面向精确的
H743 USART3、U575 USART1 FIFO 参考装配；它不替代普通 `platform_reference`。
不复制私有 provider 或厂商头，也不创建 scheduler 或 worker。

## Build the plain reference / 构建普通参考

Run the following from the examples repository root after its SDK Gitlink and
`dependencies/nexus.lock.json` have been updated together and verified.
Initialize the complete pinned `external/nexus` checkout recursively and install
its locked build tools; CMake, Ninja and the ARM GCC tools must be on PATH. This
SDK must contain the maintained `h743-uart-fifo.toml` and `u575-uart-fifo.toml`
references under `cmake/package/verification/io_evolution`. The explicit source
preflight below checks the source lock and Gitlink before configuration; direct CMake compilation alone does not establish that identity.

以下命令在 examples 仓库根目录执行，前提是 SDK Gitlink 与
`dependencies/nexus.lock.json` 已原子更新并验证。递归初始化完整的固定版本
`external/nexus`，安装其锁定构建工具，并将 CMake、Ninja、ARM GCC 工具加入 PATH。
SDK 须包含 `cmake/package/verification/io_evolution` 下既有的
`h743-uart-fifo.toml` 和 `u575-uart-fifo.toml` 参考；下述显式源码预检在 configure 前
校验 source-lock 与 Gitlink。
直接执行 CMake 编译不建立该身份。

```sh
fifo_sdk="$PWD/external/nexus"
python scripts/test_repository.py --nexus-source "$fifo_sdk" \
  --report build/uart-fifo-source.json
fifo_assemblies="$fifo_sdk/cmake/package/verification/io_evolution"
cmake -S apps/uart_fifo_acquisition -B build/uart-fifo-h743 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$fifo_sdk/cmake/toolchains/arm-gcc.cmake" \
  -DNEXUS_SOURCE="$fifo_sdk" \
  -DNEXUS_ASSEMBLY_FILE="$fifo_assemblies/h743-uart-fifo.toml"
cmake --build build/uart-fifo-h743 --target uart_fifo_acquisition --parallel 1
cmake -S apps/uart_fifo_acquisition -B build/uart-fifo-u575 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$fifo_sdk/cmake/toolchains/arm-gcc.cmake" \
  -DNEXUS_SOURCE="$fifo_sdk" \
  -DNEXUS_ASSEMBLY_FILE="$fifo_assemblies/u575-uart-fifo.toml"
cmake --build build/uart-fifo-u575 --target uart_fifo_acquisition --parallel 1
```

Each separate build produces `bin/uart_fifo_acquisition.elf`, `.bin`, `.map` and
`.resources.json`. The entry compiles `main.c` and `workload.c` as strict C11
through public `nexus_add_firmware`; the SDK supplies startup, generated public
bindings, the selected provider and resource checks. It accepts only those two
existing baremetal FIFO assemblies, with no new TOML, preset, route or clock
authority. These are software references; building them establishes no physical
PCB route, electrical authorization, timing or HIL result.

每个独立构建目录生成 `bin/uart_fifo_acquisition.elf`、`.bin`、`.map` 和
`.resources.json`。入口通过公开的 `nexus_add_firmware` 将 `main.c` 与
`workload.c` 按 strict C11 编译；SDK 提供 startup、生成的公共 bindings、选定
provider 和资源检查。入口只接受上述两份既有 baremetal FIFO 装配，不新增 TOML、
preset、路线或时钟权威。这些是软件参考；构建不证明实际 PCB 路线、电气授权、
时序或 HIL 结果。

This target is the plain reference: no IRQ wrapper, cycle probe, station marker
or private include is selected. The optional instrumentation stays in the SDK's
`tests/contracts/uart_fifo_acquisition` fixture. The existing seventeen examples
presets remain unchanged; Native `fifo_acquisition_google` exercises the
workload through the existing complete-discovery/fresh-report runner. That host
contract does not execute this MCU `main` or operate a station. ARM builds do
not flash or arm a device; the station procedure below remains explicit.

此目标是普通参考，不选 IRQ wrapper、cycle probe、station marker 或私有 include。
可选 instrumentation 仍留在 SDK 的 `tests/contracts/uart_fifo_acquisition`
fixture。examples 既有十七个 presets 保持不变；Native 的
`fifo_acquisition_google` 使用既有完整发现及 fresh-report runner 测试 workload，
不执行这里的 MCU `main`，也不操作站点。ARM 构建不烧写或启动设备；仍须显式执行
下述站点流程。

## Execution and ownership / 执行与所有权

One privileged unmasked task calls each step. A step performs at most one UART
service, one bounded RX read and one TX submission. RX continues during an
ACTIVE TX loan. Each accepted 32-byte TX block has a session and sequence number;
the next payload is prepared only after acquire-observed SETTLED and result
consumption. OVERFLOW retains its valid RX prefix and diagnostic while traffic
continues. Other fatal RX errors close admission without releasing the TX loan.

单个未屏蔽中断的特权任务调用每一步。每步至多执行一次 UART service、一次有界
RX read、一次 TX submit；TX ACTIVE 借用期间仍读 RX。每个已准入的 32 字节 TX
块带 session 和 sequence；只有 acquire 观察到 SETTLED 并取得结果后，才准备下一
payload。OVERFLOW 保留有效 RX 前缀和诊断并继续采集；其他致命 RX 错误关闭准入，
但不释放 TX 借用。

Window completion, cancel and timeout never revoke accepted bytes. The owner
continues bounded drain steps; late wire completion may permit a retained session
to close. Broken monotonic order prevents further task provider calls. A closed
UART is followed by explicit Platform cleanup, with at most 64 attempts and no
clock reads after cleanup starts. Only successful cleanup publishes
`g_fifo_platform_closed=1`; BUSY/IO exhaustion keeps static state alive. This
bounds calls per step and cleanup attempts, not total close time or WCET.

窗口结束、取消和超时都不撤销借用。owner 继续执行有界排空步骤，晚到的 wire
完成可能让保留中的会话关闭。单调时间倒退后不再调用 task provider。UART 关闭后
显式清理 Platform，最多尝试 64 次，清理开始后不再读取可能已被撤回的时钟。
只有清理成功才发布 `g_fifo_platform_closed=1`；BUSY／IO 耗尽后仍保留静态存储。
这里只限制每步调用及清理尝试次数，不证明整体关闭时间或 WCET。

## Arming and capture / 启动与采集

The station halts after BSS and Platform startup, while
`g_fifo_waiting_for_arm=1`. It writes a fresh nonzero
`g_fifo_station_control.session`, then `arm=0x46514631`, prepares the complete raw
capture and resumes. It must not halt or rewrite control while measuring.
Arming has both a nominal ten-second deadline and a one-million-poll cap; neither
poll counts nor nominal target ticks establish SI time. Invalid/stalled time
retains storage instead of attempting cleanup with a broken clock assumption.
There is no printf, second UART console, semihosting or implicit GPIO marker.

站点在 BSS 和 Platform 启动之后、`g_fifo_waiting_for_arm=1` 时 halt，先写 fresh
非零 `g_fifo_station_control.session`，再写 `arm=0x46514631`，准备完整原始采集后
resume。测量中不得 halt 或改控制。等待同时受名义十秒 deadline 和一百万次轮询
上限限制；轮询数及目标名义 tick 都不证明 SI 时间。时间无效或停滞时保留存储，
不使用损坏的时钟假设清理。没有 printf、第二 UART console、semihosting 或隐式
GPIO marker。

After provider and Platform join, the station exports exact ELF/DWARF-addressed
state and full timestamped bidirectional capture. Service times are task-step
boundary samples, not atomic provider-entry timestamps. The platform parser
`tools/hil/uart_fifo.py` binds device/raw/artifact identities but does not operate
equipment or qualify hardware. Four-column IRQ CSV values remain unverified
arithmetic candidates; wire edges, raw DWT and dispatch markers cannot supply an
unobserved IRQ assertion. Use the complete existing SOC-55 acquisition/admission
contract for a real measurement. Software-reference Board fixtures remain
unadmittable and physical HIL stays `not_executed`.

provider 与 Platform 都 join 后，站点按精确 ELF／DWARF 导出状态并保留完整、带时间
的双向采集。service 时间是任务 step 边界，不能冒充原子的 provider-entry 时间。
平台 `tools/hil/uart_fifo.py` 绑定设备、原始文件和产物身份，但不操作设备或建立
硬件资格。四列 IRQ CSV 仍只是未验证的算术候选；wire edge、raw DWT、dispatch
marker 都不能替代未观测的 IRQ assertion。真实测量使用既有完整 SOC-55 采集／准入
合同。software-reference Board 仍不能通过物理准入，实板 HIL 保持 `not_executed`。

The examples SDK Gitlink and lock must be updated together with real integration
evidence. A standalone developer compile or this application's tests cannot
qualify a different SDK source or an unexecuted physical setup.

examples 的 SDK Gitlink 和 lock 必须随真实集成证据一同更新。独立开发构建或这里
的测试不能为其他 SDK 源码或未执行的实板配置建立资格。
