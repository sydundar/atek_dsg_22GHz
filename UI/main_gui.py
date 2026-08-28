import sys
import os
import re
import time
import datetime
import serial.tools.list_ports
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QPushButton, QLabel, QComboBox, QGroupBox, QTextEdit, QDoubleSpinBox, QSpinBox, QTabWidget,
    QAbstractSpinBox, QMessageBox, QFileDialog
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal, QLocale, QProcess

from DSG_Remote import DSG_Remote

# --- THEME AND COLORS ---
COLOR_BG = "#1e1e2e"
COLOR_PANEL = "#313244"
COLOR_TEXT = "#cdd6f4"
COLOR_ACCENT = "#89b4fa"
COLOR_SUCCESS = "#a6e3a1"
COLOR_ERROR = "#f38ba8"
COLOR_WARNING = "#f9e2af"

STYLESHEET = f"""
    QMainWindow {{ background-color: {COLOR_BG}; color: {COLOR_TEXT}; }}
    QWidget {{ font-family: 'Segoe UI', sans-serif; font-size: 11pt; color: {COLOR_TEXT}; }}
    QGroupBox {{ border: 1px solid #45475a; margin-top: 1.2em; border-radius: 6px; font-weight: bold; }}
    QGroupBox::title {{ subcontrol-origin: margin; left: 10px; color: {COLOR_ACCENT}; }}

    QPushButton {{ background-color: {COLOR_PANEL}; border: 1px solid #45475a; padding: 10px; border-radius: 4px; font-weight: bold; }}
    QPushButton:hover {{ border-color: {COLOR_ACCENT}; }}
    QPushButton:pressed {{ background-color: {COLOR_ACCENT}; color: #11111b; }}

    QComboBox, QTextEdit {{ background-color: #11111b; border: 1px solid #45475a; padding: 5px; border-radius: 3px; color: white; }}

    QDoubleSpinBox, QSpinBox {{ 
        background-color: #11111b; 
        border: 1px solid #45475a; 
        padding: 5px 25px 5px 5px; 
        border-radius: 3px; 
        color: white; 
    }}

    QTabWidget::pane {{ border: 1px solid #45475a; background: {COLOR_BG}; }}
    QTabBar::tab {{ background: {COLOR_PANEL}; border: 1px solid #45475a; padding: 10px 20px; }}
    QTabBar::tab:selected {{ background: {COLOR_ACCENT}; color: #11111b; font-weight: bold; }}

    QMessageBox {{ background-color: {COLOR_BG}; color: {COLOR_TEXT}; }}
    QMessageBox QLabel {{ color: {COLOR_TEXT}; font-weight: bold; }}
    QMessageBox QPushButton {{ background-color: {COLOR_PANEL}; color: {COLOR_TEXT}; min-width: 80px; }}
"""


# -----------------------------------------------------------------------------
# Background communication thread
# Handles serial communication with the DSG device without blocking the GUI.
# It sends queued SCPI-like commands, reads device responses, and emits Qt signals
# for logs, telemetry, connection status, and UI synchronization.
# -----------------------------------------------------------------------------
class ScpiWorker(QThread):
    log_msg = pyqtSignal(str)
    connection_changed = pyqtSignal(bool)
    telemetry_data = pyqtSignal(dict)
    sync_data = pyqtSignal(dict)
    sweep_auto_stopped = pyqtSignal()

    def __init__(self):
        super().__init__()
        self.port = ""
        self.running = False
        self.is_sweeping = False
        self.cmd_queue = []

    def connect_device(self, port):
        self.port = port
        self.start()

    def disconnect_device(self):
        self.running = False
        self.wait()

    def send_cmd(self, cmd):
        self.cmd_queue.append(cmd)

    def abort_sweep_urgent(self):
        self.is_sweeping = False
        if hasattr(self, 'dsg') and self.dsg.is_open():
            self.dsg.SweepAbortUrgent()

    def process_rx(self, resp):
        if not resp or resp == "0":
            return
        if "TIMING" in resp or "DEBUG" in resp:
            return
        if "Freq(MHz):" in resp:
            freq_str = resp.split("Freq(MHz):")[-1].strip()
            self.log_msg.emit(f"〰 {freq_str}")
            return
        if resp.strip() == "SWEEP:DONE":
            # The device stopped the sweep on its own (configured Count
            # reached), not because the user pressed Stop. Let the GUI know
            # so it can reset the toggle button - otherwise it would keep
            # showing "STOP SWEEP" even though the sweep already finished.
            self.is_sweeping = False
            self.sweep_auto_stopped.emit()
            return
        if self.is_sweeping:
            return
        if resp.startswith("-"):
            self.log_msg.emit(f"⚠️ Device Message: {resp}")

    # -------------------------------------------------------------------------
    # ADC sampling helper
    # Performs one dummy read, then averages three ADC readings to reduce noise.
    # -------------------------------------------------------------------------
    def get_stable_adc(self, channel: int) -> float:
        if not hasattr(self, 'dsg') or not self.dsg.is_open():
            return 0.0

        self.dsg.ADCRead(channel)
        time.sleep(0.01)

        val1 = self.dsg.ADCRead(channel)
        val2 = self.dsg.ADCRead(channel)
        val3 = self.dsg.ADCRead(channel)

        return (val1 + val2 + val3) / 3.0

    def run(self):
        try:
            self.dsg = DSG_Remote(self.port)
            self.running = True
            self.connection_changed.emit(True)
            self.log_msg.emit(f"[SYS] Successfully connected to port {self.port}.")
            # --- REQUEST CURRENT DEVICE SCREEN SETTINGS AND SEND THEM TO THE GUI ---
            time.sleep(0.2)
            settings = self.dsg.GetSyncSettings()
            if settings:
                self.sync_data.emit(settings)
            # ------------------------------------------------

            last_telemetry_time = time.time()

            # Main worker loop: process queued commands, update telemetry, and read device messages.
            while self.running:
                while self.cmd_queue:
                    cmd = self.cmd_queue.pop(0)
                    self.dsg._write(cmd)

                    # --- COMMAND ACKNOWLEDGMENT / HANDSHAKE LOGIC ---
                    # Wait up to 0.5 seconds for the '0' (OK) response from the ESP32.
                    t0 = time.time()
                    ack = False
                    while time.time() - t0 < 0.5:
                        if self.dsg.in_waiting() > 0:
                            resp = self.dsg.read_line()
                            if resp == "0":
                                ack = True
                                break
                            self.process_rx(resp)  # Display any non-OK response, such as device error messages.
                        time.sleep(0.005)

                    # Add a short delay if the device does not acknowledge the command in time.
                    if not ack:
                        time.sleep(0.05)

                # Periodic telemetry polling is disabled during sweep mode to avoid disturbing timing.
                if not self.is_sweeping and time.time() - last_telemetry_time > 1.5 and not self.cmd_queue:
                    telemetry = {}
                    try:
                        telemetry["CURR"] = str(self.get_stable_adc(3))
                        telemetry["TEMP"] = str(self.get_stable_adc(4))
                        telemetry["VOLT"] = str(self.get_stable_adc(2))

                        pll_val = self.dsg.PLLRead(74)
                        telemetry["PLL"] = hex(pll_val)

                        self.telemetry_data.emit(telemetry)
                    except Exception:
                        pass
                    last_telemetry_time = time.time()

                lines_read = 0
                while self.dsg.in_waiting() > 0 and lines_read < 100:
                    resp = self.dsg.read_line()
                    self.process_rx(resp)
                    lines_read += 1

                time.sleep(0.01)

        except Exception as e:
            self.log_msg.emit(f"[ERR] Connection Error: {str(e)}")
        finally:
            if hasattr(self, 'dsg'):
                self.dsg.close()
            self.connection_changed.emit(False)


