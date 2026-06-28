#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RobotX UART Control Panel
=========================

Красивый пульт для отладки робота напрямую с компьютера до подключения ESP32.

Что делает:
- открывает COM-порт Arduino Nano;
- отправляет те же UART-команды, что и ESP32 Wi-Fi модуль;
- позволяет управлять мышью по большим экранным кнопкам;
- позволяет управлять клавиатурой.

Протокол:
F/B/L/R/S — движение;
Q/q/A/a — левая серва;
W/w/D/d — правая серва.
Каждая команда отправляется как ASCII-символ + '\n'.
"""

from __future__ import annotations

import sys
import time
import tkinter as tk
from tkinter import ttk, messagebox
from dataclasses import dataclass
from typing import Dict, Optional, Set

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None


BAUD_DEFAULT = 9600
APP_VERSION = "2.1"

# Коды команд должны совпадать с Arduino Nano и ESP32 Web UI.
KEY_PRESS_COMMANDS = {
    "up": "F",
    "down": "B",
    "left": "L",
    "right": "R",
    "space": "S",
    "q": "Q",
    "a": "A",
    "w": "W",
    "d": "D",
}

KEY_RELEASE_COMMANDS = {
    "up": "S",
    "down": "S",
    "left": "S",
    "right": "S",
    "space": "S",
    "q": "q",
    "a": "a",
    "w": "w",
    "d": "d",
}


@dataclass(frozen=True)
class HoldButtonSpec:
    code: str
    title: str
    subtitle: str
    press_cmd: str
    release_cmd: str
    row: int
    col: int
    kind: str
    width: int = 178
    height: int = 104


class HoldButton(tk.Frame):
    """Большая кнопка с аккуратной вёрсткой текста.

    Обычный ttk.Button на Windows часто обрезает многострочный русский текст.
    Поэтому здесь используется Frame + Label с фиксированной высотой и шириной:
    текст не обрезается, а вся кнопка работает как удерживаемая.
    """

    COLORS = {
        "servo": {"bg": "#f39c12", "active": "#d88a0f", "shadow": "#8a5600", "fg": "#ffffff"},
        "drive": {"bg": "#2d8cff", "active": "#2472d0", "shadow": "#154f97", "fg": "#ffffff"},
        "stop": {"bg": "#e74c3c", "active": "#c0392b", "shadow": "#7f1d17", "fg": "#ffffff"},
    }

    def __init__(self, master: tk.Misc, app: "RobotXControlApp", spec: HoldButtonSpec) -> None:
        self.spec = spec
        self.app = app
        self.palette = self.COLORS[spec.kind]
        super().__init__(master, width=spec.width, height=spec.height + 7, bg=self.palette["shadow"])
        self.grid_propagate(False)
        self.pack_propagate(False)

        self.face = tk.Frame(self, bg=self.palette["bg"], width=spec.width, height=spec.height)
        self.face.place(x=0, y=0, width=spec.width, height=spec.height)
        self.face.pack_propagate(False)

        # Для STOP не нужен отдельный крупный код сверху.
        if spec.code == "STOP":
            code_text = "STOP"
            title_text = spec.title
            code_font = ("Segoe UI", 20, "bold")
            title_font = ("Segoe UI", 10, "bold")
        else:
            code_text = spec.code
            title_text = spec.title
            code_font = ("Segoe UI", 21, "bold")
            title_font = ("Segoe UI", 11, "bold")

        self.code_label = tk.Label(
            self.face,
            text=code_text,
            bg=self.palette["bg"],
            fg=self.palette["fg"],
            font=code_font,
        )
        self.code_label.place(relx=0.5, rely=0.23, anchor="center")

        self.title_label = tk.Label(
            self.face,
            text=title_text,
            bg=self.palette["bg"],
            fg=self.palette["fg"],
            font=title_font,
            justify="center",
            wraplength=spec.width - 14,
        )
        self.title_label.place(relx=0.5, rely=0.56, anchor="center")

        self.subtitle_label = tk.Label(
            self.face,
            text=spec.subtitle,
            bg=self.palette["bg"],
            fg="#fff7df" if spec.kind == "servo" else "#dcecff",
            font=("Segoe UI", 8, "bold"),
            justify="center",
            wraplength=spec.width - 14,
        )
        self.subtitle_label.place(relx=0.5, rely=0.82, anchor="center")

        self._bind_all_children("<ButtonPress-1>", self._on_press)
        self._bind_all_children("<ButtonRelease-1>", self._on_release)
        self._bind_all_children("<Leave>", self._on_leave)

    def _bind_all_children(self, sequence: str, callback) -> None:
        widgets = [self, self.face, self.code_label, self.title_label, self.subtitle_label]
        for widget in widgets:
            widget.bind(sequence, callback)

    def _on_press(self, event: tk.Event) -> None:
        self.face.configure(bg=self.palette["active"])
        for widget in (self.code_label, self.title_label, self.subtitle_label):
            widget.configure(bg=self.palette["active"])
        self.face.place_configure(y=5)
        self.app.send_command(self.spec.press_cmd)

    def _on_release(self, event: tk.Event) -> None:
        self._release_visual()
        self.app.send_command(self.spec.release_cmd)

    def _on_leave(self, event: tk.Event) -> None:
        # Если кнопку удерживали мышью и увели курсор — лучше остановить действие.
        if event.state & 0x0100:
            self._release_visual()
            self.app.send_command(self.spec.release_cmd)

    def _release_visual(self) -> None:
        self.face.configure(bg=self.palette["bg"])
        for widget in (self.code_label, self.title_label, self.subtitle_label):
            widget.configure(bg=self.palette["bg"])
        self.face.place_configure(y=0)


class RobotXControlApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("RobotX UART Control")
        self.geometry("1080x680")
        self.minsize(1000, 640)
        self.configure(bg="#0f1016")

        self.serial_port: Optional["serial.Serial"] = None
        self.last_cmd = "S"
        self.pressed_keys: Set[str] = set()
        self.available_ports: Dict[str, str] = {}

        self._build_styles()
        self._build_ui()
        self._bind_keyboard()
        self.refresh_ports()

        self.protocol("WM_DELETE_WINDOW", self.on_close)

    # ---------- UI ----------

    def _build_styles(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        style.configure("Dark.TFrame", background="#0f1016")
        style.configure("Panel.TFrame", background="#181923")
        style.configure("Card.TFrame", background="#12131a")
        style.configure("TLabel", background="#0f1016", foreground="#f5f5f5", font=("Segoe UI", 11))
        style.configure("Title.TLabel", background="#0f1016", foreground="#ffffff", font=("Segoe UI", 22, "bold"))
        style.configure("PanelTitle.TLabel", background="#181923", foreground="#ffffff", font=("Segoe UI", 20, "bold"))
        style.configure("Hint.TLabel", background="#0f1016", foreground="#b7bcc8", font=("Segoe UI", 10))
        style.configure("Status.TLabel", background="#0f1016", foreground="#f39c12", font=("Segoe UI", 18, "bold"))
        style.configure("Small.TButton", font=("Segoe UI", 10), padding=(10, 6))
        style.configure("Connect.TButton", font=("Segoe UI", 10, "bold"), padding=(12, 6))

    def _build_ui(self) -> None:
        root = ttk.Frame(self, style="Dark.TFrame", padding=18)
        root.pack(fill="both", expand=True)

        header = ttk.Frame(root, style="Dark.TFrame")
        header.pack(fill="x", pady=(0, 14))

        title_block = ttk.Frame(header, style="Dark.TFrame")
        title_block.pack(side="left", fill="x", expand=True)
        ttk.Label(title_block, text="RobotX UART Control", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            title_block,
            text="Отладочный пульт: компьютер → USB-UART → Arduino Nano → робот",
            style="Hint.TLabel",
        ).pack(anchor="w", pady=(2, 0))

        self.status_label = ttk.Label(header, text="Команда: S", style="Status.TLabel")
        self.status_label.pack(side="right", padx=(16, 0))

        connection = ttk.Frame(root, style="Panel.TFrame", padding=14)
        connection.pack(fill="x", pady=(0, 14))

        for col in range(8):
            connection.grid_columnconfigure(col, weight=0)
        connection.grid_columnconfigure(1, weight=1)
        connection.grid_columnconfigure(7, weight=1)

        tk.Label(connection, text="COM-порт", bg="#181923", fg="#ffffff", font=("Segoe UI", 10, "bold")).grid(
            row=0, column=0, sticky="w", padx=(0, 8)
        )
        self.port_combo = ttk.Combobox(connection, width=42, state="readonly")
        self.port_combo.grid(row=0, column=1, sticky="ew", padx=(0, 8))

        ttk.Button(connection, text="Обновить", style="Small.TButton", command=self.refresh_ports).grid(row=0, column=2, padx=(0, 14))

        tk.Label(connection, text="Скорость", bg="#181923", fg="#ffffff", font=("Segoe UI", 10, "bold")).grid(
            row=0, column=3, sticky="w", padx=(0, 8)
        )
        self.baud_var = tk.StringVar(value=str(BAUD_DEFAULT))
        ttk.Entry(connection, textvariable=self.baud_var, width=9).grid(row=0, column=4, sticky="w", padx=(0, 14))

        self.connect_btn = ttk.Button(connection, text="Подключиться", style="Connect.TButton", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=5, sticky="w", padx=(0, 12))

        self.connection_badge = tk.Label(
            connection,
            text="● Не подключено",
            bg="#20212c",
            fg="#ff7675",
            font=("Segoe UI", 10, "bold"),
            padx=12,
            pady=7,
        )
        self.connection_badge.grid(row=0, column=6, sticky="w")

        main = ttk.Frame(root, style="Dark.TFrame")
        main.pack(fill="both", expand=True)
        main.grid_columnconfigure(0, weight=1, uniform="main")
        main.grid_columnconfigure(1, weight=1, uniform="main")
        main.grid_rowconfigure(0, weight=1)

        servo_panel = ttk.Frame(main, style="Panel.TFrame", padding=20)
        servo_panel.grid(row=0, column=0, sticky="nsew", padx=(0, 9))
        drive_panel = ttk.Frame(main, style="Panel.TFrame", padding=20)
        drive_panel.grid(row=0, column=1, sticky="nsew", padx=(9, 0))

        self._build_servo_panel(servo_panel)
        self._build_drive_panel(drive_panel)

        bottom = ttk.Frame(root, style="Dark.TFrame", padding=(0, 12, 0, 0))
        bottom.pack(fill="x")

        ttk.Label(
            bottom,
            text="Клавиатура: стрелки — движение • Space — стоп • Q/A — левая серва • W/D — правая серва. "
                 "Кнопки серв удерживаются, отпускание отправляет стоп-команду.",
            style="Hint.TLabel",
        ).pack(anchor="w", pady=(0, 8))

        log_wrap = tk.Frame(bottom, bg="#2c2f3a", padx=1, pady=1)
        log_wrap.pack(fill="x")
        self.log_text = tk.Text(
            log_wrap,
            height=4,
            bg="#090a0f",
            fg="#e8e8e8",
            insertbackground="#ffffff",
            relief="flat",
            font=("Consolas", 10),
            padx=10,
            pady=8,
        )
        self.log_text.pack(fill="x")

        self.log("Готово. Выберите COM-порт Arduino Nano и нажмите 'Подключиться'.")
        if serial is None:
            self.log("pyserial не установлен. Установите: pip install pyserial")

    def _build_servo_panel(self, panel: ttk.Frame) -> None:
        ttk.Label(panel, text="Сервоприводы", style="PanelTitle.TLabel").pack(anchor="center", pady=(0, 4))
        tk.Label(
            panel,
            text="Q/A — левая серва, W/D — правая серва",
            bg="#181923",
            fg="#b7bcc8",
            font=("Segoe UI", 10),
        ).pack(anchor="center", pady=(0, 18))

        grid = tk.Frame(panel, bg="#181923")
        grid.pack(expand=True)

        specs = [
            HoldButtonSpec("Q", "Левая серва вверх", "удерживать", "Q", "q", 0, 0, "servo", 190, 108),
            HoldButtonSpec("W", "Правая серва вверх", "удерживать", "W", "w", 0, 1, "servo", 190, 108),
            HoldButtonSpec("A", "Левая серва вниз", "удерживать", "A", "a", 1, 0, "servo", 190, 108),
            HoldButtonSpec("D", "Правая серва вниз", "удерживать", "D", "d", 1, 1, "servo", 190, 108),
        ]
        for spec in specs:
            btn = HoldButton(grid, self, spec)
            btn.grid(row=spec.row, column=spec.col, padx=14, pady=12)

    def _build_drive_panel(self, panel: ttk.Frame) -> None:
        ttk.Label(panel, text="Движение", style="PanelTitle.TLabel").pack(anchor="center", pady=(0, 4))
        tk.Label(
            panel,
            text="Стрелки на клавиатуре или кнопки мышью",
            bg="#181923",
            fg="#b7bcc8",
            font=("Segoe UI", 10),
        ).pack(anchor="center", pady=(0, 18))

        grid = tk.Frame(panel, bg="#181923")
        grid.pack(expand=True)

        specs = [
            HoldButtonSpec("▲", "Вперёд", "F", "F", "S", 0, 1, "drive", 150, 94),
            HoldButtonSpec("◀", "Влево", "L", "L", "S", 1, 0, "drive", 150, 94),
            HoldButtonSpec("STOP", "Остановить", "Space / S", "S", "S", 1, 1, "stop", 150, 94),
            HoldButtonSpec("▶", "Вправо", "R", "R", "S", 1, 2, "drive", 150, 94),
            HoldButtonSpec("▼", "Назад", "B", "B", "S", 2, 1, "drive", 150, 94),
        ]
        for spec in specs:
            btn = HoldButton(grid, self, spec)
            btn.grid(row=spec.row, column=spec.col, padx=9, pady=8)

    # ---------- serial ----------

    def refresh_ports(self) -> None:
        self.available_ports.clear()
        values = []

        if list_ports is not None:
            for port in list_ports.comports():
                label = f"{port.device} — {port.description}"
                self.available_ports[label] = port.device
                values.append(label)

        self.port_combo["values"] = values
        if values and not self.port_combo.get():
            self.port_combo.current(0)
        if not values:
            self.log("COM-порты не найдены. Подключите Arduino Nano или проверьте драйвер CH340/FTDI.")

    def toggle_connection(self) -> None:
        if self.serial_port and self.serial_port.is_open:
            self.disconnect()
        else:
            self.connect()

    def connect(self) -> None:
        if serial is None:
            messagebox.showerror("pyserial не установлен", "Выполните: pip install pyserial")
            return

        selected = self.port_combo.get()
        port = self.available_ports.get(selected)
        if not port:
            messagebox.showwarning("COM-порт", "Выберите COM-порт Arduino Nano.")
            return

        try:
            baud = int(self.baud_var.get().strip())
        except ValueError:
            messagebox.showwarning("Скорость", "Скорость должна быть числом, например 9600.")
            return

        try:
            self.serial_port = serial.Serial(port=port, baudrate=baud, timeout=0.05, write_timeout=0.5)
            time.sleep(1.8)  # Arduino Nano может перезагрузиться при открытии порта
            self.connect_btn.config(text="Отключиться")
            self.connection_badge.config(text=f"● Подключено: {port} @ {baud}", fg="#55efc4")
            self.log(f"Подключено к {port} на {baud} бод.")
            self.send_command("S")
        except Exception as exc:
            self.serial_port = None
            messagebox.showerror("Ошибка подключения", str(exc))
            self.log(f"Ошибка подключения: {exc}")

    def disconnect(self) -> None:
        try:
            if self.serial_port and self.serial_port.is_open:
                self.send_command("S")
                self.serial_port.close()
        finally:
            self.serial_port = None
            self.connect_btn.config(text="Подключиться")
            self.connection_badge.config(text="● Не подключено", fg="#ff7675")
            self.log("Отключено.")

    def send_command(self, cmd: str) -> None:
        cmd = cmd.strip()
        if not cmd:
            return

        self.last_cmd = cmd
        self.status_label.config(text=f"Команда: {cmd}")

        line = (cmd + "\n").encode("ascii")
        if self.serial_port and self.serial_port.is_open:
            try:
                self.serial_port.write(line)
                self.serial_port.flush()
                self.log(f"TX: {cmd}")
            except Exception as exc:
                self.log(f"Ошибка отправки {cmd}: {exc}")
                messagebox.showerror("UART", str(exc))
        else:
            self.log(f"DEMO TX: {cmd}  (порт не подключен)")

    # ---------- keyboard ----------

    def _bind_keyboard(self) -> None:
        self.bind("<KeyPress>", self.on_key_press)
        self.bind("<KeyRelease>", self.on_key_release)

    def on_key_press(self, event: tk.Event) -> None:
        key = event.keysym.lower()
        if key in self.pressed_keys:
            return
        self.pressed_keys.add(key)

        cmd = KEY_PRESS_COMMANDS.get(key)
        if cmd:
            self.send_command(cmd)

    def on_key_release(self, event: tk.Event) -> None:
        key = event.keysym.lower()
        self.pressed_keys.discard(key)

        cmd = KEY_RELEASE_COMMANDS.get(key)
        if cmd:
            self.send_command(cmd)

    # ---------- helpers ----------

    def log(self, message: str) -> None:
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{timestamp}] {message}\n")
        self.log_text.see("end")

    def on_close(self) -> None:
        self.disconnect()
        self.destroy()


def main() -> int:
    app = RobotXControlApp()
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
