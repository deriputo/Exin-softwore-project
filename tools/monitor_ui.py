"""Swiss typography and grid for the Tk monitor; no transport or device commands."""

PAPER = "#f4f3ef"
INK = "#191919"
MUTED = "#696965"
RED = "#d52b1e"
LINE = "#d8d7d1"


def build(app):
    tk, ttk, root = app.tk, app.ttk, app.root
    root.geometry("1320x940")
    root.minsize(1100, 820)
    root.configure(background=PAPER)
    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure(".", font=("Microsoft YaHei UI", 10), background=PAPER, foreground=INK)
    style.configure("TFrame", background=PAPER)
    style.configure("TLabel", background=PAPER, foreground=INK)
    style.configure("Muted.TLabel", foreground=MUTED, font=("Microsoft YaHei UI", 9))
    style.configure("Title.TLabel", font=("Microsoft YaHei UI", 30, "bold"))
    style.configure("Section.TLabel", font=("Microsoft YaHei UI", 12, "bold"))
    style.configure("Index.TLabel", foreground=RED, font=("Segoe UI", 11, "bold"))
    style.configure("Metric.TLabel", font=("Segoe UI", 29, "bold"))
    style.configure("Red.Metric.TLabel", foreground=RED)
    style.configure("TButton", background=PAPER, foreground=INK, bordercolor=LINE,
                    lightcolor=PAPER, darkcolor=PAPER, relief="flat", borderwidth=1,
                    padding=(10, 7), font=("Microsoft YaHei UI", 9))
    style.map("TButton", background=[("disabled", PAPER), ("pressed", INK), ("active", "#e5e4de")],
              foreground=[("disabled", "#999993"), ("pressed", "white")])
    style.configure("Accent.TButton", background=RED, foreground="white", bordercolor=RED)
    style.configure("Compact.TButton", padding=(8, 2))
    style.map("Accent.TButton", background=[("disabled", "#c2b6b2"), ("active", "#b62217")],
              foreground=[("disabled", "#eeeeea"), ("!disabled", "white")])
    for name in ("TEntry", "TCombobox", "TSpinbox"):
        style.configure(name, fieldbackground="white", foreground=INK, bordercolor=LINE,
                        lightcolor="white", darkcolor="white", padding=5, arrowsize=13)
        style.map(name, fieldbackground=[("readonly", "white"), ("disabled", "#eae9e3")],
                  foreground=[("disabled", "#999993")], selectbackground=[("!disabled", INK)],
                  selectforeground=[("!disabled", "white")])
    style.configure("TSeparator", background=LINE)
    style.configure("TLabelframe", borderwidth=1, bordercolor=LINE, background=PAPER)
    style.configure("TLabelframe.Label", foreground=MUTED, font=("Microsoft YaHei UI", 9))

    container = ttk.Frame(root, padding=(24, 16, 24, 16))
    container.pack(fill="both", expand=True)
    container.columnconfigure(0, weight=1)
    container.rowconfigure(4, weight=1)
    header = ttk.Frame(container)
    header.grid(row=0, column=0, sticky="ew")
    tk.Frame(header, background=RED, width=10, height=58).pack(side="left", padx=(0, 14))
    titles = ttk.Frame(header)
    titles.pack(side="left")
    ttk.Label(titles, text="e芯 / 控制台", style="Title.TLabel").pack(anchor="w")
    ttk.Label(titles, text="STM32  /  DEVICE CONTROL & TELEMETRY", style="Muted.TLabel").pack(anchor="w")
    ttk.Label(header, text="01 — CONNECT\n02 — CONTROL\n03 — OBSERVE", style="Muted.TLabel",
              justify="right").pack(side="right")
    ttk.Separator(container).grid(row=1, column=0, sticky="ew", pady=(14, 12))

    toolbar = ttk.Frame(container)
    toolbar.grid(row=2, column=0, sticky="ew")
    top = ttk.Frame(toolbar)
    top.pack(fill="x")
    ttk.Label(top, text="01", style="Index.TLabel").pack(side="left", padx=(0, 12))
    app.mode_combo = ttk.Combobox(top, textvariable=app.transport,
                                  values=("USB串口", "蓝牙BLE"), width=10, state="readonly")
    app.mode_combo.pack(side="left")
    app.mode_combo.bind("<<ComboboxSelected>>", app._change_transport)
    app.port_combo = ttk.Combobox(top, textvariable=app.port, width=33, state="readonly")
    app.port_combo.pack(side="left", padx=8)
    app.refresh_button = ttk.Button(top, text="刷新 / 扫描", command=app._refresh_ports)
    app.refresh_button.pack(side="left")
    app.connect_button = ttk.Button(top, text="连接设备", style="Accent.TButton", command=app._connect)
    app.connect_button.pack(side="left", padx=8)
    ttk.Button(top, text="断开", command=app._disconnect).pack(side="left")
    ttk.Label(toolbar, textvariable=app.status, style="Muted.TLabel").pack(anchor="w", pady=(6, 0))
    app.toolbar = toolbar
    app.ble_box = ttk.LabelFrame(container, text="BLE / HC-04 · 请先断开手机连接", padding=8)
    summary = ttk.Frame(app.ble_box)
    summary.grid(row=0, column=0, columnspan=2, sticky="ew")
    ttk.Label(summary, text="默认服务 FFE0 / 收发 FFE1 · 如模块配置不同，请展开 UUID 设置",
              style="Muted.TLabel").pack(side="left")
    app.ble_advanced = ttk.Frame(app.ble_box)
    def toggle_uuid():
        if app.ble_advanced.winfo_manager():
            app.ble_advanced.grid_remove()
            uuid_toggle.configure(text="UUID 设置 +")
        else:
            app.ble_advanced.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(6, 0))
            uuid_toggle.configure(text="收起 UUID −")
    uuid_toggle = ttk.Button(summary, text="UUID 设置 +", style="Compact.TButton", command=toggle_uuid)
    uuid_toggle.pack(side="right")
    app.uuid_entries = []
    for i, (label, variable) in enumerate((("服务", app.ble_service), ("接收", app.ble_rx), ("发送", app.ble_tx))):
        ttk.Label(app.ble_advanced, text=label, style="Muted.TLabel").grid(row=i, column=0, padx=(0, 10))
        entry = ttk.Entry(app.ble_advanced, textvariable=variable, width=43, font=("Consolas", 10))
        entry.grid(row=i, column=1, sticky="ew", pady=2)
        app.uuid_entries.append(entry)
    app.ble_box.columnconfigure(1, weight=1)
    app.ble_box.columnconfigure(0, weight=1)
    app.ble_advanced.columnconfigure(1, weight=1)

    body = ttk.Frame(container)
    body.grid(row=4, column=0, sticky="nsew", pady=(14, 0))
    body.columnconfigure(1, weight=1)
    body.rowconfigure(0, weight=1)
    side_shell = ttk.Frame(body)
    side_shell.grid(row=0, column=0, sticky="nsw", padx=(0, 24))
    side_canvas = tk.Canvas(side_shell, width=320, background=PAPER, highlightthickness=0)
    side_scroll = ttk.Scrollbar(side_shell, orient="vertical", command=side_canvas.yview)
    side_canvas.configure(yscrollcommand=side_scroll.set)
    side_scroll.pack(side="right", fill="y")
    side_canvas.pack(side="left", fill="both", expand=True)
    sidebar = ttk.Frame(side_canvas)
    side_window = side_canvas.create_window(0, 0, window=sidebar, anchor="nw")
    sidebar.bind("<Configure>", lambda event: side_canvas.configure(scrollregion=side_canvas.bbox("all")))
    side_canvas.bind("<Configure>", lambda event: side_canvas.itemconfigure(side_window, width=event.width))
    main = ttk.Frame(body)
    main.grid(row=0, column=1, sticky="nsew")

    def section(number, title):
        frame = ttk.Frame(sidebar)
        frame.pack(fill="x", pady=(0, 14))
        ttk.Separator(frame).pack(fill="x", pady=(0, 8))
        heading = ttk.Frame(frame)
        heading.pack(fill="x", pady=(0, 8))
        ttk.Label(heading, text=number, style="Index.TLabel").pack(side="left", padx=(0, 10))
        ttk.Label(heading, text=title, style="Section.TLabel").pack(side="left")
        return frame

    clock = section("02.1", "实时时钟")
    row = ttk.Frame(clock)
    row.pack(fill="x")
    app.time_fields = app._time_inputs(row, ("23", "59", "55"))
    ttk.Button(clock, text="设置时间  /  TIME", command=app._set_time).pack(fill="x", pady=(8, 0))

    alarm = section("02.2", "闹钟")
    row = ttk.Frame(alarm)
    row.pack(fill="x")
    ttk.Combobox(row, textvariable=app.alarm_number, values=("1", "2", "3"), width=2,
                 state="readonly").pack(side="left", padx=(0, 8))
    app.alarm_fields = app._time_inputs(row, ("06", "30", "00"))
    row = ttk.Frame(alarm)
    row.pack(fill="x", pady=(8, 0))
    ttk.Button(row, text="设置并启用", command=app._set_alarm).pack(side="left", fill="x", expand=True)
    ttk.Button(row, text="停用", command=lambda: app._send("ALARM" + app.alarm_number.get() + " OFF")).pack(side="left", padx=5)
    ttk.Button(row, text="删除", command=lambda: app._send("ALARM" + app.alarm_number.get() + " DEL")).pack(side="left")

    servo = section("02.3", "舵机与姿态")
    row = ttk.Frame(servo)
    row.pack(fill="x")
    ttk.Spinbox(row, textvariable=app.servo_angle, from_=0, to=180, width=5).pack(side="left")
    ttk.Label(row, text="°", style="Muted.TLabel").pack(side="left", padx=5)
    ttk.Button(row, text="确认角度  /  SET", command=app._set_servo).pack(side="right")
    row = ttk.Frame(servo)
    row.pack(fill="x", pady=(8, 0))
    for label, command in (("HOLD ON", "SERVO HOLD ON"), ("HOLD OFF", "SERVO HOLD OFF"), ("状态 ?", "SERVO HOLD ?")):
        ttk.Button(row, text=label, command=lambda c=command: app._send(c)).pack(side="left", fill="x", expand=True, padx=(0, 4))

    queries = section("02.4", "设备查询")
    for commands in (("HELP", "IMU ?"), ("SERVO ?", "ALARM ?")):
        row = ttk.Frame(queries)
        row.pack(fill="x", pady=(0, 5))
        for command in commands:
            ttk.Button(row, text=command, command=lambda c=command: app._send(c)).pack(side="left", fill="x", expand=True, padx=(0, 5))
    ttk.Label(sidebar, text="USB  115200 / 8N1\nBLE  HC-04 / UART 9600\n\n控制指令仅在点击后发送。", style="Muted.TLabel",
              justify="left").pack(anchor="w", pady=(0, 4))

    heading = ttk.Frame(main)
    heading.pack(fill="x")
    ttk.Label(heading, text="03", style="Index.TLabel").pack(side="left", padx=(0, 10))
    ttk.Label(heading, text="姿态遥测", style="Section.TLabel").pack(side="left")
    ttk.Label(heading, text="LIVE / 最近 10 秒", style="Muted.TLabel").pack(side="right")
    metrics = ttk.Frame(main)
    metrics.pack(fill="x", pady=(10, 8))
    app.metric_values = []
    for i, label in enumerate(("目标 / TARGET", "实测 / MEASURED", "误差 / ERROR")):
        cell = ttk.Frame(metrics)
        cell.grid(row=0, column=i, sticky="ew")
        metrics.columnconfigure(i, weight=1)
        ttk.Label(cell, text=label, style="Muted.TLabel").pack(anchor="w")
        variable = tk.StringVar(value="—")
        app.metric_values.append(variable)
        ttk.Label(cell, textvariable=variable, style="Red.Metric.TLabel" if i == 1 else "Metric.TLabel").pack(anchor="w")
    ttk.Label(main, textvariable=app.latest, style="Muted.TLabel", wraplength=690).pack(anchor="w")
    app.canvas = tk.Canvas(main, height=230, background=PAPER, highlightthickness=0)
    app.canvas.pack(fill="both", expand=True, pady=(8, 8))
    app.canvas.bind("<Configure>", lambda event: app._draw_plot())
    traces = ttk.Frame(main)
    traces.pack(fill="x")
    for label, cmd in (("TRACE ON", "SERVO TRACE ON"), ("TRACE OFF", "SERVO TRACE OFF"), ("TRACE ?", "SERVO TRACE ?")):
        ttk.Button(traces, text=label, command=lambda c=cmd: app._send(c)).pack(side="left", padx=(0, 5))
    app.log_button = ttk.Button(traces, text="开始记录 CSV", command=app._toggle_log)
    app.log_button.pack(side="right")
    ttk.Separator(main).pack(fill="x", pady=(12, 10))
    command_row = ttk.Frame(main)
    command_row.pack(fill="x")
    ttk.Label(command_row, text="COMMAND", style="Muted.TLabel").pack(side="left", padx=(0, 10))
    command_entry = ttk.Entry(command_row, textvariable=app.command, font=("Consolas", 11))
    command_entry.pack(side="left", fill="x", expand=True)
    command_entry.bind("<Return>", lambda event: app._send(app.command.get()))
    ttk.Button(command_row, text="发送 ↗", style="Accent.TButton", command=lambda: app._send(app.command.get())).pack(side="left", padx=(8, 0))
    log_heading = ttk.Frame(main)
    log_heading.pack(fill="x", pady=(8, 4))
    ttk.Label(log_heading, text="通信日志 / CONSOLE", style="Muted.TLabel").pack(side="left")
    ttk.Button(log_heading, text="清空文本", command=lambda: app.console.delete("1.0", "end")).pack(side="right")
    ttk.Button(log_heading, text="清空曲线", command=app._clear_plot).pack(side="right", padx=5)
    console_box = ttk.Frame(main)
    console_box.pack(fill="x")
    app.console = tk.Text(console_box, height=4, wrap="none", font=("Consolas", 10), background="#ecebe6",
                          foreground=INK, relief="flat", borderwidth=0, padx=12, pady=10,
                          insertbackground=RED, selectbackground=INK, selectforeground="white")
    scrollbar = ttk.Scrollbar(console_box, orient="vertical", command=app.console.yview)
    app.console.configure(yscrollcommand=scrollbar.set)
    app.console.tag_configure("sent", foreground=MUTED)
    app.console.tag_configure("error", foreground=RED)
    app.console.pack(side="left", fill="both", expand=True)
    scrollbar.pack(side="right", fill="y")
    # Keep command/log controls visible; let only the chart shrink on short windows.
    packed = [(child, child.pack_info()) for child in main.winfo_children()]
    for child, options in packed:
        child.pack_forget()
    for index, (child, options) in enumerate(packed):
        child.grid(row=index, column=0, sticky="nsew" if child is app.canvas else "ew",
                   padx=options.get("padx", 0), pady=options.get("pady", 0))
    main.columnconfigure(0, weight=1)
    main.rowconfigure(3, weight=1)
    ttk.Label(container, text="e芯 / ENGINEERING CONSOLE     ·     USB 50 Hz  /  BLE 5 Hz     ·     CSV 带电脑接收时间",
              style="Muted.TLabel").grid(row=5, column=0, sticky="w", pady=(12, 0))
