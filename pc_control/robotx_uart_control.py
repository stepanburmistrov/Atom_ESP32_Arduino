#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RobotX UART Control Panel
=========================

Пульт для отладки робота напрямую с компьютера до подключения ESP32.

Что делает:
- открывает COM-порт Arduino Nano;
- отправляет те же UART-команды, что и ESP32 Wi-Fi модуль;
- позволяет управлять мышью по кнопкам и клавиатурой.

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
from typing import Optional, Dict, Callable, Set

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pyserial is optional until user connects
    serial = None
    list_ports = None


BAUD_DEFAULT = 9600

# Коды команд должны совпадать с Arduino Nano и ESP32 Web UI.
MOVE_COMMANDS = {
    "forward": "F",
    "back": "B",
    "left": "L",
    "right": "R",
    "stop": "S",
}

SERVO_COMMANDS = {
    "left_up_press": "Q",
    "left_up_release": "q",
    "left_down_press": "A",
    "left_down_release": "a",
    "right_up_press": "W",
    "right_up_release": "w",
    "right_down_press": "D",
    "right_down_release": "d",
}


@dataclass
class ButtonSpec:
    title: str
    press_cmd: str
    release_cmd: str
    row: int
    col: int
    style: str
    width: int = 11


class RobotXControlApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("RobotX UART Control")
        self.geometry("980x620")
        self.minsize(900, 560)

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

        style.configure("TFrame", background="#101014")
        style.configure("Panel.TFrame", background="#181820", relief="flat")
        style.configure("TLabel", background="#101014", foreground="#f2f2f2", font=("Arial", 11))
        style.configure("Title.TLabel", background="#101014", foreground="#ffffff", font=("Arial", 20, "bold"))
        style.configure("Status.TLabel", background="#101014", foreground="#f39c12", font=("Arial", 14, "bold"))
        style.configure("Hint.TLabel", background="#101014", foreground="#aaaaaa", font=("Arial", 10))
        style.configure("Robot.TButton", font=("Arial", 16, "bold"), padding=14)
        style.configure("Stop.TButton", font=("Arial", 16, "bold"), padding=14)
        style.configure("Servo.TButton", font=("Arial", 16, "bold"), padding=14)

    def _build_ui(self) -> None:
        self.configure(bg="#101014")
        root = ttk.Frame(self, padding=16)
        root.pack(fill="both", expand=True)

        header = ttk.Frame(root)
        header.pack(fill="x", pady=(0, 12))

        ttk.Label(header, text="RobotX UART Control", style="Title.TLabel").pack(side="left")
        self.status_label = ttk.Label(header, text="Последняя команда: S", style="Status.TLabel")
        self.status_label.pack(side="right")

        connection = ttk.Frame(root, style="Panel.TFrame", padding=12)
        connection.pack(fill="x", pady=(0, 14))

        ttk.Label(connection, text="COM-порт:").grid(row=0, column=0, sticky="w", padx=(0, 8))
        self.port_combo = ttk.Combobox(connection, width=32, state="readonly")
        self.port_combo.grid(row=0, column=1, sticky="w", padx=(0, 8))

        ttk.Button(connection, text="Обновить", command=self.refresh_ports).grid(row=0, column=2, padx=4)

        ttk.Label(connection, text="Скорость:").grid(row=0, column=3, sticky="w", padx=(20, 8))
        self.baud_var = tk.StringVar(value=str(BAUD_DEFAULT))
        ttk.Entry(connection, textvariable=self.baud_var, width=10).grid(row=0, column=4, sticky="w")

        self.connect_btn = ttk.Button(connection, text="Подключиться", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=5, padx=(16, 4))

        self.connection_label = ttk.Label(connection, text="Не подключено", style="Hint.TLabel")
        self.connection_label.grid(row=0, column=6, padx=(12, 0), sticky="w")

        main = ttk.Frame(root)
        main.pack(fill="both", expand=True)

        servo_frame = ttk.Frame(main, style="Panel.TFrame", padding=18)
        servo_frame.pack(side="left", fill="both", expand=True, padx=(0, 8))
        drive_frame = ttk.Frame(main, style="Panel.TFrame", padding=18)
        drive_frame.pack(side="left", fill="both", expand=True, padx=(8, 0))

        ttk.Label(servo_frame, text="Сервоприводы", style="Title.TLabel").pack(anchor="center", pady=(0, 10))
        servo_grid = ttk.Frame(servo_frame, style="Panel.TFrame")
        servo_grid.pack(expand=True)

        servo_specs = [
            ButtonSpec("Q\nЛевый вверх", "Q", "q", 0, 0, "Servo.TButton"),
            ButtonSpec("W\nПравый вверх", "W", "w", 0, 1, "Servo.TButton"),
            ButtonSpec("A\nЛевый вниз", "A", "a", 1, 0, "Servo.TButton"),
            ButtonSpec("D\nПравый вниз", "D", "d", 1, 1, "Servo.TButton"),
        ]
        for spec in servo_specs:
            self._make_hold_button(servo_grid, spec)

        ttk.Label(drive_frame, text="Движение", style="Title.TLabel").pack(anchor="center", pady=(0, 10))
        drive_grid = ttk.Frame(drive_frame, style="Panel.TFrame")
        drive_grid.pack(expand=True)

        drive_specs = [
            ButtonSpec("▲\nВперёд", "F", "S", 0, 1, "Robot.TButton"),
            ButtonSpec("◀\nВлево", "L", "S", 1, 0, "Robot.TButton"),
            ButtonSpec("STOP", "S", "S", 1, 1, "Stop.TButton"),
            ButtonSpec("▶\nВправо", "R", "S", 1, 2, "Robot.TButton"),
            ButtonSpec("▼\nНазад", "B", "S", 2, 1, "Robot.TButton"),
        ]
        for spec in drive_specs:
            self._make_hold_button(drive_grid, spec)

        bottom = ttk.Frame(root, padding=(0, 12, 0, 0))
        bottom.pack(fill="both")

        ttk.Label(
            bottom,
            text="Клавиатура: стрелки — движение, Space — стоп, Q/A — левая серва, W/D — правая серва. "
                 "Мышью кнопки работают как удерживаемые.",
            style="Hint.TLabel",
        ).pack(anchor="w")

        self.log_text = tk.Text(bottom, height=7, bg="#0b0b0f", fg="#eeeeee", insertbackground="#ffffff")
        self.log_text.pack(fill="both", expand=True, pady=(8, 0))
        self.log("Готово. Выберите COM-порт Arduino Nano и нажмите 'Подключиться'.")
        if serial is None:
            self.log("pyserial не установлен. Установите: pip install pyserial")

    def _make_hold_button(self, parent: ttk.Frame, spec: ButtonSpec) -> None:
        btn = ttk.Button(parent, text=spec.title, style=spec.style, width=spec.width)
        btn.grid(row=spec.row, column=spec.col, padx=10, pady=10, sticky="nsew")
        btn.bind("<ButtonPress-1>", lambda event, cmd=spec.press_cmd: self.send_command(cmd))
        btn.bind("<ButtonRelease-1>", lambda event, cmd=spec.release_cmd: self.send_command(cmd))

        for i in range(3):
            parent.grid_columnconfigure(i, weight=1, minsize=130)
        for i in range(3):
            parent.grid_rowconfigure(i, weight=1, minsize=90)

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
            self.connection_label.config(text=f"Подключено: {port} @ {baud}")
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
            self.connection_label.config(text="Не подключено")
            self.log("Отключено.")

    def send_command(self, cmd: str) -> None:
        cmd = cmd.strip()
        if not cmd:
            return

        self.last_cmd = cmd
        self.status_label.config(text=f"Последняя команда: {cmd}")

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

        mapping = {
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
        cmd = mapping.get(key)
        if cmd:
            self.send_command(cmd)

    def on_key_release(self, event: tk.Event) -> None:
        key = event.keysym.lower()
        self.pressed_keys.discard(key)

        release_mapping = {
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
        cmd = release_mapping.get(key)
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
