# 电脑采集端

`telemetry_agent.py` 是 macOS／Windows 共用的局域网采集核心。协议版本为 `1`，按固定间隔向设备的 `smart_speaker.product.computer` 投影发送 CPU 和指标状态；首次部署先在网页或设备端用相同 `computer_id` 调用 `product.computer` 的 `pair` 操作，然后运行：

```sh
python tools/pc-agent/telemetry_agent.py \
  --device http://192.168.1.20 \
  --computer-id my-mac --interval 5
```

如果安装了 `psutil`，两平台都用短时 `cpu_percent` 采样；Windows 没有该库时回退到 PowerShell 的 `Win32_Processor.LoadPercentage`。权限或采集失败返回对应状态，不把负载平均值冒充使用率。GPU、温度和风扇在没有明确权限和驱动来源时保持 `unavailable`，不会填 0。退出采集端后设备状态由 `disconnect` 或样本超时显示为 `offline`／`stale`。

当前交付的是无 GUI 的协议核心，便于在两平台分别验证；菜单栏／托盘包装、签名和开机启动仍属于 A-PC 外部端交付项，不能由固件构建通过替代。按 Ctrl-C 退出会发送 `disconnect`；网络中断时由设备样本超时转为 `stale`。

## 菜单栏／托盘应用

用 Python 3.11 建虚拟环境并安装对应平台依赖：

```sh
python -m venv .venv
. .venv/bin/activate                 # Windows: .venv\\Scripts\\activate
python -m pip install -r tools/pc-agent/requirements-desktop.txt
python tools/pc-agent/desktop_agent.py --device http://192.168.1.20 --computer-id my-computer
```

先在网页“电脑配对”输入同一个 ID 并配对，再启动采集端。macOS 由 `rumps` 提供菜单栏入口，Windows 由 `pystray` 提供托盘入口；连接、断开和打开设备网页由菜单控制。GUI 线程不执行指标采样或 HTTP，请求超时为 3 秒。退出会停止线程并按配对 ID 发送断连；设备只接受当前配对 ID。应用不自动安装后台启动，不请求管理员权限，不含云端凭据。该入口已完成源码及模拟验证，macOS 菜单栏实际运行与 Windows 托盘运行分别仍待对应操作系统验收。