# -----------------------------------------------------------------------------
# Main GUI window
# Builds the PyQt6 user interface and connects user actions to the communication
# worker. This class does not talk directly to the serial port; it sends commands
# through ScpiWorker to keep the interface responsive.
# -----------------------------------------------------------------------------
class DSGMainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("DSG 22.6 GHz - Professional Control Center")
        self.resize(1000, 650)
        self.setStyleSheet(STYLESHEET)

        self.worker = ScpiWorker()
        self.worker.log_msg.connect(self.append_log)
        self.worker.connection_changed.connect(self.on_connection_changed)
        self.worker.telemetry_data.connect(self.update_sensors)
        self.worker.sync_data.connect(self.sync_ui_with_device)
        self.worker.sweep_auto_stopped.connect(self.on_sweep_auto_stopped)

        self.rf_state = False
        self.firmware_process = None
        self.setup_ui()

    def setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)

        h_conn = QHBoxLayout()
        self.combo_port = QComboBox()
        self.combo_port.setMinimumWidth(150)

        self.btn_refresh = QPushButton("🔄 REFRESH")
        self.btn_refresh.setStyleSheet(f"background-color: {COLOR_PANEL}; padding: 8px;")
        self.btn_refresh.clicked.connect(self.refresh_ports)

        self.btn_connect = QPushButton("CONNECT")
        self.btn_connect.clicked.connect(self.toggle_connection)

        self.btn_load_cal = QPushButton("📁 LOAD CALIBRATION")
        self.btn_load_cal.setStyleSheet(
            f"background-color: {COLOR_WARNING}; color: #11111b; font-weight: bold; padding: 8px;")
        self.btn_load_cal.clicked.connect(self.load_calibration_csv)

        h_conn.addWidget(QLabel("Port:"))
        h_conn.addWidget(self.combo_port)
        h_conn.addWidget(self.btn_refresh)
        h_conn.addWidget(self.btn_connect)
        h_conn.addStretch()
        main_layout.addLayout(h_conn)

        h_body = QHBoxLayout()
        self.tabs = QTabWidget()

        # --- TAB 1: CW ---
        tab_cw = QWidget()
        cw_layout = QGridLayout(tab_cw)
        cw_layout.setSpacing(15)

        self.spin_freq = QDoubleSpinBox()
        self.spin_freq.setRange(0, 999999999)
        self.spin_freq.setValue(1000)
        self.spin_freq.setDecimals(3)

        self.combo_unit = QComboBox()
        self.combo_unit.addItems(["kHz", "MHz", "GHz"])
        self.combo_unit.setCurrentText("MHz")

        self.spin_freq_step = QDoubleSpinBox()
        self.spin_freq_step.setRange(0.001, 999999999)
        self.spin_freq_step.setDecimals(3)
        self.spin_freq_step.setValue(100.0)

        self.combo_step_unit = QComboBox()
        self.combo_step_unit.addItems(["kHz", "MHz", "GHz"])
        self.combo_step_unit.setCurrentText("MHz")

        self.spin_freq_step.valueChanged.connect(self.update_freq_step)
        self.combo_step_unit.currentTextChanged.connect(self.update_freq_step)
        self.combo_unit.currentTextChanged.connect(self.update_freq_step)

        self.spin_att = QDoubleSpinBox()
        self.spin_att.setRange(-20.0, 20.0)
        self.spin_att.setValue(4.0)
        self.spin_att.setDecimals(2)

        self.combo_filt = QComboBox()
        self.combo_filt.addItems(["Filter: OFF (0.15-22.6 GHz)", "Filter: ON (2-18 GHz)"])

        cw_layout.addWidget(QLabel("CW Frequency:"), 0, 0)
        cw_layout.addWidget(self.spin_freq, 0, 1)
        cw_layout.addWidget(self.combo_unit, 0, 2)
        cw_layout.addWidget(QLabel("Increment Step:"), 0, 3, alignment=Qt.AlignmentFlag.AlignRight)
        cw_layout.addWidget(self.spin_freq_step, 0, 4)
        cw_layout.addWidget(self.combo_step_unit, 0, 5)

        cw_layout.addWidget(QLabel("Target Power (dBm):"), 1, 0)
        cw_layout.addWidget(self.spin_att, 1, 1, 1, 5)
        cw_layout.addWidget(self.combo_filt, 2, 0, 1, 6)

        btn_apply_cw = QPushButton("APPLY CW SETTINGS")
        btn_apply_cw.clicked.connect(self.apply_cw_settings)
        btn_apply_cw.setStyleSheet(f"background-color: {COLOR_ACCENT}; color: #000; font-weight: bold; padding: 10px;")

        self.btn_rf = QPushButton("RF OUTPUT: OFF")
        self.btn_rf.clicked.connect(self.toggle_rf)
        self.btn_rf.setStyleSheet(
            f"border: 2px solid {COLOR_ERROR}; color: {COLOR_ERROR}; padding: 15px; font-size: 14pt;")

        cw_layout.addWidget(btn_apply_cw, 3, 0, 1, 6)
        cw_layout.addWidget(self.btn_rf, 4, 0, 1, 6)
        cw_layout.setRowStretch(5, 1)
        self.tabs.addTab(tab_cw, "📡 CW (Continuous Wave)")

        # --- TAB 2: SWEEP ---
        tab_sweep = QWidget()
        sw_layout = QGridLayout(tab_sweep)
        sw_layout.setSpacing(10)

        self.sw_start = QDoubleSpinBox()
        self.sw_start.setRange(0, 999999999);
        self.sw_start.setValue(300);
        self.sw_start.setDecimals(3)
        self.sw_start.setButtonSymbols(QAbstractSpinBox.ButtonSymbols.NoButtons)
        self.sw_start_u = QComboBox();
        self.sw_start_u.addItems(["kHz", "MHz", "GHz"]);
        self.sw_start_u.setCurrentText("MHz")

        self.sw_stop = QDoubleSpinBox()
        self.sw_stop.setRange(0, 999999999);
        self.sw_stop.setValue(22600);
        self.sw_stop.setDecimals(3)
        self.sw_stop.setButtonSymbols(QAbstractSpinBox.ButtonSymbols.NoButtons)
        self.sw_stop_u = QComboBox();
        self.sw_stop_u.addItems(["kHz", "MHz", "GHz"]);
        self.sw_stop_u.setCurrentText("MHz")

        h_start_pre = QHBoxLayout()
        h_start_pre.setSpacing(5)
        pre_style = "background-color: #45475a; padding: 4px 8px; font-size: 9pt; border-radius: 3px; font-weight: normal;"

        btn_st_1 = QPushButton("150 MHz");
        btn_st_1.setStyleSheet(pre_style)
        btn_st_1.clicked.connect(lambda: self.set_preset("start", 150.0, "MHz"))

        btn_st_2 = QPushButton("1 GHz");
        btn_st_2.setStyleSheet(pre_style)
        btn_st_2.clicked.connect(lambda: self.set_preset("start", 1.0, "GHz"))

        btn_st_3 = QPushButton("5 GHz");
        btn_st_3.setStyleSheet(pre_style)
        btn_st_3.clicked.connect(lambda: self.set_preset("start", 5.0, "GHz"))

        h_start_pre.addWidget(btn_st_1);
        h_start_pre.addWidget(btn_st_2);
        h_start_pre.addWidget(btn_st_3)
        h_start_pre.addStretch()

        h_stop_pre = QHBoxLayout()
        h_stop_pre.setSpacing(5)

        btn_sp_1 = QPushButton("5 GHz");
        btn_sp_1.setStyleSheet(pre_style)
        btn_sp_1.clicked.connect(lambda: self.set_preset("stop", 5.0, "GHz"))

        btn_sp_2 = QPushButton("10 GHz");
        btn_sp_2.setStyleSheet(pre_style)
        btn_sp_2.clicked.connect(lambda: self.set_preset("stop", 10.0, "GHz"))

        btn_sp_3 = QPushButton("22.6 GHz");
        btn_sp_3.setStyleSheet(pre_style)
        btn_sp_3.clicked.connect(lambda: self.set_preset("stop", 22.6, "GHz"))

        h_stop_pre.addWidget(btn_sp_1);
        h_stop_pre.addWidget(btn_sp_2);
        h_stop_pre.addWidget(btn_sp_3)
        h_stop_pre.addStretch()

        self.sw_step = QDoubleSpinBox()
        self.sw_step.setRange(0, 999999999);
        self.sw_step.setValue(100);
        self.sw_step.setDecimals(3)
        self.sw_step.setButtonSymbols(QAbstractSpinBox.ButtonSymbols.NoButtons)
        self.sw_step_u = QComboBox();
        self.sw_step_u.addItems(["kHz", "MHz", "GHz"]);
        self.sw_step_u.setCurrentText("MHz")

        self.sw_dwell = QDoubleSpinBox()
        self.sw_dwell.setRange(0, 1000000);
        self.sw_dwell.setValue(1);
        self.sw_dwell.setDecimals(0)
        self.sw_dwell.setButtonSymbols(QAbstractSpinBox.ButtonSymbols.NoButtons)

        self.sw_count = QSpinBox()
        self.sw_count.setRange(0, 999999)
        self.sw_count.setValue(0)  # 0 = run forever
        self.sw_count.setButtonSymbols(QAbstractSpinBox.ButtonSymbols.NoButtons)
        self.sw_count.setSpecialValueText("∞ (Continuous)")

        self.sw_pow = QDoubleSpinBox()
        self.sw_pow.setRange(-20.0, 20.0);
        self.sw_pow.setValue(4.0);
        self.sw_pow.setDecimals(2)

        self.sw_type = QComboBox()
        self.sw_type.addItems(["Linear (LIN)", "Logarithmic (LOG)"])

        # Sweep uses the same physical filter state as CW. Keep both selectors synchronized.
        self.sw_filt = QComboBox()
        self.sw_filt.addItems(["Filter: OFF (0.15-22.6 GHz)", "Filter: ON (2-18 GHz)"])
        self.sw_filt.setCurrentIndex(self.combo_filt.currentIndex())
        self.combo_filt.currentIndexChanged.connect(self.sync_filter_from_cw)
        self.sw_filt.currentIndexChanged.connect(self.sync_filter_from_sweep)

        sw_layout.addWidget(QLabel("Start:"), 0, 0);
        sw_layout.addWidget(self.sw_start, 0, 1);
        sw_layout.addWidget(self.sw_start_u, 0, 2)
        sw_layout.addLayout(h_start_pre, 0, 3)

        sw_layout.addWidget(QLabel("Stop:"), 1, 0);
        sw_layout.addWidget(self.sw_stop, 1, 1);
        sw_layout.addWidget(self.sw_stop_u, 1, 2)
        sw_layout.addLayout(h_stop_pre, 1, 3)

        sw_layout.addWidget(QLabel("Step:"), 2, 0);
        sw_layout.addWidget(self.sw_step, 2, 1);
        sw_layout.addWidget(self.sw_step_u, 2, 2)
        sw_layout.addWidget(QLabel("Dwell (ms):"), 3, 0);
        sw_layout.addWidget(self.sw_dwell, 3, 1, 1, 2)
        sw_layout.addWidget(QLabel("Target Power (dBm):"), 4, 0);
        sw_layout.addWidget(self.sw_pow, 4, 1, 1, 2)
        sw_layout.addWidget(QLabel("Type:"), 5, 0);
        sw_layout.addWidget(self.sw_type, 5, 1, 1, 2)
        sw_layout.addWidget(QLabel("Filter:"), 6, 0);
        sw_layout.addWidget(self.sw_filt, 6, 1, 1, 3)
        sw_layout.addWidget(QLabel("Sweep Count (0 = ∞):"), 7, 0);
        sw_layout.addWidget(self.sw_count, 7, 1, 1, 2)

        btn_apply_sw = QPushButton("LOAD SWEEP SETTINGS")
        btn_apply_sw.clicked.connect(self.apply_sweep_settings)
        btn_apply_sw.setStyleSheet(f"background-color: {COLOR_ACCENT}; color: #000; font-weight: bold;")

        self.btn_sweep_toggle = QPushButton("▶ START SWEEP")
        self.btn_sweep_toggle.setStyleSheet(
            f"background-color: {COLOR_SUCCESS}; color: #000; font-size: 12pt; padding: 12px; font-weight: bold;")
        self.btn_sweep_toggle.clicked.connect(self.toggle_sweep)

        sw_layout.addWidget(btn_apply_sw, 8, 0, 1, 4)
        sw_layout.addWidget(self.btn_sweep_toggle, 9, 0, 1, 4)
        sw_layout.setRowStretch(10, 1)
        self.tabs.addTab(tab_sweep, "📈 Sweep")

        # --- TAB 3: SETTINGS ---
        tab_settings = QWidget()
        settings_layout = QVBoxLayout(tab_settings)
        settings_layout.setSpacing(15)

        group_cal = QGroupBox("Calibration")
        cal_layout = QVBoxLayout(group_cal)
        self.btn_load_cal.setMinimumHeight(40)
        cal_layout.addWidget(self.btn_load_cal)
        settings_layout.addWidget(group_cal)

        group_firmware = QGroupBox("Firmware")
        firmware_layout = QVBoxLayout(group_firmware)

        self.btn_firmware_update = QPushButton("⬆️ FIRMWARE UPDATE")
        self.btn_firmware_update.setMinimumHeight(40)
        self.btn_firmware_update.setStyleSheet(
            f"background-color: {COLOR_ACCENT}; color: #11111b; font-weight: bold; padding: 8px;")
        self.btn_firmware_update.clicked.connect(self.flash_firmware)
        firmware_layout.addWidget(self.btn_firmware_update)
        settings_layout.addWidget(group_firmware)

        settings_layout.addStretch()
        self.tabs.addTab(tab_settings, "⚙️ Settings")

        h_body.addWidget(self.tabs, stretch=2)

        # Live device telemetry panel: current, voltage, power, temperature, and PLL lock state.
        group_sensor = QGroupBox("Device Screen (Live)")
        v_sensor = QVBoxLayout(group_sensor)
        v_sensor.setSpacing(15)
        lbl_style = f"background-color: #11111b; padding: 8px; border-radius: 4px; font-size: 13pt; font-weight: bold; color: {COLOR_WARNING};"

        self.lbl_curr = QLabel("Current: --.- A")
        self.lbl_curr.setStyleSheet(lbl_style);
        self.lbl_curr.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self.lbl_volt = QLabel("Voltage: --.- V")
        self.lbl_volt.setStyleSheet(lbl_style);
        self.lbl_volt.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self.lbl_power = QLabel("Power: --.- W")
        self.lbl_power.setStyleSheet(lbl_style);
        self.lbl_power.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self.lbl_temp = QLabel("Temperature: --.- C")
        self.lbl_temp.setStyleSheet(lbl_style);
        self.lbl_temp.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self.lbl_pll = QLabel("LD Result: UNKNOWN")
        self.lbl_pll.setStyleSheet(lbl_style);
        self.lbl_pll.setAlignment(Qt.AlignmentFlag.AlignCenter)

        v_sensor.addWidget(self.lbl_curr)
        v_sensor.addWidget(self.lbl_volt)
        v_sensor.addWidget(self.lbl_power)
        v_sensor.addWidget(self.lbl_temp)
        v_sensor.addWidget(self.lbl_pll)
        v_sensor.addStretch()
        h_body.addWidget(group_sensor, stretch=1)
        main_layout.addLayout(h_body, stretch=3)

        log_layout = QVBoxLayout()
        log_header = QHBoxLayout()
        lbl_log = QLabel("System Logs")
        lbl_log.setStyleSheet(f"font-weight: bold; color: {COLOR_ACCENT}; font-size: 12pt;")
        btn_clear_log = QPushButton("CLEAR LOG")
        btn_clear_log.setStyleSheet(
            f"background-color: {COLOR_PANEL}; padding: 5px 15px; border-radius: 4px; font-size: 9pt;")
        btn_clear_log.clicked.connect(self.clear_log)

        log_header.addWidget(lbl_log)
        log_header.addStretch()
        log_header.addWidget(btn_clear_log)
        log_layout.addLayout(log_header)

        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setStyleSheet(
            "background: #11111b; font-family: 'Consolas'; font-size: 9pt; border-radius: 4px; padding: 5px;")
        log_layout.addWidget(self.txt_log)
        main_layout.addLayout(log_layout, stretch=1)

        # Firmware build timestamp, shown bottom-left (status bar). Populated
        # once the device connects and sends its :SYNC? data (see
        # sync_ui_with_device); reset to "--" on disconnect.
        self.lbl_fw_version = QLabel("Firmware: --")
        self.lbl_fw_version.setStyleSheet(f"color: {COLOR_TEXT}; padding: 2px 8px;")
        self.statusBar().addWidget(self.lbl_fw_version)

        self.refresh_ports()
        self.update_freq_step()

    def sync_filter_from_cw(self, index):
        """Keep the Sweep filter selector synchronized with the CW filter selector."""
        if self.sw_filt.currentIndex() != index:
            self.sw_filt.blockSignals(True)
            self.sw_filt.setCurrentIndex(index)
            self.sw_filt.blockSignals(False)

    def sync_filter_from_sweep(self, index):
        """Keep the CW filter selector synchronized with the Sweep filter selector."""
        if self.combo_filt.currentIndex() != index:
            self.combo_filt.blockSignals(True)
            self.combo_filt.setCurrentIndex(index)
            self.combo_filt.blockSignals(False)

    def set_preset(self, target, val, unit):
        if target == "start":
            self.sw_start.setValue(val)
            self.sw_start_u.setCurrentText(unit)
        elif target == "stop":
            self.sw_stop.setValue(val)
            self.sw_stop_u.setCurrentText(unit)

    def update_freq_step(self):
        val = self.spin_freq_step.value()
        step_unit = self.combo_step_unit.currentText()
        main_unit = self.combo_unit.currentText()

        if step_unit == "kHz":
            step_hz = val * 1e3
        elif step_unit == "MHz":
            step_hz = val * 1e6
        elif step_unit == "GHz":
            step_hz = val * 1e9
        else:
            step_hz = val

        if main_unit == "kHz":
            final_step = step_hz / 1e3
        elif main_unit == "MHz":
            final_step = step_hz / 1e6
        elif main_unit == "GHz":
            final_step = step_hz / 1e9
        else:
            final_step = step_hz

        self.spin_freq.setSingleStep(final_step)

    def validate_frequency(self, value, unit):
        mhz_val = value
        if unit == "kHz":
            mhz_val = value / 1000.0
        elif unit == "GHz":
            mhz_val = value * 1000.0

        if mhz_val < 150.0:
            QMessageBox.warning(self, "Invalid Value",
                                "Below 150 MHz. Please enter a value between 150 MHz and 22.6 GHz.")
            return False
        elif mhz_val > 22600.0:
            QMessageBox.warning(self, "Invalid Value",
                                "You entered a value above 22.6 GHz. Please enter a value between 150 MHz and 22.6 GHz.")
            return False
        return True

    def refresh_ports(self):
        self.combo_port.clear()
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if ports:
            self.combo_port.addItems(ports)
            self.append_log("[SYS] COM ports refreshed.")
        else:
            self.combo_port.addItem("No Port Found")
            self.append_log("[SYS] No COM ports found!")

    def toggle_connection(self):
        if not self.worker.running:
            if self.combo_port.currentText() != "No Port Found":
                self.worker.connect_device(self.combo_port.currentText())
        else:
            self.worker.disconnect_device()

    def on_connection_changed(self, connected):
        self.btn_connect.setText("DISCONNECT" if connected else "CONNECT")
        self.btn_connect.setStyleSheet(
            f"background-color: {COLOR_ERROR if connected else COLOR_ACCENT}; color: #11111b;")
        self.btn_refresh.setEnabled(not connected)
        self.combo_port.setEnabled(not connected)

        if not connected:
            self.lbl_curr.setText("Current: --.- A")
            self.lbl_volt.setText("Voltage: --.- V")
            self.lbl_power.setText("Power: --.- W")
            self.lbl_temp.setText("Temperature: --.- C")
            self.lbl_pll.setText("LD Result: UNKNOWN")
            self.lbl_pll.setStyleSheet(
                f"background-color: #11111b; padding: 8px; border-radius: 4px; font-size: 13pt; font-weight: bold; color: {COLOR_WARNING};")
            self.lbl_fw_version.setText("Firmware: --")
            self.sw_filt.setEnabled(True)

            self.rf_state = False
            self.btn_rf.setText("RF OUTPUT: OFF")
            self.btn_rf.setStyleSheet(
                f"border: 2px solid {COLOR_ERROR}; color: {COLOR_ERROR}; padding: 15px; font-size: 14pt;")
            if self.worker.is_sweeping:
                self.worker.is_sweeping = False
                self.btn_sweep_toggle.setText("▶ START SWEEP")
                self.btn_sweep_toggle.setStyleSheet(
                    f"background-color: {COLOR_SUCCESS}; color: #000; font-size: 12pt; padding: 12px; font-weight: bold;")

    def apply_cw_settings(self):
        if not self.worker.running: return

        if not self.validate_frequency(self.spin_freq.value(), self.combo_unit.currentText()):
            return

        self.worker.send_cmd(":DISP:MENU CW")
        self.worker.send_cmd(f":FREQ {self.spin_freq.value()}{self.combo_unit.currentText()}")

        # Send the filter setting first so the power calculation logic can use the correct state.
        self.worker.send_cmd(f":FILT {'ON' if self.combo_filt.currentIndex() == 1 else 'OFF'}")
        self.worker.send_cmd(f":POW:LEV {self.spin_att.value()}")
        self.worker.send_cmd(f":OUTP {'ON' if self.rf_state else 'OFF'}")
        self.append_log(">>> CW Settings applied.")

    def toggle_rf(self):
        if not self.worker.running: return
        self.rf_state = not self.rf_state
        self.worker.send_cmd(f":OUTP {'ON' if self.rf_state else 'OFF'}")
        color = COLOR_SUCCESS if self.rf_state else COLOR_ERROR
        self.btn_rf.setText(f"RF OUTPUT: {'ON' if self.rf_state else 'OFF'}")
        self.btn_rf.setStyleSheet(f"border: 2px solid {color}; color: {color}; padding: 15px; font-size: 14pt;")

    def apply_sweep_settings(self):
        if not self.worker.running: return

        if not self.validate_frequency(self.sw_start.value(), self.sw_start_u.currentText()):
            return
        if not self.validate_frequency(self.sw_stop.value(), self.sw_stop_u.currentText()):
            return

        self.worker.send_cmd(":DISP:MENU SWP")
        self.worker.send_cmd(f":FILT {'ON' if self.sw_filt.currentIndex() == 1 else 'OFF'}")

        self.worker.send_cmd(f":SWEEP:STAR {self.sw_start.value()}{self.sw_start_u.currentText()}")
        self.worker.send_cmd(f":SWEEP:STOP {self.sw_stop.value()}{self.sw_stop_u.currentText()}")
        self.worker.send_cmd(f":SWEEP:STEP {self.sw_step.value()}{self.sw_step_u.currentText()}")
        self.worker.send_cmd(f":SWEEP:DWEL {self.sw_dwell.value()}")
        self.worker.send_cmd(f":SWEEP:COUN {self.sw_count.value()}")
        self.worker.send_cmd(f":SWEEP:POW {self.sw_pow.value()}")
        t_type = "LIN" if self.sw_type.currentIndex() == 0 else "LOG"
        self.worker.send_cmd(f":SWEEP:TYPE {t_type}")
        self.append_log(">>> Sweep Settings loaded.")

    def toggle_sweep(self):
        if not self.worker.running: return
        if not self.worker.is_sweeping:
            self.start_sweep()
        else:
            self.stop_sweep()

    def start_sweep(self):
        self.worker.is_sweeping = True
        self.sw_filt.setEnabled(False)
        self.worker.send_cmd(":DISP:MENU SWP")
        self.worker.send_cmd(":SWEEP:INIT")

        self.btn_sweep_toggle.setText("⏹ STOP SWEEP")
        self.btn_sweep_toggle.setStyleSheet(
            f"background-color: {COLOR_ERROR}; color: #11111b; font-size: 12pt; padding: 12px; font-weight: bold;")

        self.btn_rf.setText("RF OUTPUT: SWEEP MODE")
        self.btn_rf.setStyleSheet(
            f"border: 2px solid {COLOR_WARNING}; color: {COLOR_WARNING}; padding: 15px; font-size: 14pt;")
        self.rf_state = True
        self.append_log(">>> Sweep Started.")

    def stop_sweep(self):
        self.worker.abort_sweep_urgent()
        self._reset_sweep_ui(">>> Sweep Stopped.")

    def on_sweep_auto_stopped(self):
        # Called when the device stopped the sweep on its own (the configured
        # Sweep Count was reached) rather than the user pressing Stop. The
        # worker has already set is_sweeping = False and the device has
        # already turned the sweep/PLL off, so we only need to bring the UI
        # (button labels/colors) back in sync - no abort command needed.
        self._reset_sweep_ui(">>> Sweep finished (reached configured Count).")

    def _reset_sweep_ui(self, log_text):
        self.sw_filt.setEnabled(True)
        self.btn_sweep_toggle.setText("▶ START SWEEP")
        self.btn_sweep_toggle.setStyleSheet(
            f"background-color: {COLOR_SUCCESS}; color: #000; font-size: 12pt; padding: 12px; font-weight: bold;")

        self.rf_state = False
        self.btn_rf.setText("RF OUTPUT: OFF")
        self.btn_rf.setStyleSheet(
            f"border: 2px solid {COLOR_ERROR}; color: {COLOR_ERROR}; padding: 15px; font-size: 14pt;")
        self.append_log(log_text)

    # -------------------------------------------------------------------------
    # GUI telemetry update
    # Converts raw ADC values into user-facing current, voltage, power,
    # temperature, and PLL lock status values.
    # -------------------------------------------------------------------------
    def update_sensors(self, data):
        try:
            current_val = None
            voltage_val = None

            if "CURR" in data:
                current_val = float(data["CURR"])
                self.lbl_curr.setText(f"Current: {current_val:.2f} A")

            if "VOLT" in data:
                raw_volt = float(data["VOLT"])
                voltage_val = raw_volt * 2.0
                self.lbl_volt.setText(f"Voltage: {voltage_val:.2f} V")

            if current_val is not None and voltage_val is not None:
                power_w = current_val * voltage_val
                self.lbl_power.setText(f"Power: {power_w:.2f} W")

            if "TEMP" in data:
                v = float(data["TEMP"])
                temp_c = (v - 0.5) * 100.0
                self.lbl_temp.setText(f"Temperature: {temp_c:.1f} C")

            if "PLL" in data:
                val = int(data["PLL"], 16)
                rb_ld = (val >> 14) & 0x03
                if rb_ld == 2:
                    self.lbl_pll.setText("LD Result: LOCKED")
                    self.lbl_pll.setStyleSheet(
                        f"background-color: #11111b; padding: 8px; border-radius: 4px; font-size: 13pt; font-weight: bold; color: {COLOR_SUCCESS};")
                else:
                    self.lbl_pll.setText("LD Result: UNLOCKED")
                    self.lbl_pll.setStyleSheet(
                        f"background-color: #11111b; padding: 8px; border-radius: 4px; font-size: 13pt; font-weight: bold; color: {COLOR_ERROR};")
        except Exception:
            pass

    def append_log(self, msg):
        ts = datetime.datetime.now().strftime("%H:%M:%S")
        self.txt_log.append(f"[{ts}] {msg}")
        self.txt_log.verticalScrollBar().setValue(self.txt_log.verticalScrollBar().maximum())

    def clear_log(self):
        self.txt_log.clear()

    # -------------------------------------------------------------------------
    # Calibration CSV loader
    # Reads calibration rows from a CSV file and transfers them to the device in
    # small paced batches to avoid overloading the serial communication buffer.
    # -------------------------------------------------------------------------
    def load_calibration_csv(self):
        filepath, _ = QFileDialog.getOpenFileName(self, "Select Calibration CSV File", "",
                                                  "CSV Files (*.csv);;All Files (*)")
        if not filepath:
            return

        # Calibration file name must start with SN_XXX_
        # Example: SN_002_Calib_Optimized_FILT_F.csv
        filename = os.path.basename(filepath)
        serial_match = re.match(r"^(SN_\d{3})_", filename, re.IGNORECASE)

        if not serial_match:
            QMessageBox.warning(
                self,
                "Invalid Calibration File Name",
                "Calibration file name must start with SN_XXX_ format.\n\n"
                "Example: SN_002_Calib_Optimized_FILT_F.csv"
            )
            self.append_log(f"[ERR] Invalid calibration file name: {filename}")
            return

        device_serial = serial_match.group(1).upper()

        try:
            with open(filepath, 'r', encoding='utf-8-sig', errors='ignore') as f:
                lines = f.readlines()

            if len(lines) < 2:
                self.append_log("[ERR] CSV file is empty or invalid!")
                return

            self.append_log(f"[CAL] Device Serial Number: {device_serial}")
            self.append_log("[CAL] Reading calibration data...")

            self.worker.send_cmd(f":CAL:SER {device_serial}")
            self.worker.send_cmd(":CAL:CLEAR")

            success_count = 0
            for i in range(1, len(lines)):
                line = lines[i].strip()
                if not line: continue

                cols = line.split(';')
                if len(cols) < 18:
                    continue

                try:
                    freq = int(float(cols[0].strip().replace(',', '.')))

                    def parse_val(val_str, is_filton):
                        v = val_str.strip().replace(',', '.')
                        if not v or v.lower() == 'nan': return -1
                        try:
                            val_float = float(v)
                            if is_filton and not (2000 <= freq <= 18000):
                                return -1
                            return int(round(val_float))
                        except ValueError:
                            return -1

                    att6_on = parse_val(cols[1], True)
                    att3_on = parse_val(cols[3], True)
                    attn3_on = parse_val(cols[5], True)

                    att6_off = parse_val(cols[13], False)
                    att3_off = parse_val(cols[15], False)
                    attn3_off = parse_val(cols[17], False)

                    cmd = f":CAL:DATA {freq},{att6_on},{att3_on},{attn3_on},{att6_off},{att3_off},{attn3_off}"
                    self.worker.send_cmd(cmd)

                    # --- SERIAL BUFFER PROTECTION DELAY ---
                    # Add a short pause every 10 rows to prevent the serial port buffer from being overloaded.
                    # This helps protect the device RAM and serial communication queue during bulk transfer.
                    if i % 10 == 0:
                        time.sleep(0.05)

                    success_count += 1
                except Exception as e:
                    continue

            # Send the final save command after all calibration rows are transferred.
            time.sleep(0.1)  # Final short delay before saving calibration data.
            self.worker.send_cmd(":CAL:SAVE")

            self.append_log(f"[CAL] Transfer Complete! {success_count} rows uploaded to device in total.")
            QMessageBox.information(self, "Upload Complete",
                                    f"Device Serial Number: {device_serial}\n\n"
                                    f"A total of {success_count} frequency points were transferred to the device.\n"
                                    f"The device has saved this data to its persistent memory.")

        except Exception as e:
            self.append_log(f"[ERR] CSV upload error: {str(e)}")

    def flash_firmware(self):
        """Uploads the selected .bin file to the device via esptool (no Arduino IDE required)."""
        port = self.combo_port.currentText()
        if not port:
            QMessageBox.warning(self, "No Port Selected", "Please select a COM port first.")
            return

        # If the serial port is already open (connected), release it before flashing.
        if self.worker.running:
            self.append_log("[FW] Closing existing connection before flashing...")
            self.toggle_connection()

        filepath, _ = QFileDialog.getOpenFileName(self, "Select Firmware (.bin) File", "",
                                                   "Binary Files (*.bin);;All Files (*)")
        if not filepath:
            return

        reply = QMessageBox.question(
            self, "Firmware Update",
            f"Selected file:\n{filepath}\n\nPort: {port}\n\nStart the flashing process?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply != QMessageBox.StandardButton.Yes:
            return

        self.btn_firmware_update.setEnabled(False)
        self.append_log(f"[FW] Uploading firmware: {filepath}")
        self.append_log(f"[FW] Port: {port}")

        args = [
            "-m", "esptool",
            "--chip", "esp32s3",
            "--port", port,
            "--baud", "460800",
            "write_flash", "0x0", filepath
        ]

        self.firmware_process = QProcess(self)
        self.firmware_process.setProgram(sys.executable)
        self.firmware_process.setArguments(args)
        self.firmware_process.readyReadStandardOutput.connect(self._read_firmware_stdout)
        self.firmware_process.readyReadStandardError.connect(self._read_firmware_stderr)
        self.firmware_process.finished.connect(self._firmware_flash_finished)
        self.firmware_process.start()

    def _read_firmware_stdout(self):
        data = bytes(self.firmware_process.readAllStandardOutput()).decode(errors="ignore")
        for line in data.splitlines():
            if line.strip():
                self.append_log(f"[FW] {line.strip()}")

    def _read_firmware_stderr(self):
        data = bytes(self.firmware_process.readAllStandardError()).decode(errors="ignore")
        for line in data.splitlines():
            if line.strip():
                self.append_log(f"[FW] {line.strip()}")

    def _firmware_flash_finished(self, exit_code, exit_status):
        self.btn_firmware_update.setEnabled(True)
        if exit_code == 0:
            self.append_log("[FW] Firmware update complete.")
            QMessageBox.information(self, "Complete", "Firmware updated successfully.")
        else:
            self.append_log(f"[FW] Firmware update failed (exit code: {exit_code}).")
            QMessageBox.critical(self, "Error", "Firmware update failed. Check System Logs for details.")
        self.firmware_process = None

    def sync_ui_with_device(self, data):
        """Populates all UI fields using the JSON packet received from the device."""
        try:
            self.spin_freq.setValue(float(data.get("cw_freq", 1000)))
            self.combo_unit.setCurrentText(data.get("cw_unit", "MHz"))
            self.spin_att.setValue(float(data.get("cw_amp", 0)))
            filter_index = 1 if int(data.get("filt", 0)) else 0
            self.combo_filt.setCurrentIndex(filter_index)
            self.sw_filt.setCurrentIndex(filter_index)

            self.sw_start.setValue(float(data.get("sw_start", 300)))
            self.sw_start_u.setCurrentText(data.get("sw_start_u", "MHz"))
            self.sw_stop.setValue(float(data.get("sw_stop", 22600)))
            self.sw_stop_u.setCurrentText(data.get("sw_stop_u", "MHz"))
            self.sw_step.setValue(float(data.get("sw_step", 100)))
            self.sw_step_u.setCurrentText(data.get("sw_step_u", "MHz"))
            self.sw_dwell.setValue(float(data.get("sw_dwell", 1)))
            self.sw_count.setValue(int(float(data.get("sw_count", 0))))
            self.sw_pow.setValue(float(data.get("sw_amp", 10)))

            t_type = data.get("sw_type", "Lin")
            self.sw_type.setCurrentIndex(0 if t_type == "Lin" else 1)

            fw_build = data.get("fw_build")
            if fw_build:
                self.lbl_fw_version.setText(f"Firmware: {fw_build}")

            self.append_log("[SYS] State Synchronized with Device Screen.")
        except Exception as e:
            self.append_log(f"[ERR] Sync parsing error: {e}")


if __name__ == "__main__":
    app = QApplication(sys.argv)
    locale = QLocale(QLocale.Language.English, QLocale.Country.UnitedStates)
    QLocale.setDefault(locale)
    w = DSGMainWindow()
    w.show()
    sys.exit(app.exec())