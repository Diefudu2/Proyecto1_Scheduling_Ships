# ============================================================
# Archivo: gui.py
# Proyecto: Scheduling Ships ESP32-C6 / FreeRTOS
# Rol: Interfaz gráfica Tkinter de un solo hilo para controlar el firmware, visualizar canal/colas e interrupciones.
#
# Documentación interna:
# - Este archivo pertenece a la herramienta de escritorio del proyecto.
# - Mantener lectura serial no bloqueante para no congelar la interfaz.
# - Los comandos enviados deben coincidir con serial_protocol.c.
# ============================================================

# gui.py
# GUI rápida de un solo hilo para Scheduling Ships ESP32-C6.
#
# Esta versión usa SNAPSHOT para refresco rápido e incluye botones de interrupción.
#
# Teclas:
#   w        cerrar elegantemente
#   espacio  STEP
#   n/f/p    barcos izquierda
#   N/F/P    barcos derecha

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
import re
import sys

CURRENT_DIR = Path(__file__).resolve().parent
if str(CURRENT_DIR) not in sys.path:
    sys.path.insert(0, str(CURRENT_DIR))

from serial_client import SerialClient


VALID_SHIP_TOKENS = {"n", "f", "p", "N", "F", "P"}


class SchedulingShipsGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Scheduling Ships - ESP32-C6")
        self.root.geometry("1240x800")
        self.root.minsize(1100, 720)

        self.client = SerialClient()
        self.connected = False

        self.fast_snapshot_mode = tk.BooleanVar(value=True)
        self.auto_poll_enabled = tk.BooleanVar(value=True)
        self.auto_poll_ms_var = tk.StringVar(value="120")
        self._auto_poll_elapsed_ms = 0
        self._legacy_phase = 0

        self.status_var = tk.StringVar(value="Desconectado")
        self.port_var = tk.StringVar(value="/dev/ttyACM0")
        self.baud_var = tk.StringVar(value="115200")

        self.sched_var = tk.StringVar(value="PRIORITY")
        self.flow_var = tk.StringVar(value="LETRERO")
        self.quantum_var = tk.StringVar(value="500")
        self.letrero_var = tk.StringVar(value="1000")
        self.w_var = tk.StringVar(value="1")
        self.canal_length_var = tk.StringVar(value="20")
        self.max_in_canal_var = tk.StringVar(value="4")
        self.move_ms_var = tk.StringVar(value="600")
        self.max_queue_var = tk.StringVar(value="4")
        self.queue_visible_var = tk.StringVar(value="4")

        self.last_status = {"INT": "0"}
        self.canal_positions = {}
        self.left_ready = []
        self.right_ready = []
        self.all_ships = []

        self.blink_on = True
        self.blink_elapsed_ms = 0

        self._build_ui()
        self._bind_keys()
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.after(50, self._tick)

    # =========================================================
    # UI
    # =========================================================

    def _build_ui(self):
        main = ttk.Frame(self.root, padding=8)
        main.pack(fill=tk.BOTH, expand=True)

        top = ttk.Frame(main)
        top.pack(fill=tk.X)

        self._build_connection_panel(top)
        self._build_runtime_panel(top)
        self._build_interrupt_panel(top)
        self._build_config_panel(top)

        mid = ttk.PanedWindow(main, orient=tk.HORIZONTAL)
        mid.pack(fill=tk.BOTH, expand=True, pady=(8, 8))

        left = ttk.Frame(mid)
        right = ttk.Frame(mid)
        mid.add(left, weight=3)
        mid.add(right, weight=2)

        self._build_visual_panel(left)
        self._build_console_panel(left)
        self._build_commands_panel(right)
        self._build_scenario_panel(right)

        ttk.Label(main, textvariable=self.status_var).pack(anchor="w")

    def _build_connection_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="Conexión", padding=8)
        frame.pack(side=tk.LEFT, fill=tk.X, expand=False, padx=(0, 8))

        ttk.Label(frame, text="Puerto").grid(row=0, column=0, sticky="w")
        self.port_combo = ttk.Combobox(frame, textvariable=self.port_var, width=14)
        self.port_combo["values"] = SerialClient.list_ports()
        self.port_combo.grid(row=0, column=1, padx=4)

        ttk.Button(frame, text="Actualizar", command=self.refresh_ports).grid(row=0, column=2, padx=4)

        ttk.Label(frame, text="Baudios").grid(row=1, column=0, sticky="w")
        ttk.Entry(frame, textvariable=self.baud_var, width=10).grid(row=1, column=1, sticky="w", padx=4)

        self.connect_btn = ttk.Button(frame, text="Conectar", command=self.toggle_connection)
        self.connect_btn.grid(row=1, column=2, padx=4)

    def _build_runtime_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="Ejecución", padding=8)
        frame.pack(side=tk.LEFT, fill=tk.X, expand=False, padx=(0, 8))

        for i, cmd in enumerate(["START", "PAUSE", "STEP", "RESET"]):
            ttk.Button(frame, text=cmd, command=lambda c=cmd: self.send_command(c)).grid(row=0, column=i, padx=3)

        for i, cmd in enumerate(["STATUS", "THREADS", "CANAL", "FLOW"]):
            ttk.Button(frame, text=cmd, command=lambda c=cmd: self.send_command(c)).grid(row=1, column=i, padx=3, pady=3)

        ttk.Checkbutton(frame, text="Auto", variable=self.auto_poll_enabled).grid(row=2, column=0, sticky="w")
        ttk.Checkbutton(frame, text="SNAPSHOT", variable=self.fast_snapshot_mode).grid(row=2, column=1, sticky="w")
        ttk.Entry(frame, textvariable=self.auto_poll_ms_var, width=6).grid(row=2, column=2, sticky="w")
        ttk.Label(frame, text="ms").grid(row=2, column=3, sticky="w")

    def _build_interrupt_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="Interrupción", padding=8)
        frame.pack(side=tk.LEFT, fill=tk.X, expand=False, padx=(0, 8))

        ttk.Button(frame, text="INT ON", command=lambda: self.send_command("INTERRUPT ON")).grid(row=0, column=0, padx=3)
        ttk.Button(frame, text="INT OFF", command=lambda: self.send_command("INTERRUPT OFF")).grid(row=0, column=1, padx=3)
        ttk.Button(frame, text="TOGGLE", command=lambda: self.send_command("INTERRUPT TOGGLE")).grid(row=0, column=2, padx=3)

        ttk.Button(frame, text="STATUS", command=lambda: self.send_command("INTERRUPT STATUS")).grid(row=1, column=0, padx=3, pady=3)
        ttk.Button(frame, text="SENSOR ON", command=lambda: self.send_command("INTERRUPT SENSOR ON")).grid(row=1, column=1, padx=3, pady=3)
        ttk.Button(frame, text="SENSOR OFF", command=lambda: self.send_command("INTERRUPT SENSOR OFF")).grid(row=1, column=2, padx=3, pady=3)

    def _build_config_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="Configuración", padding=8)
        frame.pack(side=tk.LEFT, fill=tk.X, expand=True)

        ttk.Label(frame, text="Scheduler").grid(row=0, column=0, sticky="w")
        ttk.Combobox(frame, textvariable=self.sched_var,
                     values=["FCFS", "RR", "PRIORITY", "SJF", "STRN", "EDF"],
                     width=12, state="readonly").grid(row=0, column=1, padx=3)

        ttk.Label(frame, text="Flujo").grid(row=0, column=2, sticky="w")
        ttk.Combobox(frame, textvariable=self.flow_var,
                     values=["TICO", "EQUIDAD", "LETRERO"],
                     width=12, state="readonly").grid(row=0, column=3, padx=3)

        fields = [
            ("Quantum", self.quantum_var, 1, 0),
            ("Letrero", self.letrero_var, 1, 2),
            ("W", self.w_var, 1, 4),
            ("Largo", self.canal_length_var, 2, 0),
            ("Max canal", self.max_in_canal_var, 2, 2),
            ("Move ms", self.move_ms_var, 2, 4),
            ("Max cola", self.max_queue_var, 3, 0),
            ("Visible", self.queue_visible_var, 3, 2),
        ]

        for label, var, row, col in fields:
            ttk.Label(frame, text=label).grid(row=row, column=col, sticky="w", padx=(8 if col else 0, 2), pady=2)
            ttk.Entry(frame, textvariable=var, width=8).grid(row=row, column=col + 1, sticky="w", padx=3, pady=2)

        ttk.Button(frame, text="Aplicar", command=self.apply_config).grid(row=3, column=4, columnspan=2, padx=6)

    def _build_visual_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="Vista lógica", padding=8)
        frame.pack(fill=tk.X, expand=False)

        self.canvas = tk.Canvas(frame, height=260, bg="#1f1f1f", highlightthickness=0)
        self.canvas.pack(fill=tk.X, expand=True)

    def _build_console_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="Consola serial", padding=8)
        frame.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

        self.console = tk.Text(frame, height=16, wrap=tk.WORD, bg="#111111", fg="#e8e8e8", insertbackground="#e8e8e8")
        self.console.pack(fill=tk.BOTH, expand=True)

        cmd_frame = ttk.Frame(frame)
        cmd_frame.pack(fill=tk.X, pady=(6, 0))

        self.manual_cmd = tk.StringVar()
        entry = ttk.Entry(cmd_frame, textvariable=self.manual_cmd)
        entry.pack(side=tk.LEFT, fill=tk.X, expand=True)
        entry.bind("<Return>", lambda _event: self.send_manual())

        ttk.Button(cmd_frame, text="Enviar", command=self.send_manual).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(cmd_frame, text="Limpiar consola", command=lambda: self.console.delete("1.0", tk.END)).pack(side=tk.LEFT, padx=(6, 0))

    def _build_commands_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="Barcos", padding=8)
        frame.pack(fill=tk.X)

        buttons = [
            ("Normal L (n)", "n"),
            ("Fisher L (f)", "f"),
            ("Patrol L (p)", "p"),
            ("Normal R (N)", "N"),
            ("Fisher R (F)", "F"),
            ("Patrol R (P)", "P"),
        ]

        for idx, (label, cmd) in enumerate(buttons):
            ttk.Button(frame, text=label, command=lambda c=cmd: self.send_command(c)).grid(
                row=idx // 2,
                column=idx % 2,
                sticky="ew",
                padx=4,
                pady=4
            )

        frame.columnconfigure(0, weight=1)
        frame.columnconfigure(1, weight=1)

    def _build_scenario_panel(self, parent):
        frame = ttk.LabelFrame(parent, text="Escenarios .txt", padding=8)
        frame.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

        ttk.Label(frame, text="Tokens: n f p N F P. Acepta espacios, comas, líneas y comentarios #.").pack(anchor="w")

        self.scenario_path_var = tk.StringVar(value=str(CURRENT_DIR / "scenarios" / "a.txt"))
        ttk.Entry(frame, textvariable=self.scenario_path_var).pack(fill=tk.X, pady=(6, 3))

        btns = ttk.Frame(frame)
        btns.pack(fill=tk.X)

        ttk.Button(btns, text="Buscar", command=self.pick_scenario).pack(side=tk.LEFT)
        ttk.Button(btns, text="Cargar escenario", command=self.load_scenario).pack(side=tk.LEFT, padx=6)
        ttk.Button(frame, text="Crear ejemplos", command=self.create_example_scenarios).pack(anchor="w", pady=(8, 0))

        self.scenario_preview = tk.Text(frame, height=8, wrap=tk.WORD)
        self.scenario_preview.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

    # =========================================================
    # Tick: un solo hilo
    # Este método se ejecuta periódicamente y actualiza la GUI, lectura serial y polling automático.
    # =========================================================

    def _tick(self):
        tick_ms = 50

        if self.connected:
            self._read_serial_available()
            self._auto_poll_elapsed_ms += tick_ms

            try:
                auto_ms = int(self.auto_poll_ms_var.get())
            except ValueError:
                auto_ms = 120

            if auto_ms < 80:
                auto_ms = 80

            if self.auto_poll_enabled.get() and self._auto_poll_elapsed_ms >= auto_ms:
                self._auto_poll_elapsed_ms = 0
                self._send_auto_poll_command()

        self._update_blink(tick_ms)
        self.draw_visual()

        self.root.after(tick_ms, self._tick)

    def _read_serial_available(self):
        # Lee todas las líneas recibidas del puerto serial y las procesa.
        for line in self.client.poll_lines():
            if not line.startswith("SNAPSHOT "):
                self.log(line)
            self.parse_line(line)

    def _send_auto_poll_command(self):
        # Envía comandos periódicos según el modo SNAPSHOT o el modo legado.
        if self.fast_snapshot_mode.get():
            try:
                self.client.send("SNAPSHOT")
            except Exception as exc:
                self.log(f"ERR SNAPSHOT {exc}")
            return

        commands = ["CANAL", "THREADS", "STATUS"]
        cmd = commands[self._legacy_phase % len(commands)]
        self._legacy_phase += 1

        try:
            self.client.send(cmd)
        except Exception as exc:
            self.log(f"ERR AUTO_POLL {exc}")

    def _update_blink(self, tick_ms):
        self.blink_elapsed_ms += tick_ms
        if self.blink_elapsed_ms >= 500:
            self.blink_elapsed_ms = 0
            self.blink_on = not self.blink_on

    # =========================================================
    # Serial
    # =========================================================

    def refresh_ports(self):
        # Actualiza la lista de puertos seriales disponibles en la interfaz.
        ports = SerialClient.list_ports()
        self.port_combo["values"] = ports
        self.log(f"Puertos detectados: {ports}")

    def toggle_connection(self):
        # Conecta o desconecta el cliente serial según el estado actual.
        if self.connected:
            self.client.disconnect()
            self.connected = False
            self.connect_btn.config(text="Conectar")
            self.status_var.set("Desconectado")
            self.log("Desconectado")
            return

        try:
            port = self.port_var.get().strip()
            baud = int(self.baud_var.get().strip())
            self.client.connect(port, baud)
            self.connected = True
            self.connect_btn.config(text="Desconectar")
            self.status_var.set(f"Conectado a {port}")
            self.log(f"Conectado a {port} @ {baud}")

            for cmd in ["CONFIG", "INTERRUPT STATUS", "SNAPSHOT"]:
                self.client.send(cmd)

        except Exception as exc:
            messagebox.showerror("Error de conexión", str(exc))
            self.log(f"ERR CONNECT {exc}")

    def send_command(self, command):
        # Envía un comando al ESP32 y solicita refresco si es necesario.
        try:
            if not self.connected:
                self.log("ERR GUI: no conectado")
                return

            self.client.send(command)
            self.log(f"> {command}")

            if (
                command in {"START", "PAUSE", "STEP", "RESET"}
                or command in VALID_SHIP_TOKENS
                or command.startswith("CONFIG")
                or command.startswith("INTERRUPT")
            ):
                self.request_refresh()

        except Exception as exc:
            self.log(f"ERR SEND {exc}")

    def request_refresh(self):
        if not self.connected:
            return
        try:
            self.client.send("SNAPSHOT")
        except Exception:
            return

    def send_manual(self):
        cmd = self.manual_cmd.get().strip()
        if not cmd:
            return
        self.send_command(cmd)
        self.manual_cmd.set("")

    # =========================================================
    # Configuración
    # =========================================================

    def apply_config(self):
        # Construye y envía todos los comandos de configuración al firmware.
        commands = [
            f"CONFIG SCHED={self.sched_var.get()}",
            f"CONFIG FLOW={self.flow_var.get()}",
            f"CONFIG QUANTUM={self.quantum_var.get()}",
            f"CONFIG LETRERO={self.letrero_var.get()}",
            f"CONFIG W={self.w_var.get()}",
            f"CONFIG CANAL_LENGTH={self.canal_length_var.get()}",
            f"CONFIG MAX_IN_CANAL={self.max_in_canal_var.get()}",
            f"CONFIG MOVE_MS={self.move_ms_var.get()}",
            f"CONFIG MAX_QUEUE={self.max_queue_var.get()}",
            f"CONFIG QUEUE_VISIBLE={self.queue_visible_var.get()}",
            "CONFIG",
            "SNAPSHOT",
        ]

        for cmd in commands:
            self.send_command(cmd)

    # =========================================================
    # Escenarios
    # =========================================================

    def pick_scenario(self):
        path = filedialog.askopenfilename(
            title="Seleccionar escenario",
            initialdir=str(CURRENT_DIR / "scenarios"),
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")]
        )
        if path:
            self.scenario_path_var.set(path)
            self.preview_scenario(Path(path))

    def preview_scenario(self, path):
        try:
            text = path.read_text(encoding="utf-8")
        except Exception as exc:
            text = f"ERR {exc}"

        tokens = self.parse_scenario_text(text)
        self.scenario_preview.delete("1.0", tk.END)
        self.scenario_preview.insert(tk.END, text)
        self.scenario_preview.insert(tk.END, f"\n\nTokens válidos: {' '.join(tokens)}")

    def parse_scenario_text(self, text):
        clean_parts = []

        for line in text.splitlines():
            clean_parts.append(line.split("#", 1)[0])

        raw = re.split(r"[\s,;]+", " ".join(clean_parts))

        tokens = []

        for token in raw:
            if not token:
                continue
            if token in VALID_SHIP_TOKENS:
                tokens.append(token)
            else:
                self.log(f"WARN escenario: token ignorado '{token}'")

        return tokens

    def load_scenario(self):
        path = Path(self.scenario_path_var.get()).expanduser()

        try:
            text = path.read_text(encoding="utf-8")
        except Exception as exc:
            messagebox.showerror("Error de escenario", str(exc))
            return

        tokens = self.parse_scenario_text(text)

        if not tokens:
            self.log("Escenario vacío o sin tokens válidos")
            return

        self.log(f"Cargando escenario {path.name}: {' '.join(tokens)}")

        for token in tokens:
            self.send_command(token)

        self.request_refresh()

    def create_example_scenarios(self):
        scenario_dir = CURRENT_DIR / "scenarios"
        scenario_dir.mkdir(exist_ok=True)

        examples = {
            "a.txt": "p n n f P N F P\n",
            "b.txt": "# Prueba mixta\nn f p\nN F P\n",
            "c.txt": "# Letrero / prioridad\np p n f\nP N F P\n",
        }

        for name, content in examples.items():
            path = scenario_dir / name
            if not path.exists():
                path.write_text(content, encoding="utf-8")

        self.scenario_path_var.set(str(scenario_dir / "a.txt"))
        self.preview_scenario(scenario_dir / "a.txt")
        self.log(f"Ejemplos creados en {scenario_dir}")

    # =========================================================
    # Parsing
    # =========================================================

    def parse_line(self, line):
        # Interpreta las líneas entrantes del firmware y actualiza el estado interno.
        if line.startswith("SNAPSHOT "):
            self._parse_snapshot(line)
            return

        if line.startswith("STATE "):
            self.last_status.update(self.parse_key_values(line))
            return

        if line.startswith("CONFIG "):
            values = self.parse_key_values(line)
            self._sync_config_fields(values)
            return

        if line.startswith("CANAL "):
            self.last_status.update(self.parse_key_values(line))
            self.canal_positions.clear()
            return

        if line.startswith("FLOW "):
            # Ej: FLOW ... INTERRUPTED=1 si luego se agrega.
            self.last_status.update(self.parse_key_values(line))
            return

        if line.startswith("INTERRUPT "):
            self._parse_interrupt_status(line)
            return

        if line.startswith("POS "):
            self._parse_canal_position(line)
            return

        if line.startswith("THREADS "):
            self.left_ready.clear()
            self.right_ready.clear()
            self.all_ships.clear()
            return

        if line.startswith("SHIP "):
            self._parse_ship_line(line)
            return

    def parse_key_values(self, line):
        result = {}
        for part in line.split()[1:]:
            if "=" in part:
                k, v = part.split("=", 1)
                result[k] = v
        return result

    def _parse_interrupt_status(self, line):
        values = self.parse_key_values(line)
        # Acepta ACTIVE=1, INT=1 o INTERRUPTED=1.
        if "ACTIVE" in values:
            self.last_status["INT"] = values["ACTIVE"]
        if "INT" in values:
            self.last_status["INT"] = values["INT"]
        if "INTERRUPTED" in values:
            self.last_status["INT"] = values["INTERRUPTED"]
        self.last_status.update(values)

    def _parse_snapshot(self, line):
        values = self.parse_key_values(line)

        self.last_status.update(values)

        # Acepta INT=0/1 desde SNAPSHOT.
        if "INT" not in self.last_status:
            self.last_status["INT"] = "0"

        self.canal_positions.clear()
        canal_text = values.get("C", "")

        if canal_text and canal_text != "-":
            for item in canal_text.split(","):
                parts = item.split(":")
                if len(parts) < 6:
                    continue

                try:
                    pos = int(parts[0])
                except ValueError:
                    continue

                self.canal_positions[pos] = {
                    "SHIP": parts[1],
                    "TYPE": parts[2],
                    "DIR": parts[3],
                    "REM": parts[4],
                    "SLOT": parts[5],
                }

        self.left_ready = self._parse_ready_snapshot(values.get("LQ", ""))
        self.right_ready = self._parse_ready_snapshot(values.get("RQ", ""))

    def _parse_ready_snapshot(self, text):
        result = []
        if not text or text == "-":
            return result

        for item in text.split(","):
            parts = item.split(":")
            if len(parts) < 3:
                continue
            result.append({
                "ID": parts[0],
                "TYPE": parts[1],
                "DIR": parts[2],
                "STATE": "READY",
            })
        return result

    def _sync_config_fields(self, values):
        mapping = {
            "SCHED": self.sched_var,
            "FLOW": self.flow_var,
            "QUANTUM": self.quantum_var,
            "LETRERO": self.letrero_var,
            "W": self.w_var,
            "CANAL_LENGTH": self.canal_length_var,
            "MAX_IN_CANAL": self.max_in_canal_var,
            "MOVE_MS": self.move_ms_var,
            "MAX_QUEUE": self.max_queue_var,
            "QUEUE_VISIBLE": self.queue_visible_var,
        }

        for key, var in mapping.items():
            if key in values:
                var.set(values[key])

    def _parse_canal_position(self, line):
        parts = line.split()

        if len(parts) < 3:
            return

        try:
            pos = int(parts[1])
        except ValueError:
            return

        if "EMPTY" in parts:
            self.canal_positions[pos] = None
            return

        data = self.parse_key_values("X " + " ".join(parts[2:]))
        self.canal_positions[pos] = data

    def _parse_ship_line(self, line):
        data = self.parse_key_values("X " + " ".join(line.split()[1:]))

        if not data:
            return

        self.all_ships.append(data)

        state = data.get("STATE", "")
        direction = data.get("DIR", "")

        if state == "READY":
            if direction == "L":
                self.left_ready.append(data)
            elif direction == "R":
                self.right_ready.append(data)

    # =========================================================
    # Dibujo
    # =========================================================

    def draw_visual(self):
        # Dibuja la vista lógica del canal, barcos y colas en el área de visualización.
        self.canvas.delete("all")

        width = max(self.canvas.winfo_width(), 900)
        height = max(self.canvas.winfo_height(), 250)

        self.canvas.create_rectangle(0, 0, width, height, fill="#1f1f1f", outline="")

        run = self.last_status.get("RUN", "?")
        sched = self.last_status.get("SCHED", self.sched_var.get())
        flow = self.last_status.get("FLOW", self.flow_var.get())
        direction = self.last_status.get("DIR", "FREE")
        interrupted = self.last_status.get("INT", "0") in {"1", "ON", "TRUE", "ACTIVE"}

        try:
            length = int(self.last_status.get("LEN", self.canal_length_var.get() or 20))
        except ValueError:
            length = 20

        count = self.last_status.get("COUNT", self.last_status.get("CANAL", "?"))

        flow_arrow = self.flow_arrow_text(direction)

        self.canvas.create_text(
            10,
            8,
            anchor="nw",
            fill="#eeeeee",
            text=f"SCHED={sched} | FLOW={flow} | DIR={flow_arrow} | RUN={run} | LEN={length} | CANAL={count} | INT={int(interrupted)}"
        )

        x0 = 210
        x1 = width - 210
        y = 120
        segment_h = 34
        led_count = 10
        led_w = (x1 - x0) / led_count

        self.draw_ready_queue("READY L", self.left_ready, 18, y - 58)
        self.draw_ready_queue("READY R", self.right_ready, width - 168, y - 58)

        self.canvas.create_rectangle(x0, y - segment_h, x1, y + segment_h, outline="#888888", width=2)

        for i in range(led_count):
            lx0 = x0 + i * led_w
            lx1 = x0 + (i + 1) * led_w
            self.canvas.create_rectangle(lx0, y - segment_h, lx1, y + segment_h, outline="#555555")
            self.canvas.create_text((lx0 + lx1) / 2, y + segment_h + 12, fill="#999999", text=str(i))

        self.draw_flow_arrow(x0, x1, y, segment_h, direction, interrupted)

        # Puertas: blanco parpadeante abiertas, rojo fijo en interrupción.
        gate_color = "#cc2222" if interrupted else ("#dddddd" if self.blink_on else "#333333")
        self.canvas.create_oval(x0 - 40, y - 16, x0 - 12, y + 16, fill=gate_color, outline="")
        self.canvas.create_oval(x1 + 12, y - 16, x1 + 40, y + 16, fill=gate_color, outline="")
        self.canvas.create_text(x0 - 26, y + 36, fill="#cccccc", text="Gate")
        self.canvas.create_text(x1 + 26, y + 36, fill="#cccccc", text="Gate")

        if length <= 0:
            length = 1

        for pos, data in self.canal_positions.items():
            if not data:
                continue

            try:
                p = int(pos)
            except ValueError:
                continue

            if "SLOT" in data:
                try:
                    slot = int(data["SLOT"])
                except ValueError:
                    slot = min(led_count - 1, max(0, (p * led_count) // length))
            else:
                slot = min(led_count - 1, max(0, (p * led_count) // length))

            cx = x0 + slot * led_w + led_w / 2
            cy = y

            ship_type = data.get("TYPE", "UNKNOWN")
            ship_id = data.get("SHIP", data.get("ID", "?"))
            color = self.color_for_type(ship_type)

            self.canvas.create_oval(cx - 16, cy - 16, cx + 16, cy + 16, fill=color, outline="#ffffff")
            self.canvas.create_text(cx, cy, fill="#ffffff", text=str(ship_id))
            self.canvas.create_text(cx, cy - 28, fill="#dddddd", text=ship_type[0:3])

        self.draw_legend(height)

    def draw_flow_arrow(self, x0, x1, y, segment_h, direction, interrupted):
        arrow_y = y - segment_h - 22

        if interrupted:
            self.canvas.create_text(
                (x0 + x1) / 2,
                arrow_y - 12,
                fill="#cc2222",
                text="Interrupción activa / canal cerrado"
            )
            self.canvas.create_line(
                x0 + 20, arrow_y,
                x1 - 20, arrow_y,
                fill="#cc2222",
                width=4
            )
            return

        if direction == "L_TO_R":
            self.canvas.create_line(
                x0 + 20, arrow_y,
                x1 - 20, arrow_y,
                arrow=tk.LAST,
                fill="#f0d43a",
                width=4
            )
            self.canvas.create_text(
                (x0 + x1) / 2,
                arrow_y - 12,
                fill="#f0d43a",
                text="Flujo izquierda → derecha"
            )

        elif direction == "R_TO_L":
            self.canvas.create_line(
                x1 - 20, arrow_y,
                x0 + 20, arrow_y,
                arrow=tk.LAST,
                fill="#3ad4f0",
                width=4
            )
            self.canvas.create_text(
                (x0 + x1) / 2,
                arrow_y - 12,
                fill="#3ad4f0",
                text="Flujo derecha → izquierda"
            )

        else:
            self.canvas.create_text(
                (x0 + x1) / 2,
                arrow_y - 12,
                fill="#888888",
                text="Canal libre / sin dirección activa"
            )

    def draw_ready_queue(self, title, ships, x, y):
        self.canvas.create_text(x, y - 22, anchor="nw", fill="#eeeeee", text=title)

        max_visible = 4
        box_w = 34
        gap = 8

        for i in range(max_visible):
            by = y + i * (box_w + gap)
            self.canvas.create_rectangle(x, by, x + box_w, by + box_w, outline="#777777", width=1)

            if i < len(ships):
                ship = ships[i]
                ship_type = ship.get("TYPE", "UNKNOWN")
                ship_id = ship.get("ID", ship.get("SHIP", "?"))
                color = self.color_for_type(ship_type)

                self.canvas.create_oval(x + 5, by + 5, x + box_w - 5, by + box_w - 5, fill=color, outline="#ffffff")
                self.canvas.create_text(x + box_w / 2, by + box_w / 2, fill="#ffffff", text=str(ship_id))

        extra = len(ships) - max_visible
        if extra > 0:
            self.canvas.create_text(x + box_w + 8, y + 12, anchor="w", fill="#cccccc", text=f"+{extra}")

    def draw_legend(self, height):
        legend_y = height - 42
        legend = [
            ("NORMAL", "#1ba34a"),
            ("FISHER", "#2f6df6"),
            ("PATROL", "#a23be8"),
            ("Gate abierta", "#dddddd"),
            ("Gate cerrada", "#cc2222"),
        ]

        x = 12
        for label, color in legend:
            self.canvas.create_rectangle(x, legend_y, x + 18, legend_y + 18, fill=color, outline="")
            self.canvas.create_text(x + 24, legend_y + 9, anchor="w", fill="#eeeeee", text=label)
            x += 130

    def flow_arrow_text(self, direction):
        if direction == "L_TO_R":
            return "L → R"
        if direction == "R_TO_L":
            return "R → L"
        return "FREE"

    def color_for_type(self, ship_type):
        if ship_type == "NORMAL":
            return "#1ba34a"
        if ship_type == "FISHER":
            return "#2f6df6"
        if ship_type == "PATROL":
            return "#a23be8"
        return "#cccccc"

    # =========================================================
    # Utilidad
    # =========================================================

    def log(self, text):
        self.console.insert(tk.END, text + "\n")
        self.console.see(tk.END)

    def _bind_keys(self):
        self.root.bind("<KeyPress-w>", lambda _event: self.close())
        self.root.bind("<space>", lambda _event: self.send_command("STEP"))

        for key in ["n", "f", "p", "N", "F", "P"]:
            self.root.bind(f"<KeyPress-{key}>", lambda _event, k=key: self.send_command(k))

    def close(self):
        try:
            if self.connected:
                self.client.send("PAUSE")
                self.client.disconnect()
        finally:
            self.root.destroy()


def main():
    # Inicia la aplicación gráfica y arranca el bucle principal de Tkinter.
    root = tk.Tk()
    SchedulingShipsGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
